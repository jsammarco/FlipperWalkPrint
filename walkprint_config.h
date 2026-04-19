#pragma once

#include <stddef.h>
#include <stdint.h>

#define WALKPRINT_APP_NAME "WalkPrint"
#define WALKPRINT_APP_VERSION "0.1"

#define WALKPRINT_PRINTER_ADDRESS_STR_SIZE 18
#define WALKPRINT_STATUS_TEXT_SIZE 48
#define WALKPRINT_HEX_PREVIEW_SIZE 96
#define WALKPRINT_FRAME_MAX_SIZE 5000
#define WALKPRINT_MENU_VISIBLE_ITEMS 4
#define WALKPRINT_EVENT_QUEUE_SIZE 8
#define WALKPRINT_PATH_MAX_SIZE 256

#define WALKPRINT_DENSITY_MIN 0
#define WALKPRINT_DENSITY_MAX 40
#define WALKPRINT_DENSITY_DEFAULT 20

#define WALKPRINT_PRINTER_ADDRESS_TEMPLATE "00:00:00:00:00:00"
#define WALKPRINT_SETTINGS_PATH "/ext/walkprint.cfg"

/*
 * Replace these placeholder frames with the real WalkPrint / YHK packets once
 * you have the confirmed command bytes and checksum / session rules.
 */
static const uint8_t walkprint_config_init_frame[] = {0x1B, 0x40};
static const uint8_t walkprint_config_start_print_frame[] = {0x1D, 0x49, 0xF0, 0x19};
static const uint8_t walkprint_config_end_print_frame[] = {0x0A, 0x0A, 0x0A, 0x0A};
static const uint8_t walkprint_config_feed_frame[] = {0x0A, 0x0A};
static const uint8_t walkprint_config_raw_frame[] = {0x1D, 0x67, 0x69};

#define WALKPRINT_INIT_FRAME_LEN (sizeof(walkprint_config_init_frame))
#define WALKPRINT_START_PRINT_FRAME_LEN (sizeof(walkprint_config_start_print_frame))
#define WALKPRINT_END_PRINT_FRAME_LEN (sizeof(walkprint_config_end_print_frame))
#define WALKPRINT_FEED_FRAME_LEN (sizeof(walkprint_config_feed_frame))
#define WALKPRINT_RAW_FRAME_LEN (sizeof(walkprint_config_raw_frame))

/*
 * These demo lines are routed through the protocol builder as text payload.
 * Replace with the real raster/image path once you add the WalkPrint image
 * encoding and packet framing rules.
 */
static const char* const walkprint_demo_receipt_lines[] = {
    "WalkPrint Prototype",
    "Classic BT Serial Test",
    "Density placeholder active",
    "Replace with real raster bytes",
};

#define WALKPRINT_DEMO_RECEIPT_LINE_COUNT (sizeof(walkprint_demo_receipt_lines) / sizeof(walkprint_demo_receipt_lines[0]))
