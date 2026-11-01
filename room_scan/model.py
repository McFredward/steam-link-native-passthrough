"""Versioned semantic room model, shared by capture and future game adapters."""
from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum
import uuid

from .geometry import Mesh, Pose, RoomError, contains, dot, polygon, require, vector

SCHEMA = "sfnp-room-v1"
MAX_ELEMENTS = 256


def identifier(value):
    require(type(value) is str and len(value) == 36, "A canonical UUID is required")
    try:
        require(str(uuid.UUID(value)) == value, "A canonical UUID is required")
    except (ValueError, AttributeError) as error:
        raise RoomError("Invalid UUID") from error
    return value


def text(value, maximum=80):
    require(type(value) is str and 1 <= len(value) <= maximum
            and value == value.strip() and all(ord(c) >= 32 and ord(c) != 127 for c in value), "Invalid name")
    return value


class Label(str, Enum):
    FLOOR = "FLOOR"
    CEILING = "CEILING"
    WALL_FACE = "WALL_FACE"
    DOOR_FRAME = "DOOR_FRAME"
    WINDOW_FRAME = "WINDOW_FRAME"
    TABLE = "TABLE"
    COUCH = "COUCH"
    BED = "BED"
    STORAGE = "STORAGE"
    OTHER = "OTHER"


@dataclass(frozen=True)
class Geometry:
    boundary: tuple[tuple[float, float], ...] = ()
    # Optional local-axis box. Centre is explicit: a tabletop plane and its
    # furniture volume do not generally share a centre.
    box_size: tuple[float, float, float] | None = None
    box_center: tuple[float, float, float] | None = None

    def __post_init__(self):
        object.__setattr__(self, "boundary", polygon(self.boundary) if self.boundary else ())
        require(bool(self.boundary) or self.box_size is not None, "Element geometry is empty")
        require((self.box_size is None) == (self.box_center is None), "Box centre and size must be provided together")
        if self.box_size is not None:
            size = vector(self.box_size, 3, 100.)
            require(all(x >= .01 for x in size), "Box has an empty dimension")
            object.__setattr__(self, "box_size", size)
            object.__setattr__(self, "box_center", vector(self.box_center, 3, 100.))

    def to_dict(self):
        return {"boundary_xy": [list(p) for p in self.boundary],
                "box_size": list(self.box_size) if self.box_size is not None else None,
                "box_center": list(self.box_center) if self.box_center is not None else None}

    @classmethod
    def from_dict(cls, value):
        require(type(value) is dict and set(value) == {"boundary_xy", "box_size", "box_center"}, "Invalid geometry record")
        return cls(value["boundary_xy"], value["box_size"], value["box_center"])


@dataclass(frozen=True)
class Element:
    id: str
    label: Label
    name: str
    pose: Pose
    geometry: Geometry
    parent_wall: str | None = None
    confirmed: bool = False
    origin: str = "manual"

    def __post_init__(self):
        identifier(self.id)
        require(isinstance(self.label, Label), "Unknown element label")
        text(self.name)
        require(isinstance(self.pose, Pose) and isinstance(self.geometry, Geometry), "Invalid element geometry")
        require(type(self.confirmed) is bool and self.origin in ("manual", "detected"), "Invalid element provenance")
        if self.parent_wall is not None:
            identifier(self.parent_wall)
        structural = (Label.FLOOR, Label.CEILING, Label.WALL_FACE, Label.DOOR_FRAME, Label.WINDOW_FRAME, Label.TABLE)
        require(self.label not in structural or bool(self.geometry.boundary), "This element needs a surface")
        require(self.label not in (Label.COUCH, Label.BED, Label.STORAGE, Label.OTHER)
                or self.geometry.box_size is not None, "Furniture needs a bounded volume")
        require(self.label not in (Label.DOOR_FRAME, Label.WINDOW_FRAME) or self.parent_wall is not None,
                "Door/window needs a parent wall")
        require(self.parent_wall is None or self.label in (Label.DOOR_FRAME, Label.WINDOW_FRAME), "Only openings belong to a wall")
        normal = self.pose.rotate((0., 0., 1.))
        if self.label in (Label.FLOOR, Label.CEILING, Label.TABLE):
            require(abs(normal[1]) >= .98, "Horizontal surface required")
        if self.label in (Label.WALL_FACE, Label.DOOR_FRAME, Label.WINDOW_FRAME):
            require(abs(normal[1]) <= .02, "Vertical surface required")

    def to_dict(self):
        return {"id": self.id, "label": self.label.value, "name": self.name,
                "pose": self.pose.to_dict(), "geometry": self.geometry.to_dict(),
                "parent_wall": self.parent_wall, "confirmed": self.confirmed, "origin": self.origin}

    @classmethod
    def from_dict(cls, value):
        fields = {"id", "label", "name", "pose", "geometry", "parent_wall", "confirmed", "origin"}
        require(type(value) is dict and set(value) == fields, "Invalid element record")
        try:
            label = Label(value["label"])
        except (ValueError, TypeError) as error:
            raise RoomError("Unknown element label") from error
        return cls(value["id"], label, value["name"], Pose.from_dict(value["pose"]),
                   Geometry.from_dict(value["geometry"]), value["parent_wall"], value["confirmed"], value["origin"])


