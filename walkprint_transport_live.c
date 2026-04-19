#include "walkprint_transport.h"

#include "walkprint_debug.h"

#include <notification/notification_messages.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define WALKPRINT_BRIDGE_BAUDRATE 115200U
#define WALKPRINT_BRIDGE_RX_STREAM_SIZE 512U
#define WALKPRINT_BRIDGE_LINE_SIZE 160U
#define WALKPRINT_BRIDGE_MAX_HEX_COMMAND_BYTES 1024U
#define WALKPRINT_BRIDGE_DEFAULT_TIMEOUT_MS 12000U
#define WALKPRINT_BRIDGE_PING_TIMEOUT_MS 1000U

typedef void (*WalkPrintBridgeLineHandler)(WalkPrintTransport* transport, const char* line);

static uint32_t walkprint_bridge_ms_to_ticks(uint32_t ms) {
    uint32_t tick_hz = furi_kernel_get_tick_frequency();
    uint64_t ticks = ((uint64_t)ms * tick_hz + 999ULL) / 1000ULL;
    if(ticks == 0) {
        ticks = 1;
    }
    return (uint32_t)ticks;
}

static void walkprint_bridge_set_response(WalkPrintTransport* transport, const char* message) {
    if(!transport) {
        return;
    }

    snprintf(
        transport->last_response,
        sizeof(transport->last_response),
        "%s",
        message ? message : "");
}

static void walkprint_bridge_async_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    WalkPrintTransport* transport = context;

    if(!transport || !transport->rx_stream || !handle) {
        return;
    }

    if((event & FuriHalSerialRxEventData) == 0) {
        return;
    }

    while(furi_hal_serial_async_rx_available(handle)) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(transport->rx_stream, &byte, sizeof(byte), 0);
    }
}

static void walkprint_bridge_flush_rx(WalkPrintTransport* transport) {
    uint8_t sink[32];

    if(!transport || !transport->rx_stream) {
        return;
    }

    while(furi_stream_buffer_receive(transport->rx_stream, sink, sizeof(sink), 0) > 0) {
    }
}

static bool walkprint_bridge_read_line(
    WalkPrintTransport* transport,
    char* line,
    size_t line_size,
    uint32_t timeout_ms) {
    uint8_t byte = 0;
    size_t line_len = 0;
    uint32_t deadline = furi_get_tick() + walkprint_bridge_ms_to_ticks(timeout_ms);

    if(!transport || !transport->rx_stream || !line || line_size < 2) {
        return false;
    }

    memset(line, 0, line_size);

    while(furi_get_tick() < deadline) {
        uint32_t now = furi_get_tick();
        uint32_t remaining_ticks = (deadline > now) ? (deadline - now) : 0;
        uint32_t wait_ticks = remaining_ticks > 10U ? 10U : remaining_ticks;
        size_t received =
            furi_stream_buffer_receive(transport->rx_stream, &byte, sizeof(byte), wait_ticks);

        if(received == 0) {
            continue;
        }

        if(byte == '\r') {
            continue;
        }

        if(byte == '\n') {
            if(line_len > 0) {
                line[line_len] = '\0';
                walkprint_debug_log_info("Bridge RX: %s", line);
                return true;
            }
            continue;
        }

        if(line_len + 1U < line_size) {
            line[line_len++] = (char)byte;
        }
    }

    return false;
}

static void walkprint_bridge_send_command(WalkPrintTransport* transport, const char* command) {
    size_t command_len;

    if(!transport || !transport->serial_handle || !command) {
        return;
    }

    snprintf(
        transport->tx_line_buffer,
        sizeof(transport->tx_line_buffer),
        "%s\n",
        command);
    command_len = strlen(transport->tx_line_buffer);

    walkprint_debug_log_info("Bridge TX: %s", command);
    furi_hal_serial_tx(
        transport->serial_handle, (const uint8_t*)transport->tx_line_buffer, command_len);
    furi_hal_serial_tx_wait_complete(transport->serial_handle);
}

