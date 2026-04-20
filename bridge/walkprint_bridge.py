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
    from PIL import Image
except ImportError as exc:  # pragma: no cover - import error path
    raise SystemExit(
        "Missing dependency 'Pillow'. Install with: pip install -r bridge/requirements.txt"
    ) from exc

DEFAULT_ADDRESS = "25:00:35:00:03:57"
DEFAULT_KEYWORDS = ("yhk", "walk", "printer", "thermal", "receipt", "cat")
PRINTER_WIDTH = 384
IMAGE_DITHER_CHOICES = ("floyd-steinberg", "threshold")
BARCODE_FORMAT_CHOICES = ("code128", "code39", "ean8", "ean13", "upca")

INIT_FRAME = bytes((0x1B, 0x40))
START_PRINT_FRAME = bytes((0x1D, 0x49, 0xF0, 0x19))
END_PRINT_FRAME = bytes((0x0A, 0x0A, 0x0A, 0x0A))
FEED_FRAME = bytes((0x0A, 0x0A))
RAW_FRAME = bytes((0x1D, 0x67, 0x69))


@dataclass
class PrinterCandidate:
    name: str
    address: str


def require_bluetooth_module():
    try:
        import bluetooth
    except ImportError as exc:  # pragma: no cover - import error path
        raise SystemExit(
            "Missing dependency 'pybluez2'. Install with: pip install -r bridge/requirements.txt"
        ) from exc

    return bluetooth


def require_qrcode_module():
    try:
        import qrcode
        from qrcode.constants import ERROR_CORRECT_M
    except ImportError as exc:  # pragma: no cover - import error path
        raise SystemExit(
            "Missing dependency 'qrcode'. Install with: pip install -r bridge/requirements.txt"
        ) from exc

    return qrcode, ERROR_CORRECT_M


def require_barcode_module():
    try:
        import barcode
        from barcode.writer import ImageWriter
    except ImportError as exc:  # pragma: no cover - import error path
        raise SystemExit(
            "Missing dependency 'python-barcode'. Install with: pip install -r bridge/requirements.txt"
        ) from exc

    return barcode, ImageWriter


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
    bluetooth = require_bluetooth_module()
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
    bluetooth = require_bluetooth_module()
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
    bluetooth = require_bluetooth_module()
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


