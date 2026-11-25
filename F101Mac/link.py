"""Private child of the menu app. One TLS session per process."""
import base64
import hashlib
import hmac
import ipaddress
import json
import os
import re
import secrets
import select
import socket
import ssl
import struct
import sys
import time

LIMIT = 2 * 1024 * 1024


def emit(**event):
    print(json.dumps(event, separators=(",", ":")), flush=True)


def connect(config, port):
    address = ipaddress.IPv4Address(config["host"])
    networks = ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")
    if not any(address in ipaddress.ip_network(net) for net in networks):
        raise ValueError()
    if not re.fullmatch(r"[A-Za-z0-9]{1,32}", config["serial"]):
        raise ValueError()
    pin = bytes.fromhex(config["pin"].replace(":", ""))
    if len(pin) != 32 or not 1 <= len(config["code"].encode()) <= 31:
        raise ValueError()
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE  # Exact trusted leaf DER hash checked before auth.
    sock = ctx.wrap_socket(socket.create_connection((str(address), port), 6))
    return verify_peer(sock, pin)


def verify_peer(sock, pin):
    if not hmac.compare_digest(hashlib.sha256(sock.getpeercert(True)).digest(), pin):
        sock.close()
        raise PermissionError()
    return sock


def read(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError()
        data.extend(chunk)
    return bytes(data)


def field(value):
    raw = value.encode()
    return struct.pack(">H", len(raw)) + raw


def packet(kind, body):
    length = len(body)
    prefix = bytearray([kind])
    while True:
        length, low = divmod(length, 128)
        prefix.append(low | (128 if length else 0))
        if not length:
            return bytes(prefix) + body


def receive(sock):
    kind = read(sock, 1)[0]
    size = 0
    for power in range(4):
        digit = read(sock, 1)[0]
        size += (digit & 127) << (7 * power)
        if size > LIMIT:
            raise ValueError()
        if digit < 128:
            return kind, read(sock, size)
    raise ValueError()


def monitor(sock, config):
    root = "device/" + config["serial"]
    report = root + "/report"
    login = field("MQTT") + bytes([4, 194, 0, 30]) + field("f101-" + secrets.token_hex(6))
    sock.sendall(packet(16, login + field("bblp") + field(config["code"])))
    if receive(sock) != (32, bytes(2)):
        raise PermissionError()
    sock.sendall(packet(130, bytes([0, 1]) + field(report) + bytes(1)))
    if receive(sock) != (144, bytes([0, 1, 0])):
        raise PermissionError()

    def publish(group, command):
        data = {group: {"sequence_id": str(time.time_ns() % 2147483647), "command": command}}
        sock.sendall(packet(48, field(root + "/request") + json.dumps(data).encode()))

    publish("pushing", "pushall")
    started = last_ping = last_packet = time.monotonic()
    last_telemetry = started
    last_state = None
    state = ""
    pending_input = b""
    while True:
        ready, _, _ = select.select([sock, sys.stdin], [], [], 1) if not sock.pending() else ([sock], [], [])
        if sock in ready:
            kind, body = receive(sock)
            last_packet = time.monotonic()
            if kind == 48 and body.startswith(field(report)):
                status = json.loads(body[len(field(report)):]).get("print")
                if isinstance(status, dict):
                    keys = ("gcode_state", "subtask_name", "gcode_file", "mc_percent", "mc_remaining_time",
                            "nozzle_temper", "bed_temper", "chamber_temper", "nozzle_target_temper", "bed_target_temper")
                    values = {k: status[k] for k in keys if isinstance(status.get(k), (str, int, float))}
                    # Omitted fields are unchanged for display. Only an explicit state
                    # report can renew command eligibility; invalid values revoke it.
                    if "gcode_state" in status:
                        raw_state = status["gcode_state"]
                        state = raw_state if isinstance(raw_state, str) else ""
                        last_state = last_packet
                        values["gcode_state"] = state or "—"
                    if values:
                        last_telemetry = last_packet
                        emit(status=values)
        if sys.stdin in ready:
            chunk = os.read(sys.stdin.fileno(), 4096)
            if not chunk:
                return
            lines = (pending_input + chunk).split(b"\n")
            pending_input = lines.pop()
            if len(pending_input) > 4096:
                raise ValueError()
            for line in lines:
                command = line.strip().decode("ascii")
                fresh = last_state is not None and time.monotonic() - last_state < 30
                allowed = (command == "pause" and state == "RUNNING") or (command == "stop" and state in ("RUNNING", "PAUSE"))
                if fresh and allowed:
                    publish("print", command)
                    emit(message="指令已送出，等待設備回報")
                else:
                    emit(message="設備狀態已改變或過期，請重新核對")
        now = time.monotonic()
        if now - last_packet > 40 or now - last_telemetry > 60:
            raise TimeoutError()
        if now - last_ping > 12:
            sock.sendall(packet(192, b""))
            last_ping = now


def camera(sock, config):
    sock.sendall(struct.pack("<IIII32s32s", 64, 0x3000, 0, 0, b"bblp", config["code"].encode()))
    while True:
        size = struct.unpack("<I", read(sock, 16)[:4])[0]
        if not 0 < size <= LIMIT:
            raise ValueError()
        frame = read(sock, size)
        if frame.startswith(b"\xff\xd8") and frame.endswith(b"\xff\xd9"):
            emit(frame=base64.b64encode(frame).decode())


if __name__ == "__main__":
    try:
        # Read only the configuration line; subsequent bytes belong to the select loop.
        with os.fdopen(sys.stdin.fileno(), "rb", buffering=0, closefd=False) as startup:
            config = json.loads(startup.readline())
        video = sys.argv[1] == "camera"
        with connect(config, 6000 if video else 8883) as connection:
            (camera if video else monitor)(connection, config)
    except PermissionError:
        emit(error="身分驗證失敗，請核對憑證指紋與存取碼")
    except Exception:
        emit(error="連線中斷，請核對區網設定及機型，再重新連線")