static void walkprint_bridge_handle_bt_scan_line(WalkPrintTransport* transport, const char* line) {
    const char* mac_start;
    const char* name_start;
    const char* name_field;
    size_t mac_len;

    if(!transport || !line || strncmp(line, "BT|", 3) != 0) {
        return;
    }

    mac_start = line + 3;
    name_start = strchr(mac_start, '|');
    if(!name_start) {
        return;
    }

    mac_len = (size_t)(name_start - mac_start);
    if(mac_len + 1U > sizeof(transport->printer_address)) {
        return;
    }

    memset(transport->printer_address, 0, sizeof(transport->printer_address));
    memcpy(transport->printer_address, mac_start, mac_len);

    name_field = name_start + 1;
    snprintf(transport->printer_name, sizeof(transport->printer_name), "%s", name_field);

    if(transport->printer_name[0] != '\0') {
        walkprint_bridge_set_response(transport, transport->printer_name);
    } else {
        walkprint_bridge_set_response(transport, transport->printer_address);
    }
}

static void walkprint_bridge_handle_wifi_line(WalkPrintTransport* transport, const char* line) {
    char ssid[WALKPRINT_STATUS_TEXT_SIZE];
    char summary[WALKPRINT_STATUS_TEXT_SIZE];
    int rssi = 0;
    char encryption[16];

    if(!transport || !line || strncmp(line, "WIFI|", 5) != 0) {
        return;
    }

    memset(ssid, 0, sizeof(ssid));
    memset(encryption, 0, sizeof(encryption));
    if(sscanf(line, "WIFI|%47[^|]|%d|%15s", ssid, &rssi, encryption) >= 2) {
        snprintf(summary, sizeof(summary), "%.36s %ddBm", ssid, rssi);
        if(transport->wifi_network_count < 10U) {
            snprintf(
                transport->wifi_networks[transport->wifi_network_count],
                WALKPRINT_STATUS_TEXT_SIZE,
                "%s",
                summary);
            transport->wifi_network_count++;
        }
        walkprint_bridge_set_response(transport, summary);
    }
}

static bool walkprint_bridge_wait_for_result(
    WalkPrintTransport* transport,
    uint32_t timeout_ms,
    WalkPrintBridgeLineHandler line_handler) {
    char line[WALKPRINT_BRIDGE_LINE_SIZE];

    while(walkprint_bridge_read_line(transport, line, sizeof(line), timeout_ms)) {
        if(strncmp(line, "OK|", 3) == 0) {
            if(strcmp(line, "OK|PONG") == 0) {
                walkprint_bridge_set_response(transport, "Bridge online");
            } else if(strncmp(line, "OK|BT_SCAN|", 11) == 0) {
                if(transport->printer_name[0] != '\0') {
                    walkprint_bridge_set_response(transport, transport->printer_name);
                } else {
                    walkprint_bridge_set_response(transport, transport->printer_address);
                }
            } else if(strncmp(line, "OK|BT_CONNECT|", 14) == 0) {
                walkprint_bridge_set_response(transport, line + 14);
            } else if(strncmp(line, "OK|BT_DISCONNECT", 16) == 0) {
                walkprint_bridge_set_response(transport, "Printer link closed");
            } else if(strncmp(line, "OK|BT_WRITE_HEX|", 16) == 0) {
                walkprint_bridge_set_response(transport, line + 16);
            } else if(strncmp(line, "OK|WIFI_SCAN|", 13) == 0) {
                if(transport->last_response[0] == '\0') {
                    walkprint_bridge_set_response(transport, "WiFi scan complete");
                }
            }
            return true;
        }

        if(strncmp(line, "ERR|", 4) == 0) {
            walkprint_bridge_set_response(transport, line + 4);
            return false;
        }

        if(strncmp(line, "BT_RX|", 6) == 0) {
            walkprint_bridge_set_response(transport, line + 6);
            continue;
        }

        if(line_handler) {
            line_handler(transport, line);
        }
    }

    walkprint_bridge_set_response(transport, "Bridge timeout");
    return false;
}

static bool walkprint_bridge_ping(WalkPrintTransport* transport) {
    walkprint_bridge_set_response(transport, "");
    walkprint_bridge_flush_rx(transport);
    walkprint_bridge_send_command(transport, "PING");
    return walkprint_bridge_wait_for_result(transport, WALKPRINT_BRIDGE_PING_TIMEOUT_MS, NULL);
}