def fit_image_to_width(
    image: Image.Image,
    *,
    width: int,
    background: int = 255,
) -> Image.Image:
    if width < 8:
        raise ValueError("Width must be at least 8 pixels.")

    source = image.convert("L")
    canvas = Image.new("L", (width, source.height), color=background)

    if source.width > width:
        scale = width / source.width
        target_height = max(1, int(round(source.height * scale)))
        source = source.resize((width, target_height), Image.Resampling.LANCZOS)
        canvas = Image.new("L", (width, source.height), color=background)

    x_offset = max(0, (width - source.width) // 2)
    canvas.paste(source, (x_offset, 0))
    return canvas


def save_bmp_image(image: Image.Image, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("L").save(output_path, format="BMP")


def load_content(text: str | None, input_file: str | None) -> str:
    if bool(text) == bool(input_file):
        raise ValueError("Provide exactly one of --text or --input-file.")

    if text is not None:
        content = text
    else:
        content = Path(input_file).read_text(encoding="utf-8")

    content = content.strip()
    if not content:
        raise ValueError("Barcode/QR content cannot be empty.")

    return content


def generate_qr_image(
    content: str,
    *,
    width: int,
    box_size: int,
    border: int,
) -> Image.Image:
    qrcode, error_correct_m = require_qrcode_module()
    qr = qrcode.QRCode(
        version=None,
        error_correction=error_correct_m,
        box_size=box_size,
        border=border,
    )
    qr.add_data(content)
    qr.make(fit=True)

    qr_image = qr.make_image(fill_color="black", back_color="white").convert("L")
    if qr_image.width > width:
        qr_image.thumbnail((width, width), Image.Resampling.NEAREST)

    return fit_image_to_width(qr_image, width=width)


def generate_barcode_image(
    content: str,
    *,
    barcode_format: str,
    width: int,
    module_width: float,
    module_height: float,
    quiet_zone: float,
    show_text: bool,
    font_size: int,
) -> Image.Image:
    barcode, image_writer = require_barcode_module()
    writer = image_writer()
    try:
        barcode_class = barcode.get_barcode_class(barcode_format)
        barcode_image = barcode_class(content, writer=writer).render(
            writer_options={
                "module_width": module_width,
                "module_height": module_height,
                "quiet_zone": quiet_zone,
                "font_size": font_size if show_text else 1,
                "text_distance": 4,
                "write_text": show_text,
                "background": "white",
                "foreground": "black",
                "dpi": 300,
            }
        )
    except Exception as exc:
        raise ValueError(f"{barcode_format.upper()} barcode error: {exc}") from exc

    return fit_image_to_width(barcode_image, width=width)


def rasterize_prepared_image(
    image: Image.Image,
    *,
    threshold: int,
    dither: str,
) -> tuple[bytes, Image.Image]:
    prepared = image.convert("L")

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


def rasterize_image(
    image_path: Path,
    *,
    width: int,
    threshold: int,
    dither: str,
) -> tuple[bytes, Image.Image]:
    image = Image.open(image_path)
    prepared = resize_image_for_printer(image, width)
    return rasterize_prepared_image(prepared, threshold=threshold, dither=dither)


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


def build_generated_image_print_job(
    image: Image.Image,
    *,
    threshold: int,
    dither: str,
    feed_lines: int,
) -> tuple[bytes, Image.Image]:
    raster_payload, mono = rasterize_prepared_image(image, threshold=threshold, dither=dither)
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


def cmd_make_qr(args: argparse.Namespace) -> int:
    content = load_content(args.text, args.input_file)
    image = generate_qr_image(
        content,
        width=args.width,
        box_size=args.box_size,
        border=args.border,
    )
    output_path = Path(args.output)
    save_bmp_image(image, output_path)
    print(f"Wrote {image.width}x{image.height} QR BMP to {output_path}")
    return 0


def cmd_qr(args: argparse.Namespace) -> int:
    content = load_content(args.text, args.input_file)
    image = generate_qr_image(
        content,
        width=args.width,
        box_size=args.box_size,
        border=args.border,
    )
    payload, mono = build_generated_image_print_job(
        image,
        threshold=args.threshold,
        dither=args.dither,
        feed_lines=args.feed_lines,
    )

    if args.output:
        output_path = Path(args.output)
        save_bmp_image(image, output_path)
        print(f"Saved {image.width}x{image.height} QR BMP to {output_path}")

    if args.preview:
        preview_path = Path(args.preview)
        save_preview_image(mono, preview_path)
        print(f"Saved monochrome preview to {preview_path}")

    channel = resolve_channel(args.address, args.channel)
    print(f"Sending QR code to {args.address} on RFCOMM channel {channel}")
    send_frame(args.address, channel, payload, args.timeout)
    print("Send complete.")
    return 0


def cmd_make_barcode(args: argparse.Namespace) -> int:
    content = load_content(args.text, args.input_file)
    image = generate_barcode_image(
        content,
        barcode_format=args.format,
        width=args.width,
        module_width=args.module_width,
        module_height=args.module_height,
        quiet_zone=args.quiet_zone,
        show_text=not args.no_text,
        font_size=args.font_size,
    )
    output_path = Path(args.output)
    save_bmp_image(image, output_path)
    print(f"Wrote {image.width}x{image.height} {args.format.upper()} BMP to {output_path}")
    return 0


def cmd_barcode(args: argparse.Namespace) -> int:
    content = load_content(args.text, args.input_file)
    image = generate_barcode_image(
        content,
        barcode_format=args.format,
        width=args.width,
        module_width=args.module_width,
        module_height=args.module_height,
        quiet_zone=args.quiet_zone,
        show_text=not args.no_text,
        font_size=args.font_size,
    )
    payload, mono = build_generated_image_print_job(
        image,
        threshold=args.threshold,
        dither=args.dither,
        feed_lines=args.feed_lines,
    )

    if args.output:
        output_path = Path(args.output)
        save_bmp_image(image, output_path)
        print(f"Saved {image.width}x{image.height} {args.format.upper()} BMP to {output_path}")

    if args.preview:
        preview_path = Path(args.preview)
        save_preview_image(mono, preview_path)
        print(f"Saved monochrome preview to {preview_path}")

    channel = resolve_channel(args.address, args.channel)
    print(f"Sending {args.format.upper()} barcode to {args.address} on RFCOMM channel {channel}")
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

    raster_common = argparse.ArgumentParser(add_help=False)
    raster_common.add_argument(
        "--threshold",
        type=int,
        default=160,
        help="Threshold for black/white conversion when not dithering",
    )
    raster_common.add_argument(
        "--dither",
        choices=IMAGE_DITHER_CHOICES,
        default="floyd-steinberg",
        help="Monochrome conversion mode",
    )
    raster_common.add_argument(
        "--feed-lines",
        type=int,
        default=3,
        help="Extra blank feed lines after the image prints",
    )
    raster_common.add_argument(
        "--preview",
        help="Optional path to save the monochrome preview image",
    )

    image_common = argparse.ArgumentParser(add_help=False)
    image_common.add_argument("image", help="Path to the image file to print")
    image_common.add_argument(
        "--width",
        type=int,
        default=PRINTER_WIDTH,
        help=f"Target print width in pixels (max {PRINTER_WIDTH})",
    )

    code_input = argparse.ArgumentParser(add_help=False)
    code_input.add_argument("--text", help="Inline text to encode")
    code_input.add_argument(
        "--input-file",
        help="Path to a UTF-8 text file whose contents should be encoded",
    )

    qr_common = argparse.ArgumentParser(add_help=False)
    qr_common.add_argument(
        "--width",
        type=int,
        default=PRINTER_WIDTH,
        help=f"Target BMP width in pixels (default {PRINTER_WIDTH})",
    )
    qr_common.add_argument("--box-size", type=int, default=8, help="QR module size in pixels")
    qr_common.add_argument("--border", type=int, default=2, help="QR quiet-zone size in modules")

    barcode_common = argparse.ArgumentParser(add_help=False)
    barcode_common.add_argument(
        "--format",
        choices=BARCODE_FORMAT_CHOICES,
        default="code128",
        help="1D barcode symbology",
    )
    barcode_common.add_argument(
        "--width",
        type=int,
        default=PRINTER_WIDTH,
        help=f"Target BMP width in pixels (default {PRINTER_WIDTH})",
    )
    barcode_common.add_argument(
        "--module-width",
        type=float,
        default=0.2,
        help="Barcode bar width in millimeters at 300 DPI",
    )
    barcode_common.add_argument(
        "--module-height",
        type=float,
        default=18.0,
        help="Barcode bar height in millimeters at 300 DPI",
    )
    barcode_common.add_argument(
        "--quiet-zone",
        type=float,
        default=2.0,
        help="Barcode quiet-zone width in millimeters at 300 DPI",
    )
    barcode_common.add_argument(
        "--font-size",
        type=int,
        default=10,
        help="Human-readable text size",
    )
    barcode_common.add_argument(
        "--no-text",
        action="store_true",
        help="Omit the human-readable line under the bars",
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
        parents=[image_common, raster_common],
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
        parents=[common, image_common, raster_common],
        help="Convert an image to printer raster bytes and send it",
    )
    image.set_defaults(func=cmd_image)

    make_qr = subparsers.add_parser(
        "make-qr",
        parents=[code_input, qr_common],
        help="Generate a 384px BMP QR code for Flipper-side BMP printing",
    )
    make_qr.add_argument(
        "--output",
        default="bridge/out/qr.bmp",
        help="Output BMP path",
    )
    make_qr.set_defaults(func=cmd_make_qr)

    qr = subparsers.add_parser(
        "qr",
        parents=[common, raster_common, code_input, qr_common],
        help="Generate a QR code and send it directly to the printer",
    )
    qr.add_argument(
        "--output",
        help="Optional BMP path to also save for Flipper-side printing",
    )
    qr.set_defaults(func=cmd_qr)

    make_barcode = subparsers.add_parser(
        "make-barcode",
        parents=[code_input, barcode_common],
        help="Generate a 384px BMP 1D barcode for Flipper-side BMP printing",
    )
    make_barcode.add_argument(
        "--output",
        default="bridge/out/barcode.bmp",
        help="Output BMP path",
    )
    make_barcode.set_defaults(func=cmd_make_barcode)

    barcode_cmd = subparsers.add_parser(
        "barcode",
        parents=[common, raster_common, code_input, barcode_common],
        help="Generate a 1D barcode and send it directly to the printer",
    )
    barcode_cmd.add_argument(
        "--output",
        help="Optional BMP path to also save for Flipper-side printing",
    )
    barcode_cmd.set_defaults(func=cmd_barcode)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
