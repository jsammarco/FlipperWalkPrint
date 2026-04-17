#pragma once

#include "walkprint_config.h"
#include "walkprint_protocol.h"
#include "walkprint_transport.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WalkPrintScreenMainMenu = 0,
    WalkPrintScreenBusy,
    WalkPrintScreenSettings,
    WalkPrintScreenWifiResults,
    WalkPrintScreenEditAddress,
    WalkPrintScreenEditMessage,
    WalkPrintScreenAbout,
} WalkPrintScreen;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    TextInput* text_input;
    WalkPrintProtocol protocol;
    WalkPrintTransport transport;
    WalkPrintScreen screen;
    bool running;
    size_t main_menu_index;
    size_t settings_index;
    size_t wifi_results_index;
    size_t address_cursor;
    uint8_t density;
    uint8_t font_size;
    uint8_t char_spacing;
    WalkPrintFontFamily font_family;
    WalkPrintOrientation orientation;
    char printer_address[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];
    char compose_message[33];
    char status_line[WALKPRINT_STATUS_TEXT_SIZE];
    char detail_line[WALKPRINT_STATUS_TEXT_SIZE];
    char raw_frame_preview[WALKPRINT_HEX_PREVIEW_SIZE];
} WalkPrintApp;

int32_t walkprint_app(void* p);

void walkprint_app_request_redraw(WalkPrintApp* app);
void walkprint_app_set_status(WalkPrintApp* app, const char* status, const char* detail);
void walkprint_app_show_keyboard(WalkPrintApp* app);
void walkprint_app_queue_ping_bridge(WalkPrintApp* app);
void walkprint_app_queue_discover_printer(WalkPrintApp* app);
void walkprint_app_queue_connect(WalkPrintApp* app);
void walkprint_app_queue_send_message(WalkPrintApp* app);
void walkprint_app_queue_send_feed(WalkPrintApp* app);
void walkprint_app_queue_scan_wifi(WalkPrintApp* app);
void walkprint_app_reset_transport(WalkPrintApp* app);
bool walkprint_app_connect(WalkPrintApp* app);
void walkprint_app_disconnect(WalkPrintApp* app);
bool walkprint_app_ping_bridge(WalkPrintApp* app);
bool walkprint_app_send_message(WalkPrintApp* app);
bool walkprint_app_send_feed(WalkPrintApp* app);
bool walkprint_app_discover_printer(WalkPrintApp* app);
bool walkprint_app_scan_wifi(WalkPrintApp* app);
void walkprint_app_adjust_density(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_font_size(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_char_spacing(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_font_family(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_orientation(WalkPrintApp* app, int8_t delta);
void walkprint_app_move_address_cursor(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_address_char(WalkPrintApp* app, int8_t delta);
const char* walkprint_app_connection_label(const WalkPrintApp* app);
