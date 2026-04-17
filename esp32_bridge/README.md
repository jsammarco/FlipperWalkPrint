# ESP32 Bridge

This Arduino sketch turns an ESP32-WROOM into a UART bridge between the Flipper Zero and:

- Classic Bluetooth SPP printers
- Wi-Fi scan/status helpers

## Default UART Wiring

ESP32 side:

- Board `TX` / `GPIO1` -> Flipper `PB7` RX
- Board `RX` / `GPIO3` -> Flipper `PB6` TX
- `GND` -> `GND`

Flipper side:

- USART on Flipper header pins `13/14` (`PB6/PB7`)
- `115200` baud
- `8N1`

Keep the signal levels at `3.3V`.

Alternative wiring:

- If you want to use ESP32 `GPIO16/GPIO17` instead, change `WALKPRINT_BRIDGE_USE_UART0` to `0` in [walkprint_esp32_bridge.ino](/C:/Users/jasammarco.ENG/Projects/WalkPrint/esp32_bridge/walkprint_esp32_bridge/walkprint_esp32_bridge.ino) and rebuild.

## Arduino Setup

Board assumptions:

- ESP32-WROOM class board
- Arduino ESP32 core with Classic Bluetooth SPP enabled
- 4 MB flash

Partitioning:

- This sketch includes a local [partitions.csv](/C:/Users/jasammarco.ENG/Projects/WalkPrint/esp32_bridge/walkprint_esp32_bridge/partitions.csv) with a large single-app layout because Bluetooth SPP plus Wi-Fi pushes the binary well past the small default app slot.
- If Arduino IDE still uses a smaller partition menu choice on your board profile, switch `Tools -> Partition Scheme` to a large app option such as `Huge APP` or `Minimal SPIFFS`.

Libraries used:

- `BluetoothSerial`
- `WiFi`

## UART Protocol

Commands from Flipper:

- `PING`
- `BT_SCAN`
- `BT_CONNECT|AA:BB:CC:DD:EE:FF`
- `BT_DISCONNECT`
- `BT_WRITE_HEX|1B401D49F019...`
- `WIFI_SCAN`
- `WIFI_STATUS`

Responses back to Flipper:

- `OK|...`
- `ERR|...`
- `BT|mac|name`
- `BT_RX|hexbytes`
- `WIFI|ssid|rssi|open` or `WIFI|ssid|rssi|secure`

## Notes

- The ESP32 is the device doing Classic Bluetooth work.
- The Flipper app talks only to the ESP32 over UART.
- The current `BT_WRITE_HEX` path sends printer bytes as-is and then briefly captures any printer response bytes.
- The sketch now emits `OK|BOOT` once at startup, which is useful when sanity-checking raw UART traffic.
