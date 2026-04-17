#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <memory>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled for this ESP32 target.
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth SPP is not enabled for this ESP32 target.
#endif

namespace {
constexpr uint32_t kBridgeBaud = 115200;
constexpr uint32_t kBtDiscoverMs = 8000;
constexpr uint32_t kBtReplyWindowMs = 350;
constexpr size_t kMaxBridgeLine = 4096;

// Default to UART0 so the bridge works with the RX/TX pins printed on most ESP32 dev boards.
// If you rewire to GPIO16/GPIO17 later, flip this to 0 and rebuild.
#define WALKPRINT_BRIDGE_USE_UART0 1

#if WALKPRINT_BRIDGE_USE_UART0
HardwareSerial& BridgeSerial = Serial;
constexpr int kBridgeRxPin = 3;
constexpr int kBridgeTxPin = 1;
#else
HardwareSerial& BridgeSerial = Serial2;
constexpr int kBridgeRxPin = 16;
constexpr int kBridgeTxPin = 17;
#endif

BluetoothSerial SerialBT;

String bridgeLine;
String connectedAddress;
String connectedName;
bool btConnected = false;
bool bridgeSawCommand = false;

void debugLog(const String& line) {
#if !WALKPRINT_BRIDGE_USE_UART0
  Serial.print("[bridge] ");
  Serial.println(line);
#else
  (void)line;
#endif
}

void bridgeReply(const String& line) {
  BridgeSerial.print(line);
  BridgeSerial.print('\n');
  debugLog(line);
}

bool parseMacAddress(const String& macText, uint8_t out[6]) {
  int values[6] = {0};
  if (sscanf(macText.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }

  for (size_t i = 0; i < 6; ++i) {
    out[i] = static_cast<uint8_t>(values[i]);
  }
  return true;
}

String splitField(String& input, char delimiter) {
  int pos = input.indexOf(delimiter);
  if (pos < 0) {
    String value = input;
    input = "";
    return value;
  }

  String value = input.substring(0, pos);
  input = input.substring(pos + 1);
  return value;
}

String bytesToHex(const uint8_t* data, size_t len) {
  String hex;
  hex.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x10) {
      hex += '0';
    }
    hex += String(data[i], HEX);
  }
  hex.toUpperCase();
  return hex;
}

bool hexToBytes(const String& hex, std::unique_ptr<uint8_t[]>& out, size_t& outLen) {
  String clean = hex;
  clean.replace(" ", "");
  if ((clean.length() % 2) != 0 || clean.isEmpty()) {
    return false;
  }

  outLen = clean.length() / 2;
  out.reset(new uint8_t[outLen]);
  for (size_t i = 0; i < outLen; ++i) {
    char hi = clean[i * 2];
    char lo = clean[i * 2 + 1];
    if (!isxdigit(hi) || !isxdigit(lo)) {
      return false;
    }
    String byteText = clean.substring(i * 2, i * 2 + 2);
    out[i] = static_cast<uint8_t>(strtoul(byteText.c_str(), nullptr, 16));
  }
  return true;
}

void handleBtRxWindow() {
  uint32_t deadline = millis() + kBtReplyWindowMs;
  uint8_t buffer[128];

  while (millis() < deadline) {
    size_t count = 0;
    while (SerialBT.available() && count < sizeof(buffer)) {
      buffer[count++] = static_cast<uint8_t>(SerialBT.read());
    }

    if (count > 0) {
      bridgeReply("BT_RX|" + bytesToHex(buffer, count));
      deadline = millis() + kBtReplyWindowMs;
    } else {
      delay(10);
    }
  }
}

void runBtScan() {
  int count = 0;

  BTScanResults* results = SerialBT.discover(kBtDiscoverMs);
  if (!results) {
    bridgeReply("ERR|BT scan failed");
    return;
  }

  count = results->getCount();
  for (int i = 0; i < count; ++i) {
    BTAdvertisedDevice* device = results->getDevice(i);
    if (!device) {
      continue;
    }

    String address = device->getAddress().toString().c_str();
    String name = device->getName().c_str();
    if (name.isEmpty()) {
      name = "<unknown>";
    }
    bridgeReply("BT|" + address + "|" + name);
  }

  bridgeReply("OK|BT_SCAN|" + String(count));
}

