#pragma once

#include "walkprint_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool initialized;
} WalkPrintProtocol;

typedef struct {
    uint8_t data[WALKPRINT_FRAME_MAX_SIZE];
    size_t length;
} WalkPrintFrame;

void walkprint_protocol_init(WalkPrintProtocol* protocol);

bool walkprint_protocol_build_test_receipt(
    WalkPrintProtocol* protocol,
    const char* const* lines,
    size_t line_count,
    uint8_t density,
    WalkPrintFrame* out_frame);

bool walkprint_protocol_build_message_receipt(
    WalkPrintProtocol* protocol,
    const char* message,
    uint8_t density,
    uint8_t font_scale,
    WalkPrintFrame* out_frame);

bool walkprint_protocol_build_feed(
    WalkPrintProtocol* protocol,
    uint8_t line_count,
    WalkPrintFrame* out_frame);

bool walkprint_protocol_build_raw(
    WalkPrintProtocol* protocol,
    const uint8_t* raw_data,
    size_t raw_length,
    WalkPrintFrame* out_frame);

bool walkprint_protocol_send_frame(
    WalkPrintProtocol* protocol,
    WalkPrintTransport* transport,
    const WalkPrintFrame* frame);
