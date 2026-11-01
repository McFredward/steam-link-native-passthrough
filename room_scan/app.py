"""Headset application controller; owned native child pipe, no network listener.

The native overlay owns tracking, camera acquisition and rendering. This process
owns room editing and persistence. A camera-ready record comes only from that
owned native process after explicit Start; default startup never scans.
"""
from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import sys

from .capture import Binding, DepthFrame
from .editor import SurfaceDraft, pick_surface
from .geometry import Mesh, Pose, RoomError, require
from .model import Label, Room
from .store import MAX_BYTES, RoomStore, decode
from .workflow import ScanSession, Stage

LABELS = {Label.FLOOR: "Floor", Label.CEILING: "Ceiling", Label.WALL_FACE: "Wall",
          Label.DOOR_FRAME: "Door", Label.WINDOW_FRAME: "Window", Label.TABLE: "Table",
          Label.COUCH: "Couch", Label.BED: "Bed", Label.STORAGE: "Storage", Label.OTHER: "Other furniture"}
VOLUMES = (Label.COUCH, Label.BED, Label.STORAGE, Label.OTHER)


class PipeProvider:
    def __init__(self):
        self.binding = None

    def open(self):
        if self.binding is None:
            raise RuntimeError("Camera has not been opened")
        return self.binding

    def close(self):
        self.binding = None


