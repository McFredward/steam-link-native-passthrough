"""Capture/edit/review/save state machine. Provider failure cannot start a scan."""
from __future__ import annotations

from dataclasses import replace
from enum import Enum
import uuid

from .capture import ArcturusProviderUnavailable, Binding, DepthFrame, Reconstruction
from .geometry import RoomError, require
from .model import Element, Label, Room, text


class Stage(str, Enum):
    CHECKING = "checking"
    UNAVAILABLE = "unavailable"
    ALIGNMENT_REQUIRED = "alignment_required"
    SCANNING = "scanning"
    PAUSED = "paused"
    REVIEW = "review"
    SAVED = "saved"
    CANCELLED = "cancelled"


class ScanSession:
    def __init__(self, name="Mein Raum", *, provider=None, previous: Room | None = None):
        text(name)
        require(previous is None or isinstance(previous, Room), "Invalid previous room")
        self.name, self.previous = name, previous
        self.room = previous
        self.provider = provider if provider is not None else ArcturusProviderUnavailable()
        self.stage = Stage.CHECKING
        self.message = "Kamera und Raumverfolgung prüfen"
        self.reconstruction = None
        self.binding = None
        self._history = []
        self.conflicts = {}
        self.provider_open = False
        self.tracking_verified = False

    def start(self):
        require(self.stage in (Stage.CHECKING, Stage.UNAVAILABLE), "Scan cannot start in this state")
        try:
            require(callable(getattr(self.provider, "close", None)), "Capture provider needs owned cleanup")
            self.provider_open = True
            binding = self.provider.open()
            require(isinstance(binding, Binding), "Provider returned no capture binding")
        except (RuntimeError, RoomError, OSError):
            self.close()
            self.stage = Stage.UNAVAILABLE
            self.message = "Live-Kamerazugriff nicht verfügbar. Der Raumscan wurde nicht gestartet."
            return False
        self.binding = binding
        if self.previous is not None:
            if (self.previous.origin_id != binding.origin_id or self.previous.test_only != binding.test_only):
                self.stage = Stage.ALIGNMENT_REQUIRED
                self.message = "Gespeicherten Raum zuerst in der aktuellen Umgebung ausrichten."
                self.close()
                return False
            # Never mix an existing room with observations from a different
            # calibration. A later explicit re-registration adapter is needed.
            if self.previous.calibration_id != binding.calibration_id:
                self.stage = Stage.ALIGNMENT_REQUIRED
                self.message = "Kamerakalibrierung geändert. Raumzuordnung erneut prüfen."
                self.close()
                return False
            self.room = replace(self.previous, partial_scan_accepted=False)
        else:
            self.room = Room(str(uuid.uuid4()), self.name, 0, binding.origin_id,
                             binding.calibration_id, test_only=binding.test_only)
        self.reconstruction = Reconstruction(binding)
        if self.previous is not None:
            self.reconstruction.retain_aligned_mesh(self.previous.mesh)
        self.tracking_verified = True
        self.stage = Stage.SCANNING
        self.message = "Schau dich langsam um und erfasse Flächen aus mehreren Blickwinkeln."
        return True

    def ingest(self, frame: DepthFrame):
        require(self.stage == Stage.SCANNING, "Scan is not collecting observations")
        try:
            count = self.reconstruction.integrate(frame)
        except RoomError as error:
            self.tracking_verified = False
            self.stage = Stage.PAUSED
            self.message = "Scan pausiert. Tracking, Kamerazuordnung oder Messdaten müssen erneut geprüft werden."
            raise error
        self.message = ("Weitere Bereiche und andere Blickwinkel erfassen." if count
                        else "Hier fehlen zuverlässige Messungen. Blickwinkel oder Beleuchtung ändern.")
        return count

    def pause(self):
        require(self.stage == Stage.SCANNING, "Scan is not running")
        self.stage, self.message = Stage.PAUSED, "Scan pausiert. Bisherige Messungen bleiben erhalten."

    def resume(self, binding: Binding, *, tracking_valid: bool):
        require(self.stage == Stage.PAUSED and type(tracking_valid) is bool and tracking_valid,
                "Valid tracking is required to resume")
        if (binding.origin_id != self.binding.origin_id or binding.calibration_id != self.binding.calibration_id
                or binding.test_only != self.binding.test_only):
            self.stage = Stage.ALIGNMENT_REQUIRED
            self.message = "Raumzuordnung geändert. Vor dem Fortsetzen neu ausrichten."
            self.close()
            return False
        if binding != self.binding:
            self.reconstruction.rebind(binding)
            self.binding = binding
        self.stage, self.message = Stage.SCANNING, "Raumscan fortsetzen"
        self.tracking_verified = True
        return True

    def _editable(self):
        require(self.stage in (Stage.SCANNING, Stage.PAUSED, Stage.REVIEW), "Room is not editable")

    def _change(self, room):
        self._history.append(self.room)
        self._history = self._history[-32:]
        self.room = replace(room, partial_scan_accepted=False)

    def put_element(self, element: Element):
        self._editable()
        self._change(self.room.put(element))
        self.conflicts.pop(element.id, None)

    def suggest_element(self, element: Element):
        self._editable()
        require(element.origin == "detected" and not element.confirmed, "Detection must remain an unconfirmed suggestion")
        existing = next((e for e in self.room.elements if e.id == element.id), None)
        if existing is not None and (existing.confirmed or existing.origin == "manual"):
            require(len(self.conflicts) < 256 or element.id in self.conflicts, "Suggestion budget reached")
            self.conflicts[element.id] = element
            return False
        self._change(self.room.put(element))
        return True

    def remove_element(self, element_id):
        self._editable()
        self._change(self.room.remove(element_id))
        self.conflicts.pop(element_id, None)

    def undo(self):
        self._editable()
        require(bool(self._history), "Nothing to undo")
        self.room = replace(self._history.pop(), partial_scan_accepted=False)
        # Existing conflicts remain until their exact element is reviewed.

    def resolve_conflict(self, element_id, *, keep_confirmed: bool):
        self._editable()
        require(type(keep_confirmed) is bool and element_id in self.conflicts, "No matching conflict")
        suggestion = self.conflicts[element_id]
        if not keep_confirmed:
            self._change(self.room.put(suggestion))
        del self.conflicts[element_id]

    def review(self):
        require(self.stage in (Stage.SCANNING, Stage.PAUSED) and self.tracking_verified,
                "Valid room alignment is required for review")
        mesh = self.reconstruction.mesh()
        require(bool(mesh.triangles), "No measured surface has been reconstructed")
        self.room = replace(self.room, mesh=mesh, partial_scan_accepted=False)
        self._history.clear()
        self.stage = Stage.REVIEW
        self.message = "Raumflächen und Elemente prüfen. Sichtbare Messlücken bleiben unbekannt."

    def continue_scan(self):
        require(self.stage == Stage.REVIEW, "Room is not in review")
        self.room = replace(self.room, partial_scan_accepted=False)
        self.stage = Stage.PAUSED
        self.message = "Tracking prüfen und fehlende Bereiche nachscannen."

    def accept_captured_area(self):
        require(self.stage == Stage.REVIEW, "Review the room before accepting it")
        self.room = replace(self.room, partial_scan_accepted=True)

    def review_issues(self):
        require(self.room is not None, "No room exists")
        issues = []
        for e in self.room.elements:
            if not e.confirmed:
                issues.append({"element_id": e.id, "reason": "confirmation_required"})
        for element_id in sorted(self.conflicts):
            issues.append({"element_id": element_id, "reason": "rescan_conflict"})
        # There is intentionally no guessed full-room completion threshold.
        if not self.room.partial_scan_accepted:
            issues.append({"element_id": None, "reason": "captured_area_acceptance_required"})
        return issues

    def save(self, store):
        require(self.stage == Stage.REVIEW and self.tracking_verified and not self.review_issues(), "Review is incomplete")
        saved = store.save(self.room, expected_revision=self.previous.revision if self.previous else 0)
        self.room, self.stage, self.message = saved, Stage.SAVED, "Raum gespeichert"
        self.close()
        return saved

    def cancel(self):
        require(self.stage not in (Stage.SAVED, Stage.CANCELLED), "Session already ended")
        self.stage, self.message = Stage.CANCELLED, "Einrichtung abgebrochen. Gespeicherter Raum bleibt erhalten."
        self.close()

    def close(self):
        """Release owned capture resources; cleanup failure is never hidden."""
        self.tracking_verified = False
        if self.provider_open:
            self.provider.close()
            self.provider_open = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def view(self):
        """Renderer-facing state; no percentage, invented geometry or fake scan."""
        mesh = (self.reconstruction.mesh() if self.reconstruction is not None
                and self.stage in (Stage.SCANNING, Stage.PAUSED) else self.room.mesh if self.room else None)
        world_visuals = self.tracking_verified and self.stage in (Stage.SCANNING, Stage.PAUSED, Stage.REVIEW)
        return {"stage": self.stage.value, "message": self.message,
                "origin_id": self.room.origin_id if self.room else None,
                "test_only": self.binding.test_only if self.binding else None,
                "can_add_element": self.stage in (Stage.SCANNING, Stage.PAUSED, Stage.REVIEW),
                "can_review": self.stage in (Stage.SCANNING, Stage.PAUSED) and world_visuals and bool(mesh and mesh.triangles),
                "can_save": self.stage == Stage.REVIEW and world_visuals and not self.review_issues(),
                "progress": self.reconstruction.status() if self.reconstruction else None,
                "world_visuals_allowed": world_visuals,
                "mesh": mesh.to_dict() if mesh and world_visuals else None,
                "elements": [e.to_dict() for e in self.room.elements] if self.room else [],
                "legend": {"building": "Im Aufbau", "reliable": "Zuverlässig erfasst", "uncertain": "Weitere Ansichten nötig"},
                "unknown_surfaces": "unobserved-no-geometry"}