static bool walkprint_bridge_ensure_ready(WalkPrintTransport* transport) {
    if(!transport || !transport->initialized || !transport->serial_handle) {
        return false;
    }

    if(transport->bridge_ready) {
        return true;
    }

    transport->bridge_ready = walkprint_bridge_ping(transport);
    if(!transport->bridge_ready) {
        if(transport->last_response[0] == '\0') {
            walkprint_bridge_set_response(transport, "Bridge offline");
        }
        transport->connected = false;
        return false;
    }

    return true;
}

static bool walkprint_bridge_send_hex_command(
    WalkPrintTransport* transport,
    const char* command_prefix,
    const uint8_t* data,
    size_t length) {
    size_t offset = 0;

    if(!transport || !command_prefix || (!data && length > 0)) {
        return false;
    }

    offset = (size_t)snprintf(
        transport->tx_command_buffer,
        sizeof(transport->tx_command_buffer),
        "%s|",
        command_prefix);
    for(size_t i = 0; i < length && (offset + 2U) < sizeof(transport->tx_command_buffer); i++) {
        offset += (size_t)snprintf(
            transport->tx_command_buffer + offset,
            sizeof(transport->tx_command_buffer) - offset,
            "%02X",
            data[i]);
    }

    walkprint_bridge_set_response(transport, "");
    walkprint_bridge_flush_rx(transport);
    walkprint_bridge_send_command(transport, transport->tx_command_buffer);
    return walkprint_bridge_wait_for_result(
        transport, WALKPRINT_BRIDGE_DEFAULT_TIMEOUT_MS, NULL);
}

static bool walkprint_transport_live_init(WalkPrintTransport* transport, const char* printer_address) {
    if(!transport || !printer_address) {
        return false;
    }

    snprintf(
        transport->printer_address,
        sizeof(transport->printer_address),
        "%s",
        printer_address);
    transport->printer_name[0] = '\0';
    transport->last_response[0] = '\0';
    transport->bridge_ready = false;
    transport->initialized = false;
    transport->connected = false;
    transport->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!transport->serial_handle) {
        walkprint_bridge_set_response(transport, "USART busy");
        return false;
    }

    transport->rx_stream = furi_stream_buffer_alloc(WALKPRINT_BRIDGE_RX_STREAM_SIZE, 1);
    if(!transport->rx_stream) {
        furi_hal_serial_control_release(transport->serial_handle);
        transport->serial_handle = NULL;
        walkprint_bridge_set_response(transport, "RX alloc failed");
        return false;
    }

    furi_hal_serial_init(transport->serial_handle, WALKPRINT_BRIDGE_BAUDRATE);
    furi_hal_serial_configure_framing(
        transport->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_enable_direction(transport->serial_handle, FuriHalSerialDirectionTx);
    furi_hal_serial_enable_direction(transport->serial_handle, FuriHalSerialDirectionRx);
    furi_hal_serial_async_rx_start(
        transport->serial_handle,
        walkprint_bridge_async_rx_callback,
        transport,
        false);

    transport->initialized = true;
    transport->bridge_ready = walkprint_bridge_ping(transport);
    walkprint_debug_log_info(
        "UART bridge init on Flipper pins 13/14 (PB6/PB7) @ %lu",
        (unsigned long)WALKPRINT_BRIDGE_BAUDRATE);

    return true;
}

static bool walkprint_transport_live_connect(WalkPrintTransport* transport) {
    char command[64];

    if(!transport || !transport->initialized) {
        return false;
    }

    if(!walkprint_bridge_ensure_ready(transport)) {
        return false;
    }

    snprintf(command, sizeof(command), "BT_CONNECT|%s", transport->printer_address);
    walkprint_bridge_set_response(transport, "");
    walkprint_bridge_flush_rx(transport);
    walkprint_bridge_send_command(transport, command);
    transport->connected = walkprint_bridge_wait_for_result(
        transport, WALKPRINT_BRIDGE_DEFAULT_TIMEOUT_MS, NULL);
    return transport->connected;
}

