# WalkPrint

WalkPrint is a Flipper Zero external app for a WalkPrint / YHK-style thermal printer that can print typed messages, `.txt` files from the SD card, and `384px` wide BMP images. It uses a single live path through an external ESP32 bridge instead of pretending the Flipper can talk to a Classic Bluetooth printer on its own.

| Features | Features |
| --- | --- |
| Print custom typed messages | Print `.txt` files from the SD card |
| Print `384px` BMP images from the SD card | Discover nearby supported printers |
| Generate QR BMPs on-device and save/print them | Generate Code 128 barcode BMPs on-device and save/print them |
| Connect through the ESP32 Classic Bluetooth bridge | Scan Wi-Fi from the bridge |
| Adjust density, font size, spacing, and font family | Save printer address and print settings |

The app talks to an ESP32-WROOM over Flipper USART at `115200 8N1`, and the ESP32 handles:

- Classic Bluetooth SPP printer discovery and connection
- raw printer byte transport
- Wi-Fi scan/status helpers

The build scripts prefer your local Momentum firmware checkout when it is present.

## Current State

- The Flipper app builds cleanly against Momentum and outputs [dist/walkprint.fap](/C:/Users/jasammarco.ENG/Projects/WalkPrint/dist/walkprint.fap).
- The app UI now includes printer discovery, Wi-Fi scan, configurable message font size/family settings, `.txt` printing from the SD card, and BMP printing from the SD card.
- The live transport in [walkprint_transport_live.c](https://github.com/jsammarco/FlipperWalkPrint/blob/main/walkprint_transport_live.c) uses Flipper header pins `13/14` (`PB6/PB7`) at `115200`.
- The ESP32 bridge sketch lives at [esp32_bridge/walkprint_esp32_bridge/walkprint_esp32_bridge.ino](https://github.com/jsammarco/FlipperWalkPrint/blob/main/esp32_bridge/walkprint_esp32_bridge/walkprint_esp32_bridge.ino).
- The optional WalkPrint Bridge is a PC-side Python script under `bridge/` for desktop helpers like image conversion, QR/1D barcode BMP generation, and direct PC printing. It is not required for the Flipper Zero app.

## ESP32 Bridge

The ESP32 bridge is the practical transport layer for this setup because it can own Classic Bluetooth SPP while the Flipper app stays on a supported UART link.

## Hardware Photos

<table>
  <tr>
    <td><img src="https://raw.githubusercontent.com/jsammarco/FlipperWalkPrint/refs/heads/main/Images/flipper%20module%20printer2.jpg" alt="Flipper module printer 2" width="320"></td>
    <td><img src="https://raw.githubusercontent.com/jsammarco/FlipperWalkPrint/refs/heads/main/Images/flipper%20module%20printer.jpg" alt="Flipper module printer" width="320"></td>
  </tr>
  <tr>
    <td><img src="https://raw.githubusercontent.com/jsammarco/FlipperWalkPrint/refs/heads/main/Images/Inland%20ESP-32.JPG" alt="Inland ESP-32" width="320"></td>
    <td><img src="https://raw.githubusercontent.com/jsammarco/FlipperWalkPrint/refs/heads/main/Images/flipper%20module%20back.jpg" alt="Flipper module back" width="320"></td>
  </tr>
</table>

Files:

- Bridge sketch: [esp32_bridge/walkprint_esp32_bridge/walkprint_esp32_bridge.ino](https://github.com/jsammarco/FlipperWalkPrint/blob/main/esp32_bridge/walkprint_esp32_bridge/walkprint_esp32_bridge.ino)
- Bridge notes: [esp32_bridge](https://github.com/jsammarco/FlipperWalkPrint/tree/main/esp32_bridge)

Wiring:

- ESP32 `GPIO17` TX -> Flipper `PB7` RX
- ESP32 `GPIO16` RX -> Flipper `PB6` TX
- `GND` -> `GND`

Protocol examples:

- `PING`
- `BT_SCAN`
- `BT_CONNECT|25:00:35:00:03:57`
- `BT_WRITE_HEX|...`
- `WIFI_SCAN`

## Project Layout

```text
.
|-- .gitignore
|-- README.md
|-- application.fam
|-- app_ui.c
|-- app_ui.h
|-- build.sh
|-- deploy.sh
|-- walkprint_app.c
|-- walkprint_app.h
|-- walkprint_config.h
|-- walkprint_debug.c
|-- walkprint_debug.h
|-- walkprint_protocol.c
|-- walkprint_protocol.h
|-- walkprint_transport.h
`-- walkprint_transport_live.c
```

## What You Can Edit

Printer-specific placeholders live in `walkprint_config.h`.

Edit these first:

- `walkprint_config_init_frame`
- `walkprint_config_start_print_frame`
- `walkprint_config_end_print_frame`
- `walkprint_config_feed_frame`
- `walkprint_config_raw_frame`
- `walkprint_demo_receipt_lines`

## Build

```bash
export FLIPPER_FW_PATH=/path/to/Momentum-Firmware
./build.sh
```

Optional:

```bash
./build.sh --fw-path /path/to/Momentum-Firmware
./build.sh --fw-path /path/to/Momentum-Firmware --copy
```

The Windows build script also works directly with your local checkout:

```powershell
./build.ps1 -FirmwareDir C:\Users\jasammarco.ENG\Projects\Momentum-Firmware
```

## Deploy

Mounted SD card:

```bash
./deploy.sh --mount /Volumes/FLIPPER
```

qFlipper CLI:

```bash
./deploy.sh
```

## App Menu

- Discover Printer
- Connect
- Print Message
- Print TXT
- Print QR
- Print Barcode
- Print BMP
- Feed Paper
- WiFi Scan
- Settings
- About

The app now shows:

- connection state
- the saved printer address, once discovery or manual entry sets it
- the configured density placeholder
- the configured font size from `1-10`
- the selected font family
- the latest bridge discovery or Wi-Fi scan result
- BMP print status for `.bmp` files selected from storage
- generated QR BMP status for Flipper-side save and print
- generated barcode BMP status for Flipper-side save and print
- TXT print status for `.txt` files selected from storage
- persisted settings after the app closes, including printer MAC, message, BMP path, and print options

The desktop bridge now adds:

- `python bridge/walkprint_bridge.py image <path>` to send an image directly
- `python bridge/walkprint_bridge.py convert-image <path>` to generate the printer payload without sending
- `python bridge/walkprint_bridge.py make-qr --text "..."` to generate a `384px` BMP QR code for Flipper-side `Print BMP`
- `python bridge/walkprint_bridge.py make-qr --input-file path.txt` to generate a QR code from a file
- `python bridge/walkprint_bridge.py make-barcode --text "..."` to generate a `384px` BMP 1D barcode for Flipper-side `Print BMP`
- `python bridge/walkprint_bridge.py make-barcode --input-file path.txt` to generate a 1D barcode from a file
- `python bridge/walkprint_bridge.py qr --text "..."` to generate and print a QR code directly
- `python bridge/walkprint_bridge.py barcode --text "..."` to generate and print a 1D barcode directly
- `.\bridge\send_image.ps1` to select an image with a Windows file picker and send it
- `.\bridge\send_image.ps1 -BmpOnly` to pick only `.bmp` files and send them at the default `384px` print width

## Notes

- The Flipper talks to the printer through the ESP32 bridge over UART; it does not open the printer's Classic Bluetooth link on its own.
- `Print TXT` reads a `.txt` file from the Flipper SD card, keeps line breaks, and prints it across as many text pages as needed. While a multi-page TXT job is running, `Back` cancels after the current page finishes.
- `Print QR` now generates a QR BMP directly on the Flipper. The app first collects QR text, then asks for the BMP filename, then offers `Save + Print` through the existing BMP confirmation flow. The current on-device encoder uses QR version `3-L` and accepts up to `53` characters.
- `Print Barcode` now generates a Code 128 B BMP directly on the Flipper. The app first collects barcode text, then asks for the BMP filename, then offers `Save + Print` through the same confirmation flow. The current on-device encoder accepts printable ASCII and keeps input to `24` characters so the bars still fit at `384px`.
- The built-in text renderer is best for standard printable ASCII. If a text file uses Unicode art, block characters, or other extended symbols, convert it to an image and use `Print BMP` instead.
- `Print BMP` expects a `.bmp` file on the Flipper SD card that is exactly `384px` wide. Files with other widths are rejected by the app.
- After selecting a BMP, the Flipper now shows a confirmation screen with `OK` to print and `Back` to cancel. Generated QR and barcode jobs reuse the same screen for `Save + Print`, and the saved file name comes from the filename entered on the Flipper keyboard. The app normalizes spaces to underscores, keeps safe filename characters, and appends `.bmp` when needed.
- The new QR and 1D barcode helpers in `bridge/walkprint_bridge.py` generate `384px` BMP files specifically so they can be copied to the Flipper SD card and printed with the existing `Print BMP` menu item.
- Use the Arduino IDE to compile and upload the ESP32 bridge sketch, and make sure the partition option matches the included `partitions.csv` setup.
- Good next additions for the bridge would be printer status polling, Wi-Fi connect commands, and better paging for larger Bluetooth scan results.
