#!/usr/bin/env python3
"""
Listen for UDP log stream from the Greenwood Clock and print to stdout.

Endpoint: POST /debug/udp_log  {"host": "<your-ip>", "port": <port>}
Usage:    python tools/udp_log_listen.py [--host greenwood-clock.local] [--port 5555]
"""

import socket
import sys
import argparse


def get_local_ip() -> str:
    """Return the machine's outbound IPv4 address (best guess)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def register_with_device(device_host: str, my_ip: str, port: int) -> None:
    import urllib.request
    import json

    url = f"http://{device_host}/debug/udp_log"
    body = json.dumps({"host": my_ip, "port": port}).encode()
    req = urllib.request.Request(
        url, data=body, headers={"Content-Type": "application/json"}, method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            print(
                f"Registered with device: HTTP {resp.status} {resp.read().decode().strip()}",
                file=sys.stderr,
            )
    except Exception as exc:
        print(f"Registration failed: {exc}", file=sys.stderr)
        print(
            "Listening anyway — manually POST /debug/udp_log if needed.",
            file=sys.stderr,
        )


def main() -> None:
    ap = argparse.ArgumentParser(description="Receive live UDP log stream from device.")
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    ap.add_argument(
        "--port", type=int, default=5555, help="UDP port to listen on (default: 5555)"
    )
    ap.add_argument(
        "--no-register",
        action="store_true",
        help="Skip auto-registering with the device",
    )
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", args.port))

    my_ip = get_local_ip()
    print(f"Local IP: {my_ip}", file=sys.stderr)

    if not args.no_register:
        register_with_device(args.host, my_ip, args.port)

    print(f"Listening on UDP :{args.port} ...", file=sys.stderr)
    print("-" * 60, file=sys.stderr)

    while True:
        try:
            data, addr = sock.recvfrom(2048)
            print(data.decode(errors="replace"), end="", flush=True)
        except KeyboardInterrupt:
            print("\nStopped.", file=sys.stderr)
            break


if __name__ == "__main__":
    main()