@dataclass(frozen=True)
class Room:
    id: str
    name: str
    revision: int
    origin_id: str
    calibration_id: str
    elements: tuple[Element, ...] = ()
    partial_scan_accepted: bool = False
    test_only: bool = False
    mesh: Mesh = Mesh()

    def __post_init__(self):
        identifier(self.id)
        identifier(self.origin_id)
        identifier(self.calibration_id)
        text(self.name)
        require(type(self.revision) is int and 0 <= self.revision <= 1000000, "Invalid room revision")
        require(type(self.partial_scan_accepted) is bool and type(self.test_only) is bool, "Invalid room status")
        require(isinstance(self.mesh, Mesh), "Invalid room mesh")
        require(isinstance(self.elements, (tuple, list)) and len(self.elements) <= MAX_ELEMENTS, "Too many elements")
        object.__setattr__(self, "elements", tuple(self.elements))
        require(all(isinstance(e, Element) for e in self.elements), "Invalid room element")
        by_id = {e.id: e for e in self.elements}
        require(len(by_id) == len(self.elements), "Duplicate element ID")
        for e in self.elements:
            if e.parent_wall is None:
                continue
            parent = by_id.get(e.parent_wall)
            require(parent is not None and parent.label == Label.WALL_FACE, "Parent wall missing")
            require(not e.confirmed or parent.confirmed, "Opening needs a confirmed wall")
            require(abs(dot(parent.pose.rotate((0., 0., 1.)), e.pose.rotate((0., 0., 1.)))) >= .999,
                    "Opening and wall normals differ")
            for point in e.geometry.boundary:
                local = parent.pose.inverse_transform(e.pose.transform((*point, 0.)))
                require(abs(local[2]) <= .02 and contains(parent.geometry.boundary, local[:2]),
                        "Door/window lies outside its wall")

    def put(self, element: Element):
        elements = tuple(element if e.id == element.id else e for e in self.elements)
        if not any(e.id == element.id for e in self.elements):
            elements += (element,)
        return replace(self, elements=elements)

    def remove(self, element_id: str):
        require(any(e.id == element_id for e in self.elements), "Element missing")
        require(not any(e.parent_wall == element_id for e in self.elements), "Remove or reassign wall openings first")
        return replace(self, elements=tuple(e for e in self.elements if e.id != element_id))

    def query(self, label: Label):
        require(isinstance(label, Label), "Unknown element label")
        return tuple(e for e in self.elements if e.label == label and e.confirmed)

    def to_dict(self):
        return {"schema": SCHEMA, "id": self.id, "name": self.name, "revision": self.revision,
                "origin_id": self.origin_id, "calibration_id": self.calibration_id,
                "camera_family": "arcturus", "coordinates": "metres-right-handed-y-up-negative-z-forward",
                "elements": [e.to_dict() for e in self.elements],
                "partial_scan_accepted": self.partial_scan_accepted, "test_only": self.test_only,
                "mesh": self.mesh.to_dict()}

    @classmethod
    def from_dict(cls, value):
        fields = {"schema", "id", "name", "revision", "origin_id", "calibration_id", "camera_family",
                  "coordinates", "elements", "partial_scan_accepted", "test_only", "mesh"}
        require(type(value) is dict and set(value) == fields and value["schema"] == SCHEMA
                and value["camera_family"] == "arcturus"
                and value["coordinates"] == "metres-right-handed-y-up-negative-z-forward", "Unsupported room format")
        require(type(value["elements"]) is list and len(value["elements"]) <= MAX_ELEMENTS, "Invalid element collection")
        return cls(value["id"], value["name"], value["revision"], value["origin_id"], value["calibration_id"],
                   tuple(Element.from_dict(e) for e in value["elements"]), value["partial_scan_accepted"],
                   value["test_only"], Mesh.from_dict(value["mesh"]))
