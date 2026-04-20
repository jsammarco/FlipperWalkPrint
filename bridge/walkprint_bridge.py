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
from pathlib import Path
from typing import Iterable

try:
    import bluetooth
except ImportError as exc:  # pragma: no cover - import error path
    raise SystemExit(
        "Missing dependency 'pybluez2'. Install with: pip install -r bridge/requirements.txt"
    ) from exc

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - import error path
    raise SystemExit(
        "Missing dependency 'Pillow'. Install with: pip install -r bridge/requirements.txt"
    ) from exc


DEFAULT_ADDRESS = "25:00:35:00:03:57"
DEFAULT_KEYWORDS = ("yhk", "walk", "printer", "thermal", "receipt", "cat")
PRINTER_WIDTH = 384
IMAGE_DITHER_CHOICES = ("floyd-steinberg", "threshold")

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


def resize_image_for_printer(image: Image.Image, width: int) -> Image.Image:
    if width < 8:
        raise ValueError("Width must be at least 8 pixels.")

    target_width = min(width, PRINTER_WIDTH)
    source = image.convert("RGBA")
    background = Image.new("RGBA", source.size, "white")
    flattened = Image.alpha_composite(background, source).convert("L")

    if flattened.width != target_width:
        scale = target_width / flattened.width
        target_height = max(1, int(round(flattened.height * scale)))
        flattened = flattened.resize((target_width, target_height), Image.Resampling.LANCZOS)

    return flattened


def rasterize_image(
    image_path: Path,
    *,
    width: int,
    threshold: int,
    dither: str,
) -> tuple[bytes, Image.Image]:
    image = Image.open(image_path)
    prepared = resize_image_for_printer(image, width)

    if dither == "floyd-steinberg":
        mono = prepared.convert("1")
    else:
        mono = prepared.point(lambda px: 255 if px >= threshold else 0).convert("1")

    width_bytes = (mono.width + 7) // 8
    raster = bytearray(width_bytes * mono.height)

    for y in range(mono.height):
        for x in range(mono.width):
            if mono.getpixel((x, y)) == 0:
                byte_index = y * width_bytes + (x // 8)
                raster[byte_index] |= 1 << (7 - (x % 8))

    header = bytes(
        (
            0x1D,
            0x76,
            0x30,
            0x00,
            width_bytes & 0xFF,
            (width_bytes >> 8) & 0xFF,
            mono.height & 0xFF,
            (mono.height >> 8) & 0xFF,
        )
    )
    return header + bytes(raster), mono


def build_image_print_job(
    image_path: Path,
    *,
    width: int,
    threshold: int,
    dither: str,
    feed_lines: int,
) -> tuple[bytes, Image.Image]:
    raster_payload, mono = rasterize_image(
        image_path, width=width, threshold=threshold, dither=dither
    )
    payload = INIT_FRAME + START_PRINT_FRAME + raster_payload + END_PRINT_FRAME
    if feed_lines > 0:
        payload += FEED_FRAME * feed_lines
    return payload, mono


def save_preview_image(image: Image.Image, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


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


def cmd_convert_image(args: argparse.Namespace) -> int:
    payload, mono = build_image_print_job(
        Path(args.image),
        width=args.width,
        threshold=args.threshold,
        dither=args.dither,
        feed_lines=args.feed_lines,
    )

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    print(
        f"Wrote {len(payload)} bytes for {mono.width}x{mono.height} image print job to {output_path}"
    )

    if args.preview:
        preview_path = Path(args.preview)
        save_preview_image(mono, preview_path)
        print(f"Saved monochrome preview to {preview_path}")

    return 0


def cmd_image(args: argparse.Namespace) -> int:
    channel = resolve_channel(args.address, args.channel)
    payload, mono = build_image_print_job(
        Path(args.image),
        width=args.width,
        threshold=args.threshold,
        dither=args.dither,
        feed_lines=args.feed_lines,
    )

    if args.preview:
        preview_path = Path(args.preview)
        save_preview_image(mono, preview_path)
        print(f"Saved monochrome preview to {preview_path}")

    print(
        f"Sending {mono.width}x{mono.height} image to {args.address} on RFCOMM channel {channel}"
    )
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

    image_common = argparse.ArgumentParser(add_help=False)
    image_common.add_argument("image", help="Path to the image file to print")
    image_common.add_argument(
        "--width",
        type=int,
        default=PRINTER_WIDTH,
        help=f"Target print width in pixels (max {PRINTER_WIDTH})",
    )
    image_common.add_argument(
        "--threshold",
        type=int,
        default=160,
        help="Threshold for black/white conversion when not dithering",
    )
    image_common.add_argument(
        "--dither",
        choices=IMAGE_DITHER_CHOICES,
        default="floyd-steinberg",
        help="Monochrome conversion mode",
    )
    image_common.add_argument(
        "--feed-lines",
        type=int,
        default=3,
        help="Extra blank feed lines after the image prints",
    )
    image_common.add_argument(
        "--preview",
        help="Optional path to save the monochrome preview image",
    )

    test = subparsers.add_parser("test", parents=[common], help="Send a test receipt")
    test.add_argument("--density", type=int, default=8, help="Placeholder density value")
    test.set_defaults(func=cmd_test)

    feed = subparsers.add_parser("feed", parents=[common], help="Feed paper")
    feed.add_argument("--lines", type=int, default=1, help="Feed repetitions")
    feed.set_defaults(func=cmd_feed)

    raw = subparsers.add_parser("raw", parents=[common], help="Send a raw hex frame")
    raw.add_argument("--hex", default=RAW_FRAME.hex(" "), help="Hex bytes to send")
    raw.set_defaults(func=cmd_raw)

    convert_image = subparsers.add_parser(
        "convert-image",
        parents=[image_common],
        help="Convert an image into printer-ready bytes without sending it",
    )
    convert_image.add_argument(
        "--output",
        default="bridge/out/image_print_job.bin",
        help="Output file for the printer-ready payload",
    )
    convert_image.set_defaults(func=cmd_convert_image)

    image = subparsers.add_parser(
        "image",
        parents=[common, image_common],
        help="Convert an image to printer raster bytes and send it",
    )
    image.set_defaults(func=cmd_image)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
