#pragma once

#include "walkprint_config.h"

#include <furi.h>
#include <furi_hal.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WalkPrintTransport WalkPrintTransport;

typedef struct {
    const char* name;
    bool (*init)(WalkPrintTransport* transport, const char* printer_address);
    bool (*connect)(WalkPrintTransport* transport);
    void (*disconnect)(WalkPrintTransport* transport);
    bool (*send)(WalkPrintTransport* transport, const uint8_t* data, size_t length);
    bool (*discover_printer)(WalkPrintTransport* transport);
    bool (*scan_wifi)(WalkPrintTransport* transport);
    bool (*is_connected)(const WalkPrintTransport* transport);
} WalkPrintTransportOps;

struct WalkPrintTransport {
    const WalkPrintTransportOps* ops;
    bool initialized;
    bool connected;
    bool bridge_ready;
    char printer_address[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];
    char printer_name[WALKPRINT_STATUS_TEXT_SIZE];
    char last_response[WALKPRINT_STATUS_TEXT_SIZE];
    char wifi_networks[10][WALKPRINT_STATUS_TEXT_SIZE];
    size_t wifi_network_count;
    uint32_t send_count;
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    char tx_command_buffer[WALKPRINT_FRAME_MAX_SIZE * 2U + 32U];
    char tx_line_buffer[WALKPRINT_FRAME_MAX_SIZE * 2U + 32U];
};

const WalkPrintTransportOps* walkprint_transport_live_ops(void);
const char* walkprint_transport_name(void);

static inline bool walkprint_transport_init(
    WalkPrintTransport* transport,
    const WalkPrintTransportOps* ops,
    const char* printer_address) {
    if(!transport || !ops || !ops->init) {
        return false;
    }

    transport->ops = ops;
    transport->initialized = false;
    transport->connected = false;
    transport->wifi_network_count = 0;
    transport->send_count = 0;

    return ops->init(transport, printer_address);
}

static inline bool walkprint_transport_connect(WalkPrintTransport* transport) {
    return transport && transport->ops && transport->ops->connect &&
           transport->ops->connect(transport);
}

static inline void walkprint_transport_disconnect(WalkPrintTransport* transport) {
    if(transport && transport->ops && transport->ops->disconnect) {
        transport->ops->disconnect(transport);
    }
}

static inline bool walkprint_transport_send(
    WalkPrintTransport* transport,
    const uint8_t* data,
    size_t length) {
    return transport && transport->ops && transport->ops->send &&
           transport->ops->send(transport, data, length);
}

static inline bool walkprint_transport_is_connected(const WalkPrintTransport* transport) {
    return transport && transport->ops && transport->ops->is_connected &&
           transport->ops->is_connected(transport);
}

static inline bool walkprint_transport_discover_printer(WalkPrintTransport* transport) {
    return transport && transport->ops && transport->ops->discover_printer &&
           transport->ops->discover_printer(transport);
}

static inline bool walkprint_transport_scan_wifi(WalkPrintTransport* transport) {
    return transport && transport->ops && transport->ops->scan_wifi &&
           transport->ops->scan_wifi(transport);
}