class Application:
    def __init__(self, store: RoomStore):
        self.store, self.provider = store, PipeProvider()
        self.session = None
        self.page = "home"
        self.notice = ""
        self.draft = self.pending = None
        self.label = None
        self.furniture_depth = .8
        self.measure_with_controller = False
        self.selected = None
        self.world_revision = 0
        self.starting = False
        self.list_page = 0
        self.camera_request_page = "prepare"
        self.alignment_room = self.alignment_anchor = self.aligned_preview = None
        self.rooms = self.saved_rooms()

    def saved_rooms(self):
        rooms = []
        for head in sorted(self.store.root.glob("*.head"))[:64]:
            try:
                room = self.store.load(head.stem)
                if not room.test_only:
                    rooms.append(room)
            except (RoomError, OSError):
                continue
        return rooms

    def action(self, action):
        self.notice = ""
        if action == "new":
            self.page = "prepare"
        elif action == "back":
            if self.page in ("alignment", "prepare", "saved"):
                self.page = "home"
            elif self.page == "align_place":
                self.session.cancel()
                self.page = "alignment"
            elif self.session and self.session.stage == Stage.REVIEW:
                self.page = "review"
            else:
                self.page = "scan" if self.session and self.session.stage in (Stage.SCANNING, Stage.PAUSED) else "home"
            self.draft = self.pending = None
            self.world_revision += 1
        elif action in ("page_next", "page_previous"):
            self.list_page = max(0, self.list_page + (1 if action == "page_next" else -1))
        elif action == "pause":
            self.session.pause()
        elif action == "resume":
            raise RoomError("Native tracking and capture must be checked before resuming")
        elif action == "add":
            require(self.session is not None and self.session.view()["can_add_element"], "No editable room")
            self.page = "types"
        elif action.startswith("type:"):
            self.selected = None
            self.label = Label(action.split(":", 1)[1])
            if self.label in (Label.WINDOW_FRAME, Label.DOOR_FRAME):
                walls = self.session.room.query(Label.WALL_FACE)
                require(bool(walls), "Add the containing wall first.")
                self.page = "walls"
                self.list_page = 0
            else:
                self.begin_element()
        elif action.startswith("wall:"):
            self.begin_element(parent=action.split(":", 1)[1])
        elif action == "measure_controller":
            self.measure_with_controller = not self.measure_with_controller
        elif action == "corner_undo":
            self.draft.undo_corner()
            self.pending = None
            self.aligned_preview = None
        elif action in ("depth_more", "depth_less"):
            self.furniture_depth = round(max(.05, min(3., self.furniture_depth + (.05 if action == "depth_more" else -.05))), 2)
            self.finish_draft()
        elif action == "confirm_element":
            require(self.pending is not None, "No measured element to confirm")
            self.session.put_element(replace(self.pending, confirmed=True))
            self.draft = self.pending = None
            self.page = "scan"
            self.world_revision += 1
        elif action == "confirm_alignment":
            require(self.aligned_preview is not None, "Measure the reference surface first")
            self.session.reconstruction.retain_aligned_mesh(self.aligned_preview.mesh)
            self.session.previous = self.alignment_room
            self.session.room = self.aligned_preview
            self.session.stage = Stage.REVIEW
            self.session.tracking_verified = True
            self.pending = self.draft = self.aligned_preview = None
            self.page = "review"
            self.world_revision += 1
        elif action == "elements":
            self.page = "elements"
            self.list_page = 0
        elif action.startswith("element:"):
            self.selected = action.split(":", 1)[1]
            self.page = "edit"
        elif action == "delete_element":
            self.session.remove_element(self.selected)
            self.page = "elements"
            self.world_revision += 1
        elif action == "remeasure":
            element = next(e for e in self.session.room.elements if e.id == self.selected)
            self.label = element.label
            self.begin_element(parent=element.parent_wall, element_id=element.id)
        elif action == "undo":
            self.session.undo()
            self.world_revision += 1
        elif action == "review":
            self.session.review()
            self.page = "review"
            self.world_revision += 1
        elif action == "continue":
            self.session.continue_scan()
            self.page = "scan"
        elif action == "save":
            self.session.accept_captured_area()
            self.session.save(self.store)
            self.rooms = self.saved_rooms()
            self.page = "saved"
            self.world_revision += 1
        elif action == "cancel":
            if self.session and self.session.stage not in (Stage.CANCELLED, Stage.SAVED):
                if self.session.room and self.session.binding:
                    self.checkpoint()
                self.session.cancel()
            self.draft = self.pending = None
            self.page = "home"
            self.world_revision += 1
        elif action.startswith("load:"):
            self.selected = action.split(":", 1)[1]
            self.alignment_room = self.store.load(self.selected)
            surfaces = [e for e in self.alignment_room.elements if e.label in (Label.TABLE, Label.FLOOR, Label.WALL_FACE)
                        and len(e.geometry.boundary) == 4]
            self.alignment_anchor = next((e for e in surfaces if e.label == Label.TABLE), surfaces[0] if surfaces else None)
            self.page = "alignment"
        else:
            raise RoomError("Unknown room action")

    def begin_element(self, parent=None, element_id=None):
        require(self.session and self.session.binding, "No current room origin")
        self.draft = SurfaceDraft(Label.TABLE if self.label in VOLUMES else self.label,
                                  self.session.binding.origin_id, parent_wall=parent, element_id=element_id)
        self.pending = None
        self.furniture_depth = .8
        self.page = "place"

    def finish_draft(self):
        if self.draft and len(self.draft.points) == 3:
            name = LABELS[self.label]
            count = sum(e.label == self.label for e in self.session.room.elements)
            if count:
                name += f" {count+1}"
            if self.selected:
                old = next((e for e in self.session.room.elements if e.id == self.selected), None)
                if old:
                    name = old.name
            element = self.draft.finish(name, furniture_depth_m=self.furniture_depth
                        if self.label == Label.TABLE or self.label in VOLUMES else None)
            self.pending = replace(element, label=self.label)
            if self.page == "align_place":
                self.aligned_preview = None
                original = self.alignment_anchor
                old_width = original.geometry.boundary[1][0]-original.geometry.boundary[0][0]
                old_height = original.geometry.boundary[2][1]-original.geometry.boundary[1][1]
                width = self.pending.geometry.boundary[1][0]-self.pending.geometry.boundary[0][0]
                height = self.pending.geometry.boundary[2][1]-self.pending.geometry.boundary[1][1]
                require(abs(width-old_width) <= max(.05, old_width*.05) and abs(height-old_height) <= max(.05, old_height*.05),
                        "Reference surface dimensions differ")
                transform = self.pending.pose.compose(original.pose.inverse())
                require(transform.rotate((0., 1., 0.))[1] >= .999, "Reference corner order would invert the room")
                previous = self.alignment_room
                mesh = Mesh(tuple(transform.transform(p) for p in previous.mesh.vertices), previous.mesh.triangles, previous.mesh.quality)
                elements = tuple(replace(e, pose=transform.compose(e.pose)) for e in previous.elements)
                self.aligned_preview = replace(previous, origin_id=self.session.binding.origin_id,
                    calibration_id=self.session.binding.calibration_id, elements=elements, mesh=mesh, partial_scan_accepted=False)
            self.world_revision += 1

    def event(self, event):
        require(type(event) is dict and type(event.get("event")) is str, "Invalid native event")
        kind = event["event"]
        if kind == "action":
            self.action(event["action"])
        elif kind == "camera_warming":
            self.starting = True
            self.camera_request_page = self.page
        elif kind == "camera_ready":
            require(self.page in ("prepare", "alignment"), "Camera may open only after explicit scan start")
            align = self.page == "alignment"
            b = event["binding"]
            self.provider.binding = Binding(b["session_id"], b["origin_id"], b["calibration_id"], test_only=False)
            self.session = ScanSession(f"Room {len(self.rooms)+1}", provider=self.provider)
            require(self.session.start(), "Capture could not start")
            self.page = "scan"
            self.starting = False
            if align:
                require(self.alignment_anchor is not None, "No reference surface available")
                self.label = self.alignment_anchor.label
                self.draft = SurfaceDraft(self.label, self.session.binding.origin_id, element_id=self.alignment_anchor.id)
                self.pending = self.aligned_preview = None
                self.page = "align_place"
        elif kind == "camera_resumed":
            require(self.session and self.session.stage == Stage.PAUSED, "Scan is not paused")
            b = event["binding"]
            binding = Binding(b["session_id"], b["origin_id"], b["calibration_id"], test_only=False)
            require(self.session.resume(binding, tracking_valid=event["tracking_valid"]), "Room must be aligned again")
            self.provider.binding = binding
            self.session.provider_open = True
            self.starting = False
        elif kind == "camera_unavailable":
            reason = event.get("reason")
            self.notice = {
                "camera-calibration-unverified": "Arcturus calibration could not be verified. Scanning has not started.",
                "camera-module-association-unverified": "Camera images do not match the Arcturus calibration. Scanning has not started.",
                "head-tracking-unavailable": "Room tracking is not ready. Look around and try again.",
                "public-camera-api-unavailable": "The public camera interface is unavailable. Scanning has not started.",
                "camera-not-exposed": "SteamVR reports no accessible camera. Scanning has not started.",
                "camera-count-unsupported": "The reported camera arrangement is not supported yet. Scanning has not started.",
                "camera-layout-unsupported": "The camera image has no verified stereo layout. Scanning has not started.",
            }.get(reason, "The stereo camera could not be verified. Scanning has not started.")
            if type(reason) is str and 0 < len(reason) <= 64 and all(c in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in reason):
                self.notice += " Diagnostic code: "+reason
            self.page = self.camera_request_page if self.camera_request_page in ("prepare", "alignment") else "scan"
            self.starting = False
        elif kind == "depth":
            if self.session and self.session.stage == Stage.SCANNING:
                b = event["binding"]
                binding = Binding(b["session_id"], b["origin_id"], b["calibration_id"], test_only=False)
                frame = DepthFrame(binding, event["sequence"], event["exposure_ticks"], Pose.from_dict(event["pose"]),
                                   event["width"], event["height"], *event["intrinsics"], event["depth_m"], event["confidence"])
                self.session.ingest(frame)
                self.world_revision += 1
        elif kind == "tracking_lost":
            if self.session and self.session.stage in (Stage.SCANNING, Stage.PAUSED, Stage.REVIEW):
                self.session.tracking_verified = False
                self.session.stage = Stage.PAUSED
                self.notice = "Room tracking lost. Scanning is paused."
                self.world_revision += 1
        elif kind == "point":
            require(self.page in ("place", "align_place") and self.draft is not None, "No element is being placed")
            require(self.session.tracking_verified and event["origin_id"] == self.draft.origin_id, "Room tracking changed")
            if self.measure_with_controller:
                point = event["controller_position"]
            else:
                hit = pick_surface(self.session.reconstruction.mesh(), event["ray_origin"], event["ray_direction"])
                require(hit is not None, "No reliable scanned surface here. Capture more views or measure at the controller.")
                point = hit[1]
            self.draft.add_corner(point, origin_id=event["origin_id"], sample_ns=event["sample_ns"], tracking_valid=True)
            self.finish_draft()
            self.world_revision += 1
        elif kind == "rename":
            require(self.session and self.session.room, "No room to rename")
            self.session.room = replace(self.session.room, name=event["name"])
        elif kind == "rename_element":
            require(self.page == "edit" and self.selected, "No selected element")
            element = next(e for e in self.session.room.elements if e.id == self.selected)
            self.session.put_element(replace(element, name=event["name"]))
            self.world_revision += 1
        else:
            raise RoomError("Unknown native event")

    def view(self):
        buttons = []
        def button(action, label, enabled=True, primary=False):
            index = len(buttons)
            buttons.append({"action": action, "label": label, "enabled": bool(enabled), "primary": primary,
                            "x": 60+(index % 2)*462, "y": 330+(index//2)*74, "w": 442, "h": 60})
        def listing(items, prefix):
            count = (len(items)+7)//8
            self.list_page = min(self.list_page, max(0, count-1))
            for item in items[self.list_page*8:(self.list_page+1)*8]:
                button(prefix+item.id, item.name)
            if count > 1:
                button("page_previous", "Previous page", self.list_page > 0)
                button("page_next", "More entries", self.list_page+1 < count)
        title, subtitle, step = "Your room", "Set up your surroundings for mixed reality.", 0
        if self.page == "home":
            button("new", "Scan a new room", primary=True)
            listing(self.rooms, "load:")
        elif self.page == "prepare":
            title, subtitle, step = "Capture your room", "Look around slowly. Capture the floor, walls and furniture from several viewpoints.", 1
            button("start", "Checking camera" if self.starting else "Start scan", not self.starting, primary=True)
            button("back", "Back", not self.starting)
        elif self.page == "scan":
            title, subtitle, step = "Your room is taking shape", "Teal marks captured surfaces. Amber areas need more views.", 2
            active = self.session.stage == Stage.SCANNING
            button("pause" if active else "resume", "Pause" if active else "Resume", not self.starting)
            button("add", "Add element", self.session.tracking_verified, True)
            button("elements", "Edit elements")
            button("review", "Review scan", self.session.view()["can_review"])
            button("undo", "Undo", bool(self.session._history))
            button("cancel", "End setup")
        elif self.page in ("types", "walls", "elements", "edit"):
            title, subtitle, step = "Room elements", "Choose surfaces and furniture, then adjust their boundaries.", 3
            if self.page == "types":
                for label, name in LABELS.items():
                    button("type:"+label.value, name)
            elif self.page == "walls":
                subtitle = "Which wall contains this door or window?"
                listing(self.session.room.query(Label.WALL_FACE), "wall:")
            elif self.page == "elements":
                listing(self.session.room.elements, "element:")
            else:
                button("remeasure", "Change boundary", primary=True)
                button("rename_element", "Name element")
                button("delete_element", "Remove element")
            button("back", "Back")
        elif self.page in ("place", "align_place"):
            title, step = "Add "+LABELS[self.label], 3
            align = self.page == "align_place"
            if align:
                title = "Align saved room"
            if self.pending:
                subtitle = "Check the highlighted boundary in your room, then confirm the element."
                if align:
                    subtitle = "Check that the room preview matches your real surroundings."
                button("confirm_alignment" if align else "confirm_element", "Confirm alignment" if align else "Confirm element",
                       not align or self.aligned_preview is not None, primary=True)
                if not align and (self.label == Label.TABLE or self.label in VOLUMES):
                    subtitle += f" Furniture height: {self.furniture_depth:.2f} m"
                    button("depth_less", "Lower")
                    button("depth_more", "Higher")
            else:
                instructions = ("Mark the first corner", "Mark the adjacent corner", "Mark the opposite corner")
                subtitle = instructions[min(2, len(self.draft.points))]+". Press the trigger to confirm."
                if align:
                    subtitle = self.alignment_anchor.name+": "+subtitle
                button("measure_controller", "Measure at controller" if not self.measure_with_controller else "Point at scanned surface")
            button("corner_undo", "Undo last corner", bool(self.draft.points))
            button("back", "Cancel")
        elif self.page == "review":
            title, subtitle, step = "Does your room look right?", "Check surfaces and elements. Add a table, floor or wall for future alignment. Unscanned areas remain unknown.", 4
            button("save", "Save captured area", primary=True)
            button("continue", "Continue scanning")
            button("elements", "Edit elements")
            button("rename", "Name room")
        elif self.page == "saved":
            title, subtitle, step = "Your room has been saved", self.session.room.name, 5
            button("back", "Back to rooms", primary=True)
        elif self.page == "alignment":
            title, subtitle = "Check room alignment", "Align the saved room with your current surroundings before using it."
            if self.alignment_anchor is None:
                subtitle = "This room has no reference surface. Set up a new room and add a table, floor or wall."
                button("new", "Scan a new room", primary=True)
            button("align_start", "Align saved room", self.alignment_anchor is not None and not self.starting, primary=True)
            button("back", "Back", not self.starting)
        state = self.session.view() if self.session else None
        if self.page == "align_place" and self.aligned_preview is not None and state is not None:
            state = {**state, "mesh": self.aligned_preview.mesh.to_dict(),
                     "elements": [e.to_dict() for e in self.aligned_preview.elements]}
        if state is not None and self.page in ("home", "prepare", "alignment", "saved"):
            state = {**state, "world_visuals_allowed": False, "mesh": None}
        return {"schema": "sfnp-room-dashboard-v1", "page": self.page, "title": title, "subtitle": subtitle,
                "notice": self.notice, "step": step, "buttons": buttons,
                "capture_active": bool(self.session and self.session.provider_open),
                "capture_collecting": bool(self.session and self.session.stage == Stage.SCANNING),
                "camera_check_pending": self.starting,
                "world_revision": self.world_revision, "room": state,
                "pending": self.pending.to_dict() if self.pending else None,
                "corners": list(self.draft.points) if self.draft else [],
                "controller_marker": self.page in ("place", "align_place") and self.measure_with_controller}

    def checkpoint(self):
        room = self.session.room
        if self.session.reconstruction is not None and (self.session.reconstruction.cells or self.session.reconstruction.base_mesh.vertices):
            room = replace(room, mesh=self.session.reconstruction.mesh())
        self.store.save_draft(self.session.binding.session_id, room)

    def close(self):
        if self.session:
            if self.session.room and self.session.binding and self.session.stage not in (Stage.SAVED, Stage.CANCELLED):
                self.checkpoint()
            self.session.close()


def run(root, incoming, outgoing):
    app = Application(RoomStore(root))
    errors = 0
    def emit():
        raw = (json.dumps(app.view(), ensure_ascii=False, allow_nan=False, separators=(",", ":"))+"\n").encode()
        require(len(raw) <= MAX_BYTES, "Dashboard snapshot exceeds its budget")
        outgoing.write(raw)
        outgoing.flush()
    try:
        emit()
        while True:
            raw = incoming.readline(MAX_BYTES+1)
            if not raw:
                break
            require(len(raw) <= MAX_BYTES and raw.endswith(b"\n"), "Native event exceeds its budget")
            event = None
            try:
                event = decode(raw)
                app.event(event)
            except (RoomError, OSError, ValueError, KeyError, TypeError, StopIteration) as error:
                if errors < 64:
                    kind = event.get("event", "invalid") if isinstance(event, dict) else "invalid"
                    kind = kind if type(kind) is str and kind.isascii() and kind.replace("_", "").isalnum() and len(kind) <= 64 else "invalid"
                    print(f"Room controller: event={kind} error={type(error).__name__}", file=sys.stderr)
                errors += 1
                app.notice = "This action could not be completed. Check room alignment and your input."
            emit()
    finally:
        app.close()


if __name__ == "__main__":
    require(len(sys.argv) == 2, "An explicit private room-data directory is required")
    run(Path(sys.argv[1]), sys.stdin.buffer, sys.stdout.buffer)
