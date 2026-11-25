"""Loopback-free protocol fixtures. No network endpoint or printer required."""

import concurrent.futures
import subprocess
import tempfile
from pathlib import Path
import json
import os
import secrets
import socket
import struct
import unittest
from unittest.mock import MagicMock, patch

import link


class ProtocolTest(unittest.TestCase):
    def test_status_and_commands(self):
        client, broker = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(broker.close)
        serial = secrets.token_hex(6)
        report = link.field("device/" + serial + "/report")
        status = {"gcode_state": "RUNNING", "mc_percent": 12, "subtask_id": "test-task"}
        broker.sendall(link.packet(32, bytes(2)) + link.packet(144, bytes([0, 1, 0]))
                       + link.packet(48, report + json.dumps({"print": status}).encode()))
        read_fd, write_fd = os.pipe()
        transport = MagicMock(wraps=client)
        transport.pending = lambda: 0
        broker.settimeout(2)
        with os.fdopen(read_fd) as reader, patch.object(link.sys, "stdin", reader), patch.object(link, "emit") as events:
            with concurrent.futures.ThreadPoolExecutor() as pool:
                monitor = pool.submit(link.monitor, transport, {"serial": serial, "code": secrets.token_hex(4)})
                with os.fdopen(write_fd, "wb", buffering=0) as writer:
                    writer.write(b"resume\npause\nstop\n")
                    self.assertEqual([link.receive(broker)[0] for _ in range(2)], [16, 130])
                    for group, expected in [("pushing", "pushall"), ("print", "pause"), ("print", "stop")]:
                        with self.subTest(command=expected):
                            kind, body = link.receive(broker)
                            topic = link.field("device/" + serial + "/request")
                            self.assertEqual((kind, body[:len(topic)]), (48, topic))
                            self.assertEqual(json.loads(body[len(topic):])[group]["command"], expected)
                monitor.result(timeout=2)
        events.assert_any_call(status=status)
        events.assert_any_call(message="設備狀態已改變或過期，請重新核對")

    def test_only_explicit_print_state_renews_control(self):
        serial = secrets.token_hex(6)
        report = link.field("device/" + serial + "/report")
        client, broker = socket.socketpair()
        read_fd, write_fd = os.pipe()
        # Each tick delivers one MQTT delta with one simultaneous UI command.
        ticks = iter([
            (100, {"gcode_state": "RUNNING"}, b""),
            (141, {"nozzle_temper": 210}, b"stop\n"),
            (142, {"gcode_state": "RUNNING"}, b"pause\n"),
            (172, {"mc_percent": 48}, b"stop\n"),
            (173, {"gcode_state": "RUNNING"}, b""),
            (174, {"gcode_state": None, "subtask_id": None}, b"stop\n"),
            (175, {"gcode_state": "PAUSE"}, b"stop\n"),
        ])
        clock = 100
        with client, broker, os.fdopen(read_fd) as reader, os.fdopen(write_fd, "wb", buffering=0) as writer:
            def tick(*args):
                nonlocal clock
                sample = next(ticks, None)
                if sample is None:
                    writer.close()
                    return [reader], [], []
                clock, delta, command = sample
                broker.sendall(link.packet(48, report + json.dumps({"print": delta}).encode()))
                if command:
                    writer.write(command)
                return [transport, reader] if command else [transport], [], []

            transport = MagicMock(wraps=client)
            transport.pending = lambda: 0
            broker.sendall(link.packet(32, bytes(2)) + link.packet(144, bytes([0, 1, 0])))
            with patch.object(link.sys, "stdin", reader), patch.object(link.select, "select", tick), \
                    patch.object(link.time, "monotonic", lambda: clock), patch.object(link, "emit") as events:
                link.monitor(transport, {"serial": serial, "code": secrets.token_hex(4)})
            client.shutdown(socket.SHUT_WR)
            publishes = []
            topic = link.field("device/" + serial + "/request")
            while broker.recv(1, socket.MSG_PEEK):
                kind, body = link.receive(broker)
                if kind == 48:
                    payload = json.loads(body[len(topic):])
                    if "print" in payload:
                        publishes.append(payload["print"]["command"])
            self.assertEqual(publishes, ["pause", "stop"])
            events.assert_any_call(status={"gcode_state": "—", "subtask_id": ""})
            messages = [call.kwargs["message"] for call in events.call_args_list if "message" in call.kwargs]
            self.assertEqual([msg.startswith("指令已送出") for msg in messages], [False, True, False, False, True])

    def test_camera_frames(self):
        client, camera = socket.socketpair()
        with client, camera:
            from base64 import b64decode
            jpeg = b"\xff\xd8demo\xff\xd9"
            camera.sendall(struct.pack("<IIII", len(jpeg), 0, 0, 0) + jpeg)
            camera.shutdown(socket.SHUT_WR)
            with patch.object(link, "emit") as event, self.assertRaises(EOFError):
                link.camera(client, {"code": secrets.token_hex(4)})
            self.assertEqual(b64decode(event.call_args.kwargs["frame"]), jpeg)
            self.assertEqual(len(link.read(camera, 80)), 80)

    def test_tls_handshake_and_pin(self):
        with tempfile.TemporaryDirectory() as folder:
            cert = str(Path(folder) / "cert.pem")
            key = str(Path(folder) / "key.pem")
            subprocess.run(["/usr/bin/openssl", "req", "-x509", "-newkey", "rsa:2048",
                            "-nodes", "-days", "1", "-subj", "/CN=fixture", "-out", cert,
                            "-keyout", key], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            der = link.ssl.PEM_cert_to_DER_cert(Path(cert).read_text())
            config = {"host": str(link.ipaddress.ip_network("10.0.0.0/8")[1]),
                      "serial": secrets.token_hex(4), "code": secrets.token_hex(4),
                      "pin": link.hashlib.sha256(der).hexdigest()}
            ctx = link.ssl.SSLContext(link.ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(cert, key)
            for mismatch in (False, True):
                a, b = socket.socketpair()
                a.settimeout(3)
                b.settimeout(3)
                def peer():
                    with ctx.wrap_socket(b, server_side=True) as conn:
                        return conn.recv(16)
                with concurrent.futures.ThreadPoolExecutor() as pool, patch.object(socket, "create_connection", return_value=a):
                    result = pool.submit(peer)
                    if mismatch:
                        config["pin"] = secrets.token_hex(32)
                        with self.assertRaises(PermissionError):
                            link.connect(config, 8883)
                        self.assertEqual(result.result(), b"")
                    else:
                        with link.connect(config, 8883) as conn:
                            conn.sendall(b"fixture")
                        self.assertEqual(result.result(), b"fixture")

    def test_pin_mismatch_closes_before_credentials(self):
        cfg = {"host": str(link.ipaddress.ip_network("10.0.0.0/8")[1]),
               "serial": secrets.token_hex(4), "code": secrets.token_hex(4), "pin": secrets.token_hex(32)}
        with patch.object(socket, "create_connection"), patch.object(link.ssl, "SSLContext") as context:
            peer = context.return_value.wrap_socket.return_value
            peer.getpeercert.return_value = secrets.token_bytes(64)
            with self.assertRaises(PermissionError):
                link.connect(cfg, 8883)
            peer.close.assert_called_once()
            peer.sendall.assert_not_called()


if __name__ == "__main__":
    unittest.main()
