#!/usr/bin/env python3
"""
Desktop bridge for WalkPrint / YHK printers that use Classic Bluetooth RFCOMM.

This is the practical path for real device discovery and real print sends while
the Flipper app remains transport-limited.
"""

from __future__ import annotations

import argparse
import socket
import sys
from dataclasses import dataclass
from typing import Iterable

try:
    import bluetooth
except ImportError as exc:  # pragma: no cover - import error path
    raise SystemExit(
        "Missing dependency 'pybluez2'. Install with: pip install -r bridge/requirements.txt"
    ) from exc


DEFAULT_ADDRESS = "25:00:35:00:03:57"
DEFAULT_KEYWORDS = ("yhk", "walk", "printer", "thermal", "receipt", "cat")

INIT_FRAME = bytes((0x1B, 0x40))
START_PRINT_FRAME = bytes((0x1D, 0x49, 0xF0, 0x19))
END_PRINT_FRAME = bytes((0x0A, 0x0A, 0x0A, 0x0A))
FEED_FRAME = bytes((0x0A, 0x0A))
RAW_FRAME = bytes((0x1D, 0x67, 0x69))


@dataclass
class PrinterCandidate:
    name: str
    address: str


def build_test_receipt(density: int) -> bytes:
    lines = (
        "WalkPrint Bridge",
        "Classic BT RFCOMM",
        f"Density: {density}",
        "Hello from desktop bridge",
    )
    payload = "\n".join(lines).encode("utf-8") + b"\n"
    return INIT_FRAME + START_PRINT_FRAME + payload + END_PRINT_FRAME


def discover_devices(keywords: Iterable[str]) -> list[PrinterCandidate]:
    devices = bluetooth.discover_devices(duration=8, lookup_names=True, flush_cache=True)
    filtered: list[PrinterCandidate] = []
    lowered = tuple(keyword.lower() for keyword in keywords)

    for address, name in devices:
        display_name = name or "<unknown>"
        if not name:
            continue
        if any(keyword in name.lower() for keyword in lowered):
            filtered.append(PrinterCandidate(name=display_name, address=address))

    if filtered:
        return filtered

    return [
        PrinterCandidate(name=name or "<unknown>", address=address)
        for address, name in devices
        if name
    ]


def resolve_channel(address: str, preferred: int | None) -> int:
    if preferred is not None:
        return preferred

    services = bluetooth.find_service(address=address)
    for service in services:
        protocol = str(service.get("protocol", "")).upper()
        service_name = str(service.get("name", ""))
        port = service.get("port")
        if port and (protocol == "RFCOMM" or "serial" in service_name.lower()):
            return int(port)

    # YHK reverse-engineering references commonly ended up on channel 2.
    return 2


def send_frame(address: str, channel: int, payload: bytes, timeout: float) -> None:
    sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
    sock.settimeout(timeout)
    try:
        sock.connect((address, channel))
        sock.send(payload)
    finally:
        sock.close()


def cmd_scan(args: argparse.Namespace) -> int:
    candidates = discover_devices(args.keywords)
    if not candidates:
        print("No Bluetooth devices found.", file=sys.stderr)
        return 1

    for index, candidate in enumerate(candidates, start=1):
        default_marker = "  default" if candidate.address.upper() == DEFAULT_ADDRESS else ""
        print(f"[{index}] {candidate.name}  {candidate.address}{default_marker}")
    return 0


def cmd_test(args: argparse.Namespace) -> int:
    channel = resolve_channel(args.address, args.channel)
    payload = build_test_receipt(args.density)
    print(f"Sending test receipt to {args.address} on RFCOMM channel {channel}")
    send_frame(args.address, channel, payload, args.timeout)
    print("Send complete.")
    return 0


def cmd_feed(args: argparse.Namespace) -> int:
    channel = resolve_channel(args.address, args.channel)
    payload = FEED_FRAME * max(1, args.lines)
    print(f"Sending feed frame to {args.address} on RFCOMM channel {channel}")
    send_frame(args.address, channel, payload, args.timeout)
    print("Send complete.")
    return 0


def cmd_raw(args: argparse.Namespace) -> int:
    channel = resolve_channel(args.address, args.channel)
    payload = bytes.fromhex(args.hex.replace(" ", ""))
    print(f"Sending raw frame to {args.address} on RFCOMM channel {channel}")
    send_frame(args.address, channel, payload, args.timeout)
    print("Send complete.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="WalkPrint Classic Bluetooth bridge")
    subparsers = parser.add_subparsers(dest="command", required=True)

    scan = subparsers.add_parser("scan", help="Discover nearby Bluetooth printers")
    scan.add_argument(
        "--keywords",
        nargs="+",
        default=list(DEFAULT_KEYWORDS),
        help="Name keywords to filter printer candidates",
    )
    scan.set_defaults(func=cmd_scan)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--address", default=DEFAULT_ADDRESS, help="Printer MAC address")
    common.add_argument("--channel", type=int, help="RFCOMM channel override")
    common.add_argument("--timeout", type=float, default=10.0, help="Socket timeout in seconds")

    test = subparsers.add_parser("test", parents=[common], help="Send a test receipt")
    test.add_argument("--density", type=int, default=8, help="Placeholder density value")
    test.set_defaults(func=cmd_test)

    feed = subparsers.add_parser("feed", parents=[common], help="Feed paper")
    feed.add_argument("--lines", type=int, default=1, help="Feed repetitions")
    feed.set_defaults(func=cmd_feed)

    raw = subparsers.add_parser("raw", parents=[common], help="Send a raw hex frame")
    raw.add_argument("--hex", default=RAW_FRAME.hex(" "), help="Hex bytes to send")
    raw.set_defaults(func=cmd_raw)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