static void walkprint_transport_live_disconnect(WalkPrintTransport* transport) {
    if(!transport) {
        return;
    }

    if(transport->initialized && transport->serial_handle) {
        if(transport->connected) {
            walkprint_bridge_flush_rx(transport);
            walkprint_bridge_send_command(transport, "BT_DISCONNECT");
            walkprint_bridge_wait_for_result(transport, 2000U, NULL);
        }

        furi_hal_serial_async_rx_stop(transport->serial_handle);
        furi_hal_serial_disable_direction(transport->serial_handle, FuriHalSerialDirectionTx);
        furi_hal_serial_disable_direction(transport->serial_handle, FuriHalSerialDirectionRx);
        furi_hal_serial_deinit(transport->serial_handle);
        furi_hal_serial_control_release(transport->serial_handle);
    }

    if(transport->rx_stream) {
        furi_stream_buffer_free(transport->rx_stream);
    }

    transport->rx_stream = NULL;
    transport->serial_handle = NULL;
    transport->connected = false;
    transport->initialized = false;
    transport->bridge_ready = false;
}

static bool walkprint_transport_live_send(
    WalkPrintTransport* transport,
    const uint8_t* data,
    size_t length) {
    size_t offset = 0;

    if(!transport || !transport->connected || !data || length == 0) {
        return false;
    }

    if(!walkprint_bridge_ensure_ready(transport)) {
        return false;
    }

    while(offset < length) {
        size_t chunk_length = length - offset;
        if(chunk_length > WALKPRINT_BRIDGE_MAX_HEX_COMMAND_BYTES) {
            chunk_length = WALKPRINT_BRIDGE_MAX_HEX_COMMAND_BYTES;
        }

        if(transport->notifications) {
            notification_message(transport->notifications, &sequence_blink_blue_10);
        }

        if(!walkprint_bridge_send_hex_command(
               transport, "BT_WRITE_HEX", data + offset, chunk_length)) {
            return false;
        }

        offset += chunk_length;
    }

    transport->send_count++;
    return true;
}

static bool walkprint_transport_live_discover_printer(WalkPrintTransport* transport) {
    if(!transport || !transport->initialized) {
        return false;
    }

    if(!walkprint_bridge_ensure_ready(transport)) {
        return false;
    }

    transport->printer_name[0] = '\0';
    transport->wifi_network_count = 0;
    walkprint_bridge_set_response(transport, "");
    walkprint_bridge_flush_rx(transport);
    walkprint_bridge_send_command(transport, "BT_SCAN");
    return walkprint_bridge_wait_for_result(
        transport, WALKPRINT_BRIDGE_DEFAULT_TIMEOUT_MS, walkprint_bridge_handle_bt_scan_line);
}

static bool walkprint_transport_live_scan_wifi(WalkPrintTransport* transport) {
    if(!transport || !transport->initialized) {
        return false;
    }

    if(!walkprint_bridge_ensure_ready(transport)) {
        return false;
    }

    transport->wifi_network_count = 0;
    walkprint_bridge_set_response(transport, "");
    walkprint_bridge_flush_rx(transport);
    walkprint_bridge_send_command(transport, "WIFI_SCAN");
    return walkprint_bridge_wait_for_result(
        transport, WALKPRINT_BRIDGE_DEFAULT_TIMEOUT_MS, walkprint_bridge_handle_wifi_line);
}

static bool walkprint_transport_live_is_connected(const WalkPrintTransport* transport) {
    return transport && transport->connected;
}

static const WalkPrintTransportOps walkprint_transport_live_instance = {
    .name = "uart-bridge",
    .init = walkprint_transport_live_init,
    .connect = walkprint_transport_live_connect,
    .disconnect = walkprint_transport_live_disconnect,
    .send = walkprint_transport_live_send,
    .discover_printer = walkprint_transport_live_discover_printer,
    .scan_wifi = walkprint_transport_live_scan_wifi,
    .is_connected = walkprint_transport_live_is_connected,
};

const WalkPrintTransportOps* walkprint_transport_live_ops(void) {
    return &walkprint_transport_live_instance;
}

const char* walkprint_transport_name(void) {
    return "uart-bridge";
}
