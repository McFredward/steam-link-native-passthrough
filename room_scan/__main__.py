"""Developer inspection tools. This CLI is not the headset scan application."""
import argparse
import json
from pathlib import Path
import sys

from .geometry import RoomError
from .model import Label, Room
from .store import MAX_BYTES, RoomStore, decode
from .workflow import ScanSession


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status", help="Show the actual default capture availability")
    validate = commands.add_parser("validate", help="Validate a room record without importing it")
    validate.add_argument("path", type=Path)
    query = commands.add_parser("query", help="Inspect confirmed elements in an already aligned saved room")
    query.add_argument("--store", type=Path, required=True)
    query.add_argument("--room-id", required=True)
    query.add_argument("--current-origin-id", required=True)
    query.add_argument("--label", choices=[label.value for label in Label], required=True)
    args = parser.parse_args()
    try:
        if args.command == "status":
            session = ScanSession()
            session.start()
            result = session.view()
        elif args.command == "validate":
            with args.path.open("rb") as stream:
                raw = stream.read(MAX_BYTES+1)
            room = Room.from_dict(decode(raw))
            result = {"schema": "sfnp-room-validation-v1", "valid": True, "id": room.id,
                      "revision": room.revision, "elements": len(room.elements), "test_only": room.test_only,
                      "vertices": len(room.mesh.vertices), "triangles": len(room.mesh.triangles),
                      "live_alignment_verified": False}
        else:
            result = [e.to_dict() for e in RoomStore(args.store).query(args.room_id, Label(args.label),
                       current_origin_id=args.current_origin_id)]
        print(json.dumps(result, ensure_ascii=False, allow_nan=False))
        return 0
    except (RoomError, OSError, TypeError, KeyError) as error:
        print("Room inspection failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
