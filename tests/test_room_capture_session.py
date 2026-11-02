"""Manual-session boundary fixtures. No sudo, headset or boot service is used."""
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

if os.name == "posix":
    from room_scan import capture_session as session
else:
    session = None


@unittest.skipUnless(os.name == "posix", "The manual camera session is Frame/Linux only")
class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.nonce = "sfnp.room.camera."+str(os.getuid())+"."+os.urandom(16).hex()
        self.record = dict(schema=session.SCHEMA, state="armed", abstract_socket=self.nonce,
                           helper_pid=os.getpid(), helper_sha256="a"*64, owner_pid=os.getpid())

    def write_record(self):
        path = self.root/"session.json"
        path.write_text(json.dumps(self.record))
        path.chmod(0o600)
        return path

    def test_missing_session_is_inactive_and_foreign_descriptor_is_preserved(self):
        self.assertEqual({"state": "inactive"}, session.status(self.root))
        path = self.write_record()
        path.chmod(0o644)
        before = path.read_bytes()
        with self.assertRaises(ValueError):
            session.status(self.root)
        self.assertEqual(before, path.read_bytes())

    def test_descriptor_symlink_cannot_redirect_cleanup(self):
        target = self.root/"foreign.json"
        target.write_text(json.dumps(self.record))
        target.chmod(0o600)
        (self.root/"session.json").symlink_to(target)
        with self.assertRaises(OSError):
            session.stop_session(self.root)
        self.assertTrue(target.exists())

    def test_descriptor_requires_own_uid_nonce_and_exact_process_identity(self):
        path = self.write_record()
        self.record["abstract_socket"] = "sfnp.room.camera.0."+"b"*32
        path.write_text(json.dumps(self.record))
        if os.getuid() != 0:
            with self.assertRaises(ValueError):
                session.status(self.root)

    def test_actual_local_control_status_and_stop_remove_only_owned_record(self):
        path = self.write_record()
        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        server.bind("\0"+self.nonce)
        server.listen(2)
        server.settimeout(5)
        commands, errors = [], []
        def serve():
            try:
                for _ in range(2):
                    peer, _ = server.accept()
                    with peer:
                        command = json.loads(peer.recv(512))["command"]
                        commands.append(command)
                        value = dict(schema="sfnp-native-camera-api-v1", reader_uid=os.getuid(),
                            setup_privileges_dropped=True, manual_owner_monitored=True, maps_read_only=True,
                            boot_changes=False, camera_settings_changed=False, pixel_reads_active=False)
                        if command == "stop":
                            value = dict(status="stopping")
                            path.unlink()
                        raw = json.dumps(value).encode()
                        peer.send(struct.pack("<II", len(raw), 0)+raw)
            except Exception as error:
                errors.append(error)
            finally:
                server.close()
        worker = threading.Thread(target=serve)
        worker.start()
        try:
            self.assertEqual(dict(state="armed", helper_sha256="a"*64, pixel_reads_active=False), session.status(self.root))
            self.assertEqual({"state": "inactive"}, session.stop_session(self.root))
        finally:
            worker.join(6)
        self.assertFalse(worker.is_alive())
        self.assertFalse(errors)
        self.assertEqual(["describe", "stop"], commands)
        self.assertFalse(path.exists())

    def test_root_launcher_refuses_changed_image_before_execution(self):
        binary = self.root/"fixture-helper"
        binary.write_bytes(b"fixture image, never executed"*8)
        binary.chmod(0o600)
        before = binary.read_bytes()
        proc = Path("/proc")/str(os.getpid())/"stat"
        text = proc.read_text()
        started = text[text.rfind(")")+1:].split()[19]
        result = subprocess.run(["/usr/bin/python3", "-I", "-B", "-c", session.ROOT_LAUNCHER,
            str(binary), "0"*64, "--serve", str(os.getpid()), started],
            env={"SUDO_UID": str(os.getuid()), "SUDO_GID": str(os.getgid())}, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"differs from the checked package", result.stderr)
        self.assertEqual(before, binary.read_bytes())

    def test_password_enters_only_child_stdin_and_never_session_files(self):
        binary = self.root/"fixture-helper"
        binary.write_bytes(b"fixture"*32)
        binary.chmod(0o600)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        owner_source = Path(session.__file__).read_bytes()
        secret = "test-only sign-in value"
        received = bytearray()
        class Input:
            def write(self, value):
                received.extend(value)
            def close(self):
                pass
        class Process:
            stdin = Input()
            stdout = io.BytesIO(b'{"state":"armed"}\n')
            def poll(self):
                return None
        class Selector:
            def __enter__(self):
                return self
            def __exit__(self, *_):
                pass
            def register(self, *_):
                pass
            def select(self, *_):
                return [1]
        statuses = [dict(state="inactive"), dict(state="armed", helper_sha256=digest, pixel_reads_active=False)]
        with patch.object(session, "status", side_effect=statuses), patch.object(session.subprocess, "Popen", return_value=Process()) as launch, \
                patch.object(session.selectors, "DefaultSelector", Selector):
            result = session.start_session(binary, digest, secret, owner_source)
        self.assertEqual("armed", result["state"])
        self.assertEqual(secret.encode()+b"\n", bytes(received))
        args, kwargs = launch.call_args
        self.assertNotIn(secret, repr(args)+repr(kwargs))
        self.assertEqual([binary], list(self.root.iterdir()))


if __name__ == "__main__":
    unittest.main()
