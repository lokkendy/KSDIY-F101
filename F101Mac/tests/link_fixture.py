"""Network-free child process used by the native UI acceptance fixture."""

import importlib.util
import json
import os
from pathlib import Path
import socket
import sys
from threading import Thread


repo_link = Path(__file__).with_name("protocol.py")
spec = importlib.util.spec_from_file_location("f101_protocol", repo_link)
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


class SocketPairTransport:
    def __init__(self, sock):
        self.sock = sock

    def pending(self):
        return 0

    def __getattr__(self, name):
        return getattr(self.sock, name)


def broker(peer, config):
    assert protocol.receive(peer)[0] == 16
    peer.sendall(protocol.packet(32, bytes(2)))
    assert protocol.receive(peer)[0] == 130
    peer.sendall(protocol.packet(144, bytes([0, 1, 0])))
    assert protocol.receive(peer)[0] == 48
    topic = protocol.field("device/" + config["serial"] + "/report")
    report = {"print": {"gcode_state": "RUNNING", "mc_percent": 42, "subtask_name": "測試任務.3mf"}}
    peer.sendall(protocol.packet(48, topic + json.dumps(report).encode()))
    while True:
        kind, _ = protocol.receive(peer)
        if kind == 192:
            peer.sendall(protocol.packet(208, b""))
        else:
            raise AssertionError(f"unexpected MQTT packet in read-only UI fixture: {kind}")


if __name__ == "__main__":
    with os.fdopen(sys.stdin.fileno(), "rb", buffering=0, closefd=False) as startup:
        json.loads(startup.readline())
    config = {"serial": "fixture", "code": "fixture"}
    client, peer = socket.socketpair()
    with client, peer:
        if sys.argv[1] == "monitor":
            Thread(target=broker, args=(peer, config), daemon=True).start()
            protocol.monitor(SocketPairTransport(client), config)
        else:
            protocol.camera(client, config)
