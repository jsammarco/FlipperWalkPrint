# WalkPrint Bridge

`walkprint_bridge.py` is the practical path for real discovery and real print sends for WalkPrint / YHK printers that use Classic Bluetooth RFCOMM.

It runs on your computer, discovers nearby printers, resolves or guesses an RFCOMM channel, and sends the same kinds of test/feed/raw payloads this repo already builds.

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

## Notes

- This bridge is for Classic Bluetooth printers, not BLE GATT printers.
- The payloads are still placeholders from this repo's current protocol layer.
- If your printer needs a different RFCOMM channel, pass `--channel N`.
- If test sends connect but do not print, the next step is refining the printer protocol bytes, not the transport.
