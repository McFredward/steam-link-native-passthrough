"""Metres, right-handed room coordinates: +X right, +Y up, -Z forward.

Planes lie in an element's local XY plane. Its +Z axis is the normal. Camera
poses use the same axes; positive optical depth therefore points along -Z.
"""
from __future__ import annotations

from dataclasses import dataclass
import math


class RoomError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RoomError(message)


def number(value: object, limit: float = 1000.0) -> float:
    require(type(value) in (int, float), "A finite number is required")
    result = float(value)
    require(math.isfinite(result) and abs(result) <= limit, "Number outside room limits")
    return result


def vector(value: object, count: int = 3, limit: float = 1000.0) -> tuple[float, ...]:
    require(isinstance(value, (list, tuple)) and len(value) == count, "Invalid vector")
    return tuple(number(x, limit) for x in value)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def distance(a, b):
    return math.sqrt(dot(sub(a, b), sub(a, b)))


@dataclass(frozen=True)
class Pose:
    position: tuple[float, float, float] = (0., 0., 0.)
    orientation: tuple[float, float, float, float] = (0., 0., 0., 1.)

    def __post_init__(self):
        object.__setattr__(self, "position", vector(self.position))
        q = vector(self.orientation, 4, 1.)
        require(abs(dot(q, q) - 1.) < 1e-5, "Quaternion must be normalized")
        object.__setattr__(self, "orientation", q)

    def rotate(self, point):
        x, y, z, w = self.orientation
        a, b, c = point
        tx, ty, tz = 2 * (y*c-z*b), 2 * (z*a-x*c), 2 * (x*b-y*a)
        return (a+w*tx+y*tz-z*ty, b+w*ty+z*tx-x*tz, c+w*tz+x*ty-y*tx)

    def transform(self, point):
        return tuple(a+b for a, b in zip(self.position, self.rotate(point)))

    def inverse_transform(self, point):
        x, y, z, w = self.orientation
        return Pose((0., 0., 0.), (-x, -y, -z, w)).rotate(sub(point, self.position))

    def inverse(self):
        x, y, z, w = self.orientation
        inverse = Pose((0., 0., 0.), (-x, -y, -z, w))
        return Pose(inverse.rotate(tuple(-p for p in self.position)), inverse.orientation)

    def compose(self, other):
        x, y, z, w = self.orientation
        a, b, c, d = other.orientation
        q = (w*a+x*d+y*c-z*b, w*b-x*c+y*d+z*a, w*c+x*b-y*a+z*d, w*d-x*a-y*b-z*c)
        norm = math.sqrt(dot(q, q))
        return Pose(self.transform(other.position), tuple(v/norm for v in q))

    def to_dict(self):
        return {"position": list(self.position), "orientation_xyzw": list(self.orientation)}

    @classmethod
    def from_dict(cls, value):
        require(type(value) is dict and set(value) == {"position", "orientation_xyzw"}, "Invalid pose record")
        return cls(vector(value["position"]), vector(value["orientation_xyzw"], 4, 1.))


def polygon(value):
    require(isinstance(value, (tuple, list)) and 3 <= len(value) <= 64, "Plane needs 3 to 64 boundary points")
    points = tuple(vector(p, 2, 100.) for p in value)
    require(len(set(points)) == len(points), "Repeated polygon corner")
    area = sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(points, points[1:]+points[:1])) / 2
    require(area >= .0001, "Boundary must have positive area and counter-clockwise winding")
    # Convex boundaries keep controller editing and wall containment unambiguous.
    for i in range(len(points)):
        a, b, c = points[i-2], points[i-1], points[i]
        require((b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]) > 1e-9,
                "Boundary must be strictly convex")
    # Local turns alone also admit star polygons; every other point must lie
    # inside every oriented edge's half-plane.
    for a, b in zip(points, points[1:]+points[:1]):
        require(all((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]) >= -1e-9
                    for p in points), "Self-intersecting boundary")
    return points


def contains(boundary, point, tolerance=.002):
    return all((b[0]-a[0])*(point[1]-a[1])-(b[1]-a[1])*(point[0]-a[0])
               >= -tolerance * math.hypot(b[0]-a[0], b[1]-a[1])
               for a, b in zip(boundary, boundary[1:]+boundary[:1]))


@dataclass(frozen=True)
class Mesh:
    vertices: tuple[tuple[float, float, float], ...] = ()
    triangles: tuple[tuple[int, int, int], ...] = ()
    quality: tuple[str, ...] = ()

    def __post_init__(self):
        require(len(self.vertices) <= 100000 and len(self.triangles) <= 200000, "Mesh budget exceeded")
        vertices = tuple(vector(v) for v in self.vertices)
        require(len(self.quality) == len(vertices) and all(q in ("building", "uncertain", "reliable")
                for q in self.quality), "Invalid mesh quality")
        triangles = []
        for triangle in self.triangles:
            require(isinstance(triangle, (list, tuple)) and len(triangle) == 3
                    and all(type(i) is int and 0 <= i < len(vertices) for i in triangle)
                    and len(set(triangle)) == 3, "Invalid mesh triangle")
            a, b, c = (vertices[i] for i in triangle)
            u, v = sub(b, a), sub(c, a)
            cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
            require(dot(cross, cross) > 1e-14, "Degenerate mesh triangle")
            triangles.append(tuple(triangle))
        object.__setattr__(self, "vertices", vertices)
        object.__setattr__(self, "triangles", tuple(triangles))
        object.__setattr__(self, "quality", tuple(self.quality))

    def to_dict(self):
        return {"vertices": [list(v) for v in self.vertices], "triangles": [list(t) for t in self.triangles],
                "quality": list(self.quality)}

    @classmethod
    def from_dict(cls, value):
        require(type(value) is dict and set(value) == {"vertices", "triangles", "quality"}
                and all(type(value[k]) is list for k in value), "Invalid mesh record")
        return cls(value["vertices"], value["triangles"], value["quality"])
