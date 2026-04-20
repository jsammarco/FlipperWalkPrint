#pragma once

#include "walkprint_config.h"
#include "walkprint_protocol.h"
#include "walkprint_transport.h"

#include <furi.h>
#include <dialogs/dialogs.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <notification/notification.h>
#include <storage/storage.h>

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
    WalkPrintScreenConfirmBmp,
    WalkPrintScreenAbout,
} WalkPrintScreen;

typedef enum {
    WalkPrintInputModeMessage = 0,
    WalkPrintInputModeQrContent,
    WalkPrintInputModeQrFilename,
    WalkPrintInputModeBarcodeContent,
    WalkPrintInputModeBarcodeFilename,
} WalkPrintInputMode;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    NotificationApp* notifications;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    TextInput* text_input;
    FuriTimer* text_print_timer;
    FuriString* image_path;
    FuriString* text_path;
    FuriString* pending_bmp_output_path;
    WalkPrintProtocol protocol;
    WalkPrintTransport transport;
    WalkPrintScreen screen;
    WalkPrintInputMode input_mode;
    bool running;
    size_t main_menu_index;
    size_t settings_index;
    size_t wifi_results_index;
    size_t address_cursor;
    bool address_edit_dirty;
    uint8_t density;
    uint8_t font_size;
    uint8_t char_spacing;
    WalkPrintFontFamily font_family;
    WalkPrintOrientation orientation;
    char printer_address[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];
    char address_edit_buffer[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];
    char compose_message[33];
    char generated_text[64];
    char generated_filename[32];
    char* text_print_buffer;
    size_t text_print_offset;
    size_t text_print_chars_per_line;
    size_t text_print_lines_per_page;
    size_t text_print_page_count;
    bool text_print_active;
    bool text_print_cancel_requested;
    bool busy_cancelable;
    bool bmp_confirm_save_before_print;
    char status_line[WALKPRINT_STATUS_TEXT_SIZE];
    char detail_line[WALKPRINT_STATUS_TEXT_SIZE];
    char raw_frame_preview[WALKPRINT_HEX_PREVIEW_SIZE];
} WalkPrintApp;

int32_t walkprint_app(void* p);

void walkprint_app_request_redraw(WalkPrintApp* app);
void walkprint_app_set_status(WalkPrintApp* app, const char* status, const char* detail);
void walkprint_app_show_keyboard(WalkPrintApp* app);
void walkprint_app_begin_qr_input(WalkPrintApp* app);
void walkprint_app_begin_barcode_input(WalkPrintApp* app);
void walkprint_app_queue_ping_bridge(WalkPrintApp* app);
void walkprint_app_queue_discover_printer(WalkPrintApp* app);
void walkprint_app_queue_connect(WalkPrintApp* app);
void walkprint_app_queue_send_message(WalkPrintApp* app);
void walkprint_app_queue_send_text_file(WalkPrintApp* app);
void walkprint_app_queue_send_bmp(WalkPrintApp* app);
void walkprint_app_queue_send_feed(WalkPrintApp* app);
void walkprint_app_queue_scan_wifi(WalkPrintApp* app);
void walkprint_app_reset_transport(WalkPrintApp* app);
bool walkprint_app_connect(WalkPrintApp* app);
void walkprint_app_disconnect(WalkPrintApp* app);
bool walkprint_app_ping_bridge(WalkPrintApp* app);
bool walkprint_app_send_message(WalkPrintApp* app);
bool walkprint_app_select_text_file(WalkPrintApp* app);
bool walkprint_app_send_text_file(WalkPrintApp* app);
bool walkprint_app_select_bmp(WalkPrintApp* app);
void walkprint_app_show_bmp_confirm(WalkPrintApp* app, bool save_before_print);
void walkprint_app_prepare_generated_bmp(
    WalkPrintApp* app,
    const char* temp_bmp_path,
    const char* save_bmp_path,
    const char* label);
bool walkprint_app_confirm_bmp(WalkPrintApp* app);
bool walkprint_app_send_bmp(WalkPrintApp* app);
bool walkprint_app_send_feed(WalkPrintApp* app);
bool walkprint_app_discover_printer(WalkPrintApp* app);
bool walkprint_app_scan_wifi(WalkPrintApp* app);
void walkprint_app_request_cancel(WalkPrintApp* app);
void walkprint_app_adjust_density(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_font_size(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_char_spacing(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_font_family(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_orientation(WalkPrintApp* app, int8_t delta);
bool walkprint_app_has_printer_address(const WalkPrintApp* app);
const char* walkprint_app_printer_address_label(const WalkPrintApp* app);
void walkprint_app_begin_address_edit(WalkPrintApp* app);
void walkprint_app_commit_address_edit(WalkPrintApp* app);
void walkprint_app_move_address_cursor(WalkPrintApp* app, int8_t delta);
void walkprint_app_adjust_address_char(WalkPrintApp* app, int8_t delta);
const char* walkprint_app_connection_label(const WalkPrintApp* app);
