# WalkPrint Bridge

`walkprint_bridge.py` is the practical path for real discovery and real print sends for WalkPrint / YHK printers that use Classic Bluetooth RFCOMM.

It runs on your computer, discovers nearby printers, resolves or guesses an RFCOMM channel, and can now send text, raw bytes, converted image raster payloads, QR codes, and 1D barcodes.

## Install

```powershell
python -m pip install -r bridge/requirements.txt
```

## Discover Nearby Printers

```powershell
python bridge/walkprint_bridge.py scan
```

This filters names using common WalkPrint/YHK keywords like `YHK`, `printer`, `thermal`, and `cat`.

## Send A Test Receipt

```powershell
python bridge/walkprint_bridge.py test --address 25:00:35:00:03:57
```

If service discovery cannot resolve RFCOMM automatically, the bridge falls back to channel `2`, which matches common YHK reverse-engineering examples.

## Feed Paper

```powershell
python bridge/walkprint_bridge.py feed --address 25:00:35:00:03:57 --lines 3
```

## Send Raw Bytes

```powershell
python bridge/walkprint_bridge.py raw --address 25:00:35:00:03:57 --hex "1D 67 69"
```

## Convert An Image Without Sending

```powershell
python bridge/walkprint_bridge.py convert-image .\photo.png --output .\bridge\out\photo.bin --preview .\bridge\out\photo-preview.png
```

This converts the image to monochrome printer raster bytes and saves the exact payload that would be sent.

## Generate A QR BMP For Flipper Printing

```powershell
python bridge/walkprint_bridge.py make-qr --text "https://consultingjoe.com" --output .\bridge\out\qr.bmp
```

Or encode the contents of a text file:

```powershell
python bridge/walkprint_bridge.py make-qr --input-file .\TextFiles\label.txt --output .\bridge\out\qr.bmp
```

This writes a `384px` wide BMP you can copy to the Flipper SD card and print with `Print BMP`.

## Generate A 1D Barcode BMP For Flipper Printing

```powershell
python bridge/walkprint_bridge.py make-barcode --text "123456789012" --format code128 --output .\bridge\out\barcode.bmp
```

Or encode the contents of a text file:

```powershell
python bridge/walkprint_bridge.py make-barcode --input-file .\TextFiles\label.txt --format code128 --output .\bridge\out\barcode.bmp
```

Useful barcode options:

- `--format code128` for general text and mixed characters
- `--format ean13`, `ean8`, or `upca` for retail-style numeric codes
- `--no-text` to omit the human-readable line under the bars

## Generate And Send A QR Code Directly

```powershell
python bridge/walkprint_bridge.py qr --text "WIFI:S:MyNet;T:WPA;P:secret;;" --address 25:00:35:00:03:57 --output .\bridge\out\wifi-qr.bmp
```

## Generate And Send A 1D Barcode Directly

```powershell
python bridge/walkprint_bridge.py barcode --text "ABC-123-XYZ" --format code128 --address 25:00:35:00:03:57 --output .\bridge\out\label.bmp
```

## Send An Image

```powershell
python bridge/walkprint_bridge.py image .\photo.png --address 25:00:35:00:03:57 --preview .\bridge\out\photo-preview.png
```

The bridge targets a `384` pixel print width by default and automatically resizes images that are not already `384` pixels wide.

Useful options:

- `--width 384` to control the target width in printer pixels
- `--dither floyd-steinberg` or `--dither threshold`
- `--threshold 160` when using threshold mode
- `--feed-lines 3` to add blank paper after the image
- `--text "..."` or `--input-file path.txt` for QR and barcode content

## Pick An Image On Windows

```powershell
.\bridge\send_image.ps1 -Address 25:00:35:00:03:57
```

This opens a file picker, lets you choose an image, resizes it to `384` pixels wide when needed, then sends it through the bridge.

To pick only bitmap files:

```powershell
.\bridge\send_image.ps1 -BmpOnly -Address 25:00:35:00:03:57
```

## Notes

- This bridge is for Classic Bluetooth printers, not BLE GATT printers.
- Image printing uses an ESC/POS-style `GS v 0` raster command, which is a better fit than the earlier text placeholder path.
- QR and barcode generation save plain BMP files so the Flipper app can print them through the existing `Print BMP` flow.
- If your printer needs a different RFCOMM channel, pass `--channel N`.
- If test sends connect but do not print, the next step is refining the printer protocol bytes, not the transport.