void runBtConnect(const String& macText) {
  uint8_t address[6] = {0};

  if (!parseMacAddress(macText, address)) {
    bridgeReply("ERR|Invalid MAC");
    return;
  }

  if (btConnected) {
    SerialBT.disconnect();
    btConnected = false;
  }

  if (SerialBT.connect(address)) {
    btConnected = true;
    connectedAddress = macText;
    connectedName = "";
    bridgeReply("OK|BT_CONNECT|" + macText);
  } else {
    bridgeReply("ERR|BT connect failed");
  }
}

void runBtDisconnect() {
  if (btConnected) {
    SerialBT.disconnect();
  }
  btConnected = false;
  connectedAddress = "";
  connectedName = "";
  bridgeReply("OK|BT_DISCONNECT");
}

void runBtWriteHex(const String& hex) {
  std::unique_ptr<uint8_t[]> payload;
  size_t payloadLen = 0;

  if (!btConnected) {
    bridgeReply("ERR|No BT printer connected");
    return;
  }

  if (!hexToBytes(hex, payload, payloadLen)) {
    bridgeReply("ERR|Invalid hex payload");
    return;
  }

  size_t written = SerialBT.write(payload.get(), payloadLen);
  SerialBT.flush();
  handleBtRxWindow();
  bridgeReply("OK|BT_WRITE_HEX|" + String(written));
}

void runWiFiScan() {
  int count = 0;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);

  count = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (count < 0) {
    bridgeReply("ERR|WiFi scan failed");
    return;
  }

  for (int i = 0; i < count; ++i) {
    String line = "WIFI|";
    line += WiFi.SSID(i);
    line += "|";
    line += String(WiFi.RSSI(i));
    line += "|";
    line += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secure";
    bridgeReply(line);
  }

  WiFi.scanDelete();
  bridgeReply("OK|WIFI_SCAN|" + String(count));
}

void runWiFiStatus() {
  wl_status_t status = WiFi.status();
  String line = "OK|WIFI_STATUS|";
  line += String(static_cast<int>(status));
  line += "|";
  line += WiFi.SSID();
  line += "|";
  line += WiFi.localIP().toString();
  bridgeReply(line);
}

void handleCommand(String line) {
  line.trim();
  if (line.isEmpty()) {
    return;
  }

  bridgeSawCommand = true;
  debugLog("CMD " + line);

  if (line == "PING") {
    bridgeReply("OK|PONG");
    return;
  }

  if (line == "BT_SCAN") {
    runBtScan();
    return;
  }

  if (line == "BT_DISCONNECT") {
    runBtDisconnect();
    return;
  }

  if (line == "WIFI_SCAN") {
    runWiFiScan();
    return;
  }

  if (line == "WIFI_STATUS") {
    runWiFiStatus();
    return;
  }

  String remainder = line;
  String command = splitField(remainder, '|');

  if (command == "BT_CONNECT") {
    runBtConnect(remainder);
    return;
  }

  if (command == "BT_WRITE_HEX") {
    runBtWriteHex(remainder);
    return;
  }

  bridgeReply("ERR|Unknown command");
}
}  // namespace

void setup() {
#if WALKPRINT_BRIDGE_USE_UART0
  BridgeSerial.begin(kBridgeBaud);
#else
  Serial.begin(kBridgeBaud);
  BridgeSerial.begin(kBridgeBaud, SERIAL_8N1, kBridgeRxPin, kBridgeTxPin);
#endif

  delay(250);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  SerialBT.begin("WalkPrintBridge", true);

  bridgeReply("OK|BOOT");
  debugLog("WalkPrint ESP32 bridge started");
}

void loop() {
  while (BridgeSerial.available()) {
    char c = static_cast<char>(BridgeSerial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(bridgeLine);
      bridgeLine = "";
      continue;
    }
    if (bridgeLine.length() < kMaxBridgeLine) {
      bridgeLine += c;
    }
  }

  if (btConnected && SerialBT.hasClient() == 0) {
    btConnected = false;
  }
}
