# WalkPrint Bridge

`walkprint_bridge.py` is the practical path for real discovery and real print sends for WalkPrint / YHK printers that use Classic Bluetooth RFCOMM.

It runs on your computer, discovers nearby printers, resolves or guesses an RFCOMM channel, and can now send text, raw bytes, and converted image raster payloads.

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
- If your printer needs a different RFCOMM channel, pass `--channel N`.
- If test sends connect but do not print, the next step is refining the printer protocol bytes, not the transport.
