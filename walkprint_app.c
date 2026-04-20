#include "walkprint_app.h"

#include "app_ui.h"
#include "walkprint_debug.h"

#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALKPRINT_SETTINGS_MAGIC 0x57505431UL
#define WALKPRINT_SETTINGS_VERSION 1U
#define WALKPRINT_BMP_MAGIC 0x4D42U
#define WALKPRINT_PRINTER_BITMAP_WIDTH 384U
#define WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES (WALKPRINT_PRINTER_BITMAP_WIDTH / 8U)
#define WALKPRINT_BMP_MAX_ROW_SIZE (WALKPRINT_PRINTER_BITMAP_WIDTH * 4U)
#define WALKPRINT_BMP_RASTER_CHUNK_ROWS 20U
#define WALKPRINT_BMP_RASTER_CHUNK_SIZE \
    (WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES * WALKPRINT_BMP_RASTER_CHUNK_ROWS)
#define WALKPRINT_BMP_THRESHOLD_BASE 128U
#define WALKPRINT_TEXT_PAGE_BUFFER_SIZE 512U
#define WALKPRINT_TEXT_PRINT_PAGE_DELAY_MS 100U
#define WALKPRINT_GENERATED_QR_TEXT_MAX 53U
#define WALKPRINT_GENERATED_BARCODE_TEXT_MAX 24U
#define WALKPRINT_QR_VERSION 3U
#define WALKPRINT_QR_SIZE 29U
#define WALKPRINT_QR_DATA_CODEWORDS 55U
#define WALKPRINT_QR_ECC_CODEWORDS 15U
#define WALKPRINT_QR_TOTAL_CODEWORDS 70U
#define WALKPRINT_QR_MASK 0U
#define WALKPRINT_BARCODE_QUIET_MODULES 10U
#define WALKPRINT_BARCODE_HEIGHT 140U
#define WALKPRINT_GENERATED_QR_TEMP_PATH STORAGE_EXT_PATH_PREFIX "/.walkprint_qr_tmp.bmp"
#define WALKPRINT_GENERATED_QR_SAVE_PATH STORAGE_EXT_PATH_PREFIX "/walkprint_qr.bmp"
#define WALKPRINT_GENERATED_BARCODE_TEMP_PATH STORAGE_EXT_PATH_PREFIX "/.walkprint_barcode_tmp.bmp"
#define WALKPRINT_GENERATED_BARCODE_SAVE_PATH STORAGE_EXT_PATH_PREFIX "/walkprint_barcode.bmp"

typedef enum {
    WalkPrintViewMain = 0,
    WalkPrintViewKeyboard,
} WalkPrintViewId;

typedef enum {
    WalkPrintCustomEventPingBridge = 1,
    WalkPrintCustomEventDiscoverPrinter,
    WalkPrintCustomEventConnect,
    WalkPrintCustomEventPrintMessage,
    WalkPrintCustomEventPrintTextFile,
    WalkPrintCustomEventPrintBmp,
    WalkPrintCustomEventFeedPaper,
    WalkPrintCustomEventScanWifi,
} WalkPrintCustomEvent;

typedef struct {
    WalkPrintApp* app;
} WalkPrintMainViewModel;

typedef struct {
    uint32_t pixel_data_offset;
    uint32_t dib_size;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride;
    uint16_t bits_per_pixel;
    uint16_t planes;
    uint32_t compression;
    uint32_t colors_used;
    bool top_down;
} WalkPrintBmpInfo;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t density;
    uint8_t font_size;
    uint8_t char_spacing;
    uint8_t font_family;
    uint8_t orientation;
    char printer_address[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];
    char compose_message[33];
    char image_path[WALKPRINT_PATH_MAX_SIZE];
} WalkPrintSavedSettings;

typedef struct {
    uint8_t size;
    uint8_t modules[WALKPRINT_QR_SIZE * WALKPRINT_QR_SIZE];
    uint8_t is_function[WALKPRINT_QR_SIZE * WALKPRINT_QR_SIZE];
} WalkPrintQrMatrix;

static uint16_t walkprint_app_read_le16(const uint8_t* bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t walkprint_app_read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static int32_t walkprint_app_read_le32_signed(const uint8_t* bytes) {
    return (int32_t)walkprint_app_read_le32(bytes);
}

static void walkprint_app_copy_text(char* dst, size_t dst_size, const char* src) {
    if(!dst || dst_size == 0) {
        return;
    }

    if(!src) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static bool walkprint_app_load_settings(WalkPrintApp* app);
static bool walkprint_app_save_settings(WalkPrintApp* app);
static void walkprint_app_update_led(WalkPrintApp* app);
static void walkprint_app_show_text_input(
    WalkPrintApp* app,
    WalkPrintInputMode mode,
    const char* header,
    char* buffer,
    size_t buffer_size,
    const char* status);
static bool walkprint_app_copy_file(WalkPrintApp* app, const char* source_path, const char* target_path);
static bool walkprint_app_generate_qr_bmp(WalkPrintApp* app, const char* text, const char* output_path);
static bool walkprint_app_generate_barcode_bmp(
    WalkPrintApp* app,
    const char* text,
    const char* output_path);
static bool walkprint_app_send_message_text(WalkPrintApp* app, const char* message, const char* success_status);
static bool walkprint_app_load_text_file_contents(
    WalkPrintApp* app,
    const char* path,
    char** buffer_out);
static size_t walkprint_app_text_chars_per_line(const WalkPrintApp* app);
static size_t walkprint_app_text_lines_per_page(const WalkPrintApp* app);
static bool walkprint_app_build_text_page(
    const char* text,
    size_t* offset_io,
    size_t chars_per_line,
    size_t lines_per_page,
    char* page_buffer,
    size_t page_buffer_size,
    bool* has_more);
static void walkprint_app_schedule_text_print_page(WalkPrintApp* app);
static void walkprint_app_clear_text_print_job(WalkPrintApp* app);
static void walkprint_app_text_print_timer_callback(void* context);

static const char* const walkprint_code128_patterns[106] = {
    "11011001100", "11001101100", "11001100110", "10010011000", "10010001100", "10001001100",
    "10011001000", "10011000100", "10001100100", "11001001000", "11001000100", "11000100100",
    "10110011100", "10011011100", "10011001110", "10111001100", "10011101100", "10011100110",
    "11001110010", "11001011100", "11001001110", "11011100100", "11001110100", "11101101110",
    "11101001100", "11100101100", "11100100110", "11101100100", "11100110100", "11100110010",
    "11011011000", "11011000110", "11000110110", "10100011000", "10001011000", "10001000110",
    "10110001000", "10001101000", "10001100010", "11010001000", "11000101000", "11000100010",
    "10110111000", "10110001110", "10001101110", "10111011000", "10111000110", "10001110110",
    "11101110110", "11010001110", "11000101110", "11011101000", "11011100010", "11011101110",
    "11101011000", "11101000110", "11100010110", "11101101000", "11101100010", "11100011010",
    "11101111010", "11001000010", "11110001010", "10100110000", "10100001100", "10010110000",
    "10010000110", "10000101100", "10000100110", "10110010000", "10110000100", "10011010000",
    "10011000010", "10000110100", "10000110010", "11000010010", "11001010000", "11110111010",
    "11000010100", "10001111010", "10100111100", "10010111100", "10010011110", "10111100100",
    "10011110100", "10011110010", "11110100100", "11110010100", "11110010010", "11011011110",
    "11011110110", "11110110110", "10101111000", "10100011110", "10001011110", "10111101000",
    "10111100010", "11110101000", "11110100010", "10111011110", "10111101110", "11101011110",
    "11110101110", "11010000100", "11010010000", "11010011100",
};

static const char walkprint_code128_stop_pattern[] = "1100011101011";

static void walkprint_app_queue_busy_action(
    WalkPrintApp* app,
    uint32_t event,
    const char* status,
    const char* detail,
    bool cancelable) {
    if(!app || !app->view_dispatcher) {
        return;
    }

    app->text_print_cancel_requested = false;
    app->busy_cancelable = cancelable;
    walkprint_app_set_status(app, status, detail);
    app->screen = WalkPrintScreenBusy;
    view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewMain);
    walkprint_app_request_redraw(app);
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static bool walkprint_app_require_connection(WalkPrintApp* app, const char* action_name) {
    if(walkprint_transport_is_connected(&app->transport)) {
        return true;
    }

    walkprint_app_set_status(app, "Not connected", action_name);
    return false;
}

static void walkprint_app_update_led(WalkPrintApp* app) {
    if(!app || !app->notifications) {
        return;
    }

    if(walkprint_transport_is_connected(&app->transport)) {
        notification_internal_message(app->notifications, &sequence_set_only_green_255);
    } else {
        notification_internal_message(app->notifications, &sequence_reset_rgb);
    }
}

static void walkprint_app_seed_defaults(WalkPrintApp* app) {
    app->screen = WalkPrintScreenMainMenu;
    app->input_mode = WalkPrintInputModeMessage;
    app->running = true;
    app->main_menu_index = 0;
    app->settings_index = 0;
    app->wifi_results_index = 0;
    app->address_cursor = 0;
    app->address_edit_dirty = false;
    app->density = WALKPRINT_DENSITY_DEFAULT;
    app->font_size = 3;
    app->char_spacing = 2;
    app->font_family = WalkPrintFontFamilyClassic;
    app->orientation = WalkPrintOrientationUpsideDown;
    app->text_print_buffer = NULL;
    app->text_print_offset = 0U;
    app->text_print_chars_per_line = 0U;
    app->text_print_lines_per_page = 0U;
    app->text_print_page_count = 0U;
    app->text_print_active = false;
    app->text_print_cancel_requested = false;
    app->busy_cancelable = false;
    app->bmp_confirm_save_before_print = false;

    app->printer_address[0] = '\0';
    app->address_edit_buffer[0] = '\0';
    walkprint_app_copy_text(app->compose_message, sizeof(app->compose_message), "HELLO");
    app->generated_text[0] = '\0';
    walkprint_debug_format_hex_preview(
        walkprint_config_raw_frame,
        WALKPRINT_RAW_FRAME_LEN,
        app->raw_frame_preview,
        sizeof(app->raw_frame_preview));
    if(app->image_path) {
        furi_string_set(app->image_path, STORAGE_EXT_PATH_PREFIX);
    }
    if(app->pending_bmp_output_path) {
        furi_string_set(app->pending_bmp_output_path, STORAGE_EXT_PATH_PREFIX "/generated.bmp");
    }
    walkprint_app_set_status(app, "Ready", "Discover printer to save MAC");
}

static void walkprint_app_main_view_draw(Canvas* canvas, void* model) {
    WalkPrintMainViewModel* view_model = model;

    if(!view_model || !view_model->app) {
        return;
    }

    app_ui_draw(canvas, view_model->app);
}

static bool walkprint_app_main_view_input(InputEvent* input_event, void* context) {
    WalkPrintApp* app = context;

    if(!app || !input_event) {
        return false;
    }

    app_ui_handle_input(app, input_event);
    if(!app->running && app->view_dispatcher) {
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

static void walkprint_app_text_input_done(void* context) {
    WalkPrintApp* app = context;

    if(!app) {
        return;
    }

    app->screen = WalkPrintScreenMainMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewMain);
    if(app->input_mode == WalkPrintInputModeMessage) {
        walkprint_app_save_settings(app);
        walkprint_app_queue_send_message(app);
    } else if(app->input_mode == WalkPrintInputModeQr) {
        if(walkprint_app_generate_qr_bmp(
               app, app->generated_text, WALKPRINT_GENERATED_QR_TEMP_PATH)) {
            walkprint_app_prepare_generated_bmp(
                app,
                WALKPRINT_GENERATED_QR_TEMP_PATH,
                WALKPRINT_GENERATED_QR_SAVE_PATH,
                "QR ready to save");
        }
    } else if(app->input_mode == WalkPrintInputModeBarcode) {
        if(walkprint_app_generate_barcode_bmp(
               app, app->generated_text, WALKPRINT_GENERATED_BARCODE_TEMP_PATH)) {
            walkprint_app_prepare_generated_bmp(
                app,
                WALKPRINT_GENERATED_BARCODE_TEMP_PATH,
                WALKPRINT_GENERATED_BARCODE_SAVE_PATH,
                "Barcode ready to save");
        }
    }
}

static bool walkprint_app_handle_custom_event(void* context, uint32_t event) {
    WalkPrintApp* app = context;
    bool handled = true;

    if(!app) {
        return false;
    }

    switch(event) {
    case WalkPrintCustomEventPingBridge:
        walkprint_app_ping_bridge(app);
        break;
    case WalkPrintCustomEventDiscoverPrinter:
        walkprint_app_discover_printer(app);
        break;
    case WalkPrintCustomEventConnect:
        walkprint_app_connect(app);
        break;
    case WalkPrintCustomEventPrintMessage:
        walkprint_app_send_message(app);
        break;
    case WalkPrintCustomEventPrintTextFile:
        walkprint_app_send_text_file(app);
        break;
    case WalkPrintCustomEventPrintBmp:
        walkprint_app_send_bmp(app);
        break;
    case WalkPrintCustomEventFeedPaper:
        walkprint_app_send_feed(app);
        break;
    case WalkPrintCustomEventScanWifi:
        handled = walkprint_app_scan_wifi(app);
        if(handled) {
            app->screen = WalkPrintScreenWifiResults;
            app->wifi_results_index = 0;
            walkprint_app_set_status(app, "WiFi Results", app->transport.last_response);
        }
        break;
    default:
        handled = false;
        break;
    }

    if(handled && app->screen != WalkPrintScreenWifiResults && !app->text_print_active) {
        app->screen = WalkPrintScreenMainMenu;
        app->busy_cancelable = false;
        walkprint_app_request_redraw(app);
    }

    return handled;
}

static bool walkprint_app_navigation_event(void* context) {
    WalkPrintApp* app = context;

    if(!app) {
        return false;
    }

    if(app->screen == WalkPrintScreenEditMessage) {
        app->screen = WalkPrintScreenMainMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewMain);
        if(app->input_mode == WalkPrintInputModeMessage) {
            walkprint_app_set_status(app, "Ready", app->compose_message);
        } else if(app->input_mode == WalkPrintInputModeQr) {
            walkprint_app_set_status(app, "QR canceled", "Back to menu");
        } else {
            walkprint_app_set_status(app, "Barcode canceled", "Back to menu");
        }
        return true;
    }

    return false;
}

static void walkprint_app_show_text_input(
    WalkPrintApp* app,
    WalkPrintInputMode mode,
    const char* header,
    char* buffer,
    size_t buffer_size,
    const char* status) {
    if(!app || !app->text_input || !app->view_dispatcher || !buffer || buffer_size < 2U) {
        return;
    }

    app->input_mode = mode;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_minimum_length(app->text_input, 1U);
    text_input_set_result_callback(
        app->text_input, walkprint_app_text_input_done, app, buffer, buffer_size, false);
    app->screen = WalkPrintScreenEditMessage;
    walkprint_app_set_status(app, status, "Use keyboard to type");
    view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewKeyboard);
}

void walkprint_app_request_redraw(WalkPrintApp* app) {
    WalkPrintMainViewModel* model;

    if(!app || !app->main_view) {
        return;
    }

    model = view_get_model(app->main_view);
    if(!model) {
        return;
    }

    view_commit_model(app->main_view, true);
}

void walkprint_app_set_status(WalkPrintApp* app, const char* status, const char* detail) {
    if(!app) {
        return;
    }

    walkprint_app_copy_text(app->status_line, sizeof(app->status_line), status);
    walkprint_app_copy_text(app->detail_line, sizeof(app->detail_line), detail);
    walkprint_debug_log_info("Status: %s | %s", app->status_line, app->detail_line);
}

static void walkprint_app_clear_text_print_job(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    if(app->text_print_timer) {
        furi_timer_stop(app->text_print_timer);
    }

    free(app->text_print_buffer);
    app->text_print_buffer = NULL;
    app->text_print_offset = 0U;
    app->text_print_chars_per_line = 0U;
    app->text_print_lines_per_page = 0U;
    app->text_print_page_count = 0U;
    app->text_print_active = false;
    app->text_print_cancel_requested = false;
    app->busy_cancelable = false;
}

static void walkprint_app_text_print_timer_callback(void* context) {
    WalkPrintApp* app = context;

    if(!app || !app->view_dispatcher) {
        return;
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, WalkPrintCustomEventPrintTextFile);
}

static void walkprint_app_schedule_text_print_page(WalkPrintApp* app) {
    if(!app || !app->text_print_timer) {
        return;
    }

    furi_timer_start(app->text_print_timer, furi_ms_to_ticks(WALKPRINT_TEXT_PRINT_PAGE_DELAY_MS));
}

void walkprint_app_request_cancel(WalkPrintApp* app) {
    if(!app || !app->busy_cancelable) {
        return;
    }

    app->text_print_cancel_requested = true;
    walkprint_app_set_status(app, "Canceling TXT", "Stopping after current page");
    walkprint_app_request_redraw(app);
}

void walkprint_app_show_keyboard(WalkPrintApp* app) {
    walkprint_app_show_text_input(
        app,
        WalkPrintInputModeMessage,
        "Message to print",
        app->compose_message,
        sizeof(app->compose_message),
        "Editing message");
}

void walkprint_app_begin_qr_input(WalkPrintApp* app) {
    walkprint_app_show_text_input(
        app,
        WalkPrintInputModeQr,
        "QR text",
        app->generated_text,
        sizeof(app->generated_text),
        "Editing QR");
}

void walkprint_app_begin_barcode_input(WalkPrintApp* app) {
    walkprint_app_show_text_input(
        app,
        WalkPrintInputModeBarcode,
        "Barcode text",
        app->generated_text,
        sizeof(app->generated_text),
        "Editing barcode");
}

void walkprint_app_queue_ping_bridge(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPingBridge, "Checking bridge", "Pinging ESP32 UART bridge", false);
}

void walkprint_app_queue_discover_printer(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app,
        WalkPrintCustomEventDiscoverPrinter,
        "Discovering printer",
        "Scanning Bluetooth devices",
        false);
}

void walkprint_app_queue_connect(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventConnect, "Connecting printer", "Opening Bluetooth link", false);
}

void walkprint_app_queue_send_message(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPrintMessage, "Printing message", "Rendering custom text", false);
}

void walkprint_app_queue_send_text_file(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPrintTextFile, "Printing TXT", "Loading text from SD", true);
}

void walkprint_app_queue_send_bmp(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPrintBmp, "Printing BMP", "Streaming SD bitmap", false);
}

void walkprint_app_queue_send_feed(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventFeedPaper, "Feeding paper", "Sending feed command", false);
}

void walkprint_app_queue_scan_wifi(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventScanWifi, "Scanning WiFi", "Querying bridge radios", false);
}

void walkprint_app_reset_transport(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_transport_disconnect(&app->transport);

    if(!walkprint_transport_init(
           &app->transport, walkprint_transport_live_ops(), app->printer_address)) {
        app->transport.notifications = app->notifications;
        walkprint_app_update_led(app);
        walkprint_app_set_status(app, "Transport init failed", "Check config");
        return;
    }

    app->transport.notifications = app->notifications;
    walkprint_app_update_led(app);

    walkprint_app_set_status(
        app,
        app->transport.bridge_ready ? "Bridge ready" : "Bridge offline",
        app->transport.last_response);
}

bool walkprint_app_connect(WalkPrintApp* app) {
    if(!app) {
        return false;
    }

    if(walkprint_transport_is_connected(&app->transport)) {
        walkprint_app_disconnect(app);
        walkprint_app_set_status(app, "Disconnected", walkprint_transport_name());
        return true;
    }

    if(!walkprint_app_has_printer_address(app)) {
        walkprint_app_set_status(app, "No printer saved", "Run discovery or set MAC");
        return false;
    }

    if(!app->transport.bridge_ready) {
        walkprint_app_reset_transport(app);
        if(!app->transport.bridge_ready) {
            walkprint_app_set_status(app, "Bridge offline", app->transport.last_response);
            return false;
        }
    }

    if(walkprint_transport_connect(&app->transport)) {
        walkprint_app_update_led(app);
        walkprint_app_save_settings(app);
        walkprint_app_set_status(
            app,
            "Connected",
            app->transport.printer_name[0] != '\0' ? app->transport.printer_name :
                                                     app->printer_address);
        return true;
    }

    walkprint_app_set_status(app, "Connect failed", app->transport.last_response);
    walkprint_app_update_led(app);
    return false;
}

void walkprint_app_disconnect(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_transport_disconnect(&app->transport);
    walkprint_app_update_led(app);
}

bool walkprint_app_ping_bridge(WalkPrintApp* app) {
    if(!app) {
        return false;
    }

    walkprint_app_reset_transport(app);
    if(app->transport.bridge_ready) {
        walkprint_app_set_status(app, "Bridge ready", app->transport.last_response);
        return true;
    }

    walkprint_app_set_status(app, "Bridge offline", app->transport.last_response);
    return false;
}

static bool walkprint_app_send_message_text(
    WalkPrintApp* app,
    const char* message,
    const char* success_status) {
    WalkPrintFrame frame;

    if(!app || !walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    if(!walkprint_protocol_build_message_receipt(
           &app->protocol,
           message,
           app->density,
           app->font_size,
           app->font_family,
           app->char_spacing,
           app->orientation,
           &frame)) {
        walkprint_app_set_status(app, "Message build failed", "See logs");
        return false;
    }

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_init_frame, WALKPRINT_INIT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_transport_send(
           &app->transport,
           walkprint_config_start_print_frame,
           WALKPRINT_START_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_protocol_send_frame(&app->protocol, &app->transport, &frame)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_end_print_frame, WALKPRINT_END_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        return false;
    }

    walkprint_app_set_status(app, success_status, message);
    return true;
}

bool walkprint_app_send_message(WalkPrintApp* app) {
    if(!app) {
        return false;
    }

    return walkprint_app_send_message_text(app, app->compose_message, "Message printed");
}

static bool walkprint_app_load_text_file_contents(
    WalkPrintApp* app,
    const char* path,
    char** buffer_out) {
    File* file = NULL;
    char* buffer = NULL;
    bool ok = false;
    size_t buffer_capacity = 0;
    size_t used = 0;
    uint8_t chunk[64];

    if(!app || !app->storage || !path || !buffer_out) {
        return false;
    }

    *buffer_out = NULL;

    file = storage_file_alloc(app->storage);
    if(!file) {
        walkprint_app_set_status(app, "TXT open failed", "File alloc failed");
        return false;
    }

    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        walkprint_app_set_status(app, "TXT open failed", "Open from SD failed");
        goto cleanup;
    }

    buffer_capacity = 256U;
    buffer = malloc(buffer_capacity);
    if(!buffer) {
        walkprint_app_set_status(app, "TXT read failed", "Memory alloc failed");
        goto cleanup;
    }

    while(true) {
        uint16_t read_count = storage_file_read(file, chunk, sizeof(chunk));
        if(read_count == 0U) {
            break;
        }

        for(uint16_t i = 0; i < read_count; i++) {
            char ch = (char)chunk[i];

            if(ch == '\r') {
                continue;
            }

            if(ch == '\t') {
                ch = ' ';
            }

            if(((uint8_t)ch < 0x20U) && ch != '\n') {
                continue;
            }

            if((uint8_t)ch >= 0x80U) {
                ch = ' ';
            }

            if(ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - 32);
            }

            if(used + 2U > buffer_capacity) {
                size_t next_capacity = buffer_capacity * 2U;
                char* next_buffer = realloc(buffer, next_capacity);

                if(!next_buffer) {
                    walkprint_app_set_status(app, "TXT read failed", "Out of memory");
                    goto cleanup;
                }

                buffer = next_buffer;
                buffer_capacity = next_capacity;
            }

            buffer[used++] = ch;
        }
    }

    while(used > 0U && (buffer[used - 1U] == '\n' || buffer[used - 1U] == ' ')) {
        used--;
    }

    buffer[used] = '\0';

    if(used == 0U) {
        walkprint_app_set_status(app, "TXT empty", "Choose a text file with content");
        goto cleanup;
    }

    *buffer_out = buffer;
    buffer = NULL;
    ok = true;

cleanup:
    free(buffer);
    if(file) {
        storage_file_close(file);
        storage_file_free(file);
    }

    return ok;
}

static size_t walkprint_app_text_chars_per_line(const WalkPrintApp* app) {
    size_t glyph_width;

    if(!app) {
        return 1U;
    }

    glyph_width = (5U * app->font_size) + 1U + app->char_spacing;
    if(glyph_width == 0U) {
        return 1U;
    }

    glyph_width = WALKPRINT_PRINTER_BITMAP_WIDTH / glyph_width;
    return glyph_width > 0U ? glyph_width : 1U;
}

static size_t walkprint_app_text_lines_per_page(const WalkPrintApp* app) {
    size_t glyph_height;
    size_t line_height;
    size_t y_offset = 2U;
    size_t lines = 0U;

    if(!app) {
        return 1U;
    }

    glyph_height = 7U * app->font_size;
    line_height = glyph_height + 4U;

    while((y_offset + glyph_height) <= 96U) {
        lines++;
        y_offset += line_height;
    }

    return lines > 0U ? lines : 1U;
}

static bool walkprint_app_build_text_page(
    const char* text,
    size_t* offset_io,
    size_t chars_per_line,
    size_t lines_per_page,
    char* page_buffer,
    size_t page_buffer_size,
    bool* has_more) {
    size_t src;
    size_t dst = 0U;
    size_t lines = 0U;

    if(!text || !offset_io || !page_buffer || page_buffer_size < 2U || !has_more) {
        return false;
    }

    src = *offset_io;
    page_buffer[0] = '\0';
    *has_more = false;

    while(text[src] != '\0' && lines < lines_per_page) {
        size_t line_chars = 0U;

        while(text[src] == '\n' && lines < lines_per_page) {
            if(dst + 1U >= page_buffer_size) {
                break;
            }

            page_buffer[dst++] = '\n';
            src++;
            lines++;
        }

        if(lines >= lines_per_page || text[src] == '\0') {
            break;
        }

        while(text[src] != '\0' && text[src] != '\n' && line_chars < chars_per_line) {
            if(dst + 1U >= page_buffer_size) {
                break;
            }

            page_buffer[dst++] = text[src++];
            line_chars++;
        }

        while(text[src] == ' ' && line_chars >= chars_per_line) {
            src++;
        }

        if(text[src] == '\n') {
            src++;
        }

        lines++;
        if(lines < lines_per_page && text[src] != '\0') {
            if(dst + 1U >= page_buffer_size) {
                break;
            }

            page_buffer[dst++] = '\n';
        }
    }

    while(dst > 0U && page_buffer[dst - 1U] == '\n') {
        dst--;
    }

    page_buffer[dst] = '\0';
    *offset_io = src;
    *has_more = text[src] != '\0';

    return dst > 0U;
}

static bool walkprint_app_storage_read_exact(File* file, void* buffer, size_t length) {
    return file && buffer && storage_file_read(file, buffer, length) == length;
}

static bool walkprint_app_storage_write_exact(File* file, const void* buffer, size_t length) {
    return file && buffer && storage_file_write(file, buffer, length) == length;
}

static uint8_t walkprint_qr_gf_multiply(uint8_t x, uint8_t y) {
    uint8_t z = 0U;

    while(y != 0U) {
        if((y & 0x01U) != 0U) {
            z ^= x;
        }
        y >>= 1U;
        x = (uint8_t)((x << 1U) ^ ((x & 0x80U) != 0U ? 0x1DU : 0x00U));
    }

    return z;
}

static void walkprint_qr_append_bits(
    uint8_t* data,
    size_t* bit_len,
    size_t bit_capacity,
    uint32_t value,
    uint8_t bit_count) {
    if(!data || !bit_len) {
        return;
    }

    for(uint8_t i = 0; i < bit_count && *bit_len < bit_capacity; i++) {
        uint8_t bit = (uint8_t)((value >> (bit_count - 1U - i)) & 0x01U);
        size_t index = *bit_len;
        if(bit != 0U) {
            data[index / 8U] |= (uint8_t)(1U << (7U - (index % 8U)));
        }
        (*bit_len)++;
    }
}

static bool walkprint_qr_matrix_set(
    WalkPrintQrMatrix* matrix,
    int16_t x,
    int16_t y,
    bool black,
    bool is_function) {
    size_t index;

    if(!matrix || x < 0 || y < 0 || x >= matrix->size || y >= matrix->size) {
        return false;
    }

    index = (size_t)y * matrix->size + (size_t)x;
    matrix->modules[index] = black ? 1U : 0U;
    if(is_function) {
        matrix->is_function[index] = 1U;
    }
    return true;
}

static void walkprint_qr_draw_finder(WalkPrintQrMatrix* matrix, int16_t left, int16_t top) {
    for(int16_t dy = -1; dy <= 7; dy++) {
        for(int16_t dx = -1; dx <= 7; dx++) {
            int16_t x = left + dx;
            int16_t y = top + dy;
            bool in_core = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
            bool black =
                in_core &&
                (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                 ((dx >= 2 && dx <= 4) && (dy >= 2 && dy <= 4)));
            walkprint_qr_matrix_set(matrix, x, y, black, true);
        }
    }
}

static void walkprint_qr_draw_alignment(WalkPrintQrMatrix* matrix, int16_t center_x, int16_t center_y) {
    for(int16_t dy = -2; dy <= 2; dy++) {
        for(int16_t dx = -2; dx <= 2; dx++) {
            int16_t dist_x = dx < 0 ? -dx : dx;
            int16_t dist_y = dy < 0 ? -dy : dy;
            bool black = dist_x == 2 || dist_y == 2 || (dx == 0 && dy == 0);
            walkprint_qr_matrix_set(matrix, center_x + dx, center_y + dy, black, true);
        }
    }
}

static void walkprint_qr_reserve_format_bits(WalkPrintQrMatrix* matrix) {
    if(!matrix) {
        return;
    }

    for(uint8_t i = 0; i <= 5U; i++) {
        walkprint_qr_matrix_set(matrix, 8, i, false, true);
        walkprint_qr_matrix_set(matrix, i, 8, false, true);
    }
    walkprint_qr_matrix_set(matrix, 8, 7, false, true);
    walkprint_qr_matrix_set(matrix, 8, 8, false, true);
    walkprint_qr_matrix_set(matrix, 7, 8, false, true);

    for(uint8_t i = 0; i < 8U; i++) {
        walkprint_qr_matrix_set(matrix, matrix->size - 1 - i, 8, false, true);
        walkprint_qr_matrix_set(matrix, 8, matrix->size - 1 - i, false, true);
    }
}

static void walkprint_qr_draw_function_patterns(WalkPrintQrMatrix* matrix) {
    if(!matrix) {
        return;
    }

    walkprint_qr_draw_finder(matrix, 0, 0);
    walkprint_qr_draw_finder(matrix, matrix->size - 7, 0);
    walkprint_qr_draw_finder(matrix, 0, matrix->size - 7);

    for(uint8_t i = 8U; i < (uint8_t)(matrix->size - 8U); i++) {
        bool black = (i % 2U) == 0U;
        walkprint_qr_matrix_set(matrix, 6, i, black, true);
        walkprint_qr_matrix_set(matrix, i, 6, black, true);
    }

    walkprint_qr_draw_alignment(matrix, 22, 22);
    walkprint_qr_reserve_format_bits(matrix);
    walkprint_qr_matrix_set(matrix, 8, matrix->size - 8, true, true);
}

static void walkprint_qr_build_generator(uint8_t degree, uint8_t* generator) {
    uint8_t root = 1U;

    if(!generator || degree == 0U) {
        return;
    }

    memset(generator, 0, degree);
    generator[degree - 1U] = 1U;

    for(uint8_t i = 0; i < degree; i++) {
        for(uint8_t j = 0; j < degree; j++) {
            generator[j] = walkprint_qr_gf_multiply(generator[j], root);
            if(j + 1U < degree) {
                generator[j] ^= generator[j + 1U];
            }
        }
        root = walkprint_qr_gf_multiply(root, 0x02U);
    }
}

static void walkprint_qr_compute_remainder(
    const uint8_t* data,
    uint8_t data_len,
    const uint8_t* generator,
    uint8_t degree,
    uint8_t* remainder) {
    if(!data || !generator || !remainder) {
        return;
    }

    memset(remainder, 0, degree);
    for(uint8_t i = 0; i < data_len; i++) {
        uint8_t factor = data[i] ^ remainder[0];

        memmove(remainder, remainder + 1, degree - 1U);
        remainder[degree - 1U] = 0U;
        for(uint8_t j = 0; j < degree; j++) {
            remainder[j] ^= walkprint_qr_gf_multiply(generator[j], factor);
        }
    }
}

static void walkprint_qr_write_format_bits(WalkPrintQrMatrix* matrix, uint8_t mask) {
    static const uint16_t format_bits[8] = {
        0x77C4U, 0x72F3U, 0x7DAAU, 0x789DU, 0x662FU, 0x6318U, 0x6C41U, 0x6976U,
    };
    uint16_t bits;

    if(!matrix || mask >= 8U) {
        return;
    }

    bits = format_bits[mask];
    for(uint8_t i = 0; i <= 5U; i++) {
        walkprint_qr_matrix_set(matrix, 8, i, ((bits >> i) & 1U) != 0U, true);
    }
    walkprint_qr_matrix_set(matrix, 8, 7, ((bits >> 6U) & 1U) != 0U, true);
    walkprint_qr_matrix_set(matrix, 8, 8, ((bits >> 7U) & 1U) != 0U, true);
    walkprint_qr_matrix_set(matrix, 7, 8, ((bits >> 8U) & 1U) != 0U, true);
    for(uint8_t i = 9U; i < 15U; i++) {
        walkprint_qr_matrix_set(matrix, 14 - i, 8, ((bits >> i) & 1U) != 0U, true);
    }

    for(uint8_t i = 0U; i < 8U; i++) {
        walkprint_qr_matrix_set(matrix, matrix->size - 1 - i, 8, ((bits >> i) & 1U) != 0U, true);
    }
    for(uint8_t i = 8U; i < 15U; i++) {
        walkprint_qr_matrix_set(matrix, 8, matrix->size - 15 + i, ((bits >> i) & 1U) != 0U, true);
    }
}

static bool walkprint_qr_encode_text(const char* text, WalkPrintQrMatrix* matrix) {
    uint8_t data[WALKPRINT_QR_DATA_CODEWORDS];
    uint8_t generator[WALKPRINT_QR_ECC_CODEWORDS];
    uint8_t remainder[WALKPRINT_QR_ECC_CODEWORDS];
    uint8_t codewords[WALKPRINT_QR_TOTAL_CODEWORDS];
    size_t text_len;
    size_t bit_len = 0U;
    size_t bit_capacity = WALKPRINT_QR_DATA_CODEWORDS * 8U;
    size_t bit_index = 0U;

    if(!text || !matrix) {
        return false;
    }

    text_len = strlen(text);
    if(text_len == 0U || text_len > WALKPRINT_GENERATED_QR_TEXT_MAX) {
        return false;
    }

    memset(matrix, 0, sizeof(*matrix));
    matrix->size = WALKPRINT_QR_SIZE;
    walkprint_qr_draw_function_patterns(matrix);

    memset(data, 0, sizeof(data));
    walkprint_qr_append_bits(data, &bit_len, bit_capacity, 0x4U, 4U);
    walkprint_qr_append_bits(data, &bit_len, bit_capacity, (uint32_t)text_len, 8U);
    for(size_t i = 0; i < text_len; i++) {
        walkprint_qr_append_bits(data, &bit_len, bit_capacity, (uint8_t)text[i], 8U);
    }
    if(bit_len + 4U <= bit_capacity) {
        bit_len += 4U;
    } else {
        bit_len = bit_capacity;
    }
    while((bit_len % 8U) != 0U && bit_len < bit_capacity) {
        bit_len++;
    }
    for(size_t i = bit_len / 8U; i < WALKPRINT_QR_DATA_CODEWORDS; i++) {
        data[i] = (i % 2U) == 0U ? 0xECU : 0x11U;
    }

    walkprint_qr_build_generator(WALKPRINT_QR_ECC_CODEWORDS, generator);
    walkprint_qr_compute_remainder(
        data, WALKPRINT_QR_DATA_CODEWORDS, generator, WALKPRINT_QR_ECC_CODEWORDS, remainder);

    memcpy(codewords, data, sizeof(data));
    memcpy(codewords + WALKPRINT_QR_DATA_CODEWORDS, remainder, sizeof(remainder));

    for(int16_t right = matrix->size - 1; right >= 1; right -= 2) {
        bool upward;

        if(right == 6) {
            right = 5;
        }
        upward = (((right + 1) & 2U) == 0U);
        for(uint8_t vert = 0; vert < matrix->size; vert++) {
            uint8_t y = upward ? (uint8_t)(matrix->size - 1U - vert) : vert;
            for(uint8_t j = 0; j < 2U; j++) {
                uint8_t x = (uint8_t)(right - j);
                size_t index = (size_t)y * matrix->size + x;
                bool black = false;

                if(matrix->is_function[index] != 0U) {
                    continue;
                }
                if(bit_index < sizeof(codewords) * 8U) {
                    black =
                        ((codewords[bit_index / 8U] >> (7U - (bit_index % 8U))) & 0x01U) != 0U;
                    bit_index++;
                }
                if(((x + y) & 0x01U) == 0U) {
                    black = !black;
                }
                matrix->modules[index] = black ? 1U : 0U;
            }
        }
    }

    walkprint_qr_write_format_bits(matrix, WALKPRINT_QR_MASK);
    return true;
}

static void walkprint_app_write_le16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void walkprint_app_write_le32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static bool walkprint_app_open_generated_bmp(
    WalkPrintApp* app,
    const char* path,
    uint32_t height,
    File** file_out) {
    File* file;
    uint8_t file_header[14];
    uint8_t info_header[40];
    uint8_t palette[8];
    uint32_t image_size = WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES * height;
    uint32_t file_size = sizeof(file_header) + sizeof(info_header) + sizeof(palette) + image_size;

    if(!app || !app->storage || !path || !file_out || height == 0U) {
        return false;
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        walkprint_app_set_status(app, "BMP save failed", "File alloc failed");
        return false;
    }

    if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(file);
        walkprint_app_set_status(app, "BMP save failed", "Open for write failed");
        return false;
    }

    memset(file_header, 0, sizeof(file_header));
    memset(info_header, 0, sizeof(info_header));
    memset(palette, 0, sizeof(palette));

    file_header[0] = 'B';
    file_header[1] = 'M';
    walkprint_app_write_le32(&file_header[2], file_size);
    walkprint_app_write_le32(
        &file_header[10], (uint32_t)(sizeof(file_header) + sizeof(info_header) + sizeof(palette)));

    walkprint_app_write_le32(&info_header[0], sizeof(info_header));
    walkprint_app_write_le32(&info_header[4], WALKPRINT_PRINTER_BITMAP_WIDTH);
    walkprint_app_write_le32(&info_header[8], (uint32_t)(-(int32_t)height));
    walkprint_app_write_le16(&info_header[12], 1U);
    walkprint_app_write_le16(&info_header[14], 1U);
    walkprint_app_write_le32(&info_header[20], image_size);
    walkprint_app_write_le32(&info_header[24], 11811U);
    walkprint_app_write_le32(&info_header[28], 11811U);
    walkprint_app_write_le32(&info_header[32], 2U);
    walkprint_app_write_le32(&info_header[36], 2U);

    palette[0] = 0xFFU;
    palette[1] = 0xFFU;
    palette[2] = 0xFFU;
    palette[3] = 0x00U;
    palette[4] = 0x00U;
    palette[5] = 0x00U;
    palette[6] = 0x00U;
    palette[7] = 0x00U;

    if(!walkprint_app_storage_write_exact(file, file_header, sizeof(file_header)) ||
       !walkprint_app_storage_write_exact(file, info_header, sizeof(info_header)) ||
       !walkprint_app_storage_write_exact(file, palette, sizeof(palette))) {
        storage_file_close(file);
        storage_file_free(file);
        walkprint_app_set_status(app, "BMP save failed", "Header write failed");
        return false;
    }

    *file_out = file;
    return true;
}

static void walkprint_app_set_row_bit(uint8_t* row, uint32_t x) {
    row[x / 8U] |= (uint8_t)(1U << (7U - (x % 8U)));
}

static bool walkprint_app_generate_qr_bmp(WalkPrintApp* app, const char* text, const char* output_path) {
    WalkPrintQrMatrix matrix;
    File* file = NULL;
    uint32_t border = 2U;
    uint32_t total_modules;
    uint32_t scale;
    uint32_t render_size;
    uint32_t left_pad;
    uint8_t row[WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES];

    if(!app || !text || !output_path) {
        return false;
    }

    if(!walkprint_qr_encode_text(text, &matrix)) {
        walkprint_app_set_status(app, "QR build failed", "Use 1-53 ASCII chars");
        return false;
    }

    total_modules = matrix.size + border * 2U;
    scale = WALKPRINT_PRINTER_BITMAP_WIDTH / total_modules;
    if(scale == 0U) {
        walkprint_app_set_status(app, "QR build failed", "Scale too small");
        return false;
    }
    render_size = total_modules * scale;
    left_pad = (WALKPRINT_PRINTER_BITMAP_WIDTH - render_size) / 2U;

    if(!walkprint_app_open_generated_bmp(app, output_path, render_size, &file)) {
        return false;
    }

    for(uint32_t y = 0; y < render_size; y++) {
        int32_t module_y = (int32_t)(y / scale) - (int32_t)border;

        memset(row, 0, sizeof(row));
        for(uint32_t x = 0; x < render_size; x++) {
            int32_t module_x = (int32_t)(x / scale) - (int32_t)border;
            bool black = false;

            if(module_x >= 0 && module_y >= 0 && module_x < matrix.size && module_y < matrix.size) {
                black = matrix.modules[(size_t)module_y * matrix.size + (size_t)module_x] != 0U;
            }
            if(black) {
                walkprint_app_set_row_bit(row, left_pad + x);
            }
        }

        if(!walkprint_app_storage_write_exact(file, row, sizeof(row))) {
            storage_file_close(file);
            storage_file_free(file);
            walkprint_app_set_status(app, "QR save failed", "Row write failed");
            return false;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    walkprint_app_set_status(app, "QR generated", output_path);
    return true;
}

static bool walkprint_app_generate_barcode_bmp(
    WalkPrintApp* app,
    const char* text,
    const char* output_path) {
    size_t text_len;
    uint16_t checksum = 104U;
    char pattern[512];
    size_t pattern_len = 0U;
    uint32_t total_modules;
    uint32_t left_pad;
    uint32_t top_pad = 8U;
    uint32_t bottom_pad = 8U;
    File* file = NULL;
    uint8_t row[WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES];

    if(!app || !text || !output_path) {
        return false;
    }

    text_len = strlen(text);
    if(text_len == 0U || text_len > WALKPRINT_GENERATED_BARCODE_TEXT_MAX) {
        walkprint_app_set_status(app, "Barcode build failed", "Use 1-24 ASCII chars");
        return false;
    }

    for(size_t i = 0; i < text_len; i++) {
        uint8_t ch = (uint8_t)text[i];
        if(ch < 32U || ch > 126U) {
            walkprint_app_set_status(app, "Barcode build failed", "Use printable ASCII only");
            return false;
        }
    }

    memset(pattern, 0, sizeof(pattern));
    memcpy(pattern + pattern_len, walkprint_code128_patterns[104], 11U);
    pattern_len += 11U;
    for(size_t i = 0; i < text_len; i++) {
        uint8_t code = (uint8_t)text[i] - 32U;
        size_t code_len = strlen(walkprint_code128_patterns[code]);

        checksum = (uint16_t)(checksum + ((i + 1U) * code));
        memcpy(pattern + pattern_len, walkprint_code128_patterns[code], code_len);
        pattern_len += code_len;
    }
    checksum %= 103U;
    memcpy(pattern + pattern_len, walkprint_code128_patterns[checksum], 11U);
    pattern_len += 11U;
    memcpy(pattern + pattern_len, walkprint_code128_stop_pattern, sizeof(walkprint_code128_stop_pattern) - 1U);
    pattern_len += sizeof(walkprint_code128_stop_pattern) - 1U;
    pattern[pattern_len] = '\0';

    total_modules = (uint32_t)pattern_len + WALKPRINT_BARCODE_QUIET_MODULES * 2U;
    if(total_modules > WALKPRINT_PRINTER_BITMAP_WIDTH) {
        walkprint_app_set_status(app, "Barcode too wide", "Try shorter text");
        return false;
    }
    left_pad = (WALKPRINT_PRINTER_BITMAP_WIDTH - total_modules) / 2U;

    if(!walkprint_app_open_generated_bmp(app, output_path, WALKPRINT_BARCODE_HEIGHT, &file)) {
        return false;
    }

    for(uint32_t y = 0; y < WALKPRINT_BARCODE_HEIGHT; y++) {
        memset(row, 0, sizeof(row));
        if(y >= top_pad && y + bottom_pad < WALKPRINT_BARCODE_HEIGHT) {
            for(size_t i = 0; i < pattern_len; i++) {
                if(pattern[i] == '1') {
                    walkprint_app_set_row_bit(
                        row, left_pad + WALKPRINT_BARCODE_QUIET_MODULES + (uint32_t)i);
                }
            }
        }
        if(!walkprint_app_storage_write_exact(file, row, sizeof(row))) {
            storage_file_close(file);
            storage_file_free(file);
            walkprint_app_set_status(app, "Barcode save failed", "Row write failed");
            return false;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    walkprint_app_set_status(app, "Barcode generated", output_path);
    return true;
}

static bool walkprint_app_load_settings(WalkPrintApp* app) {
    File* file;
    WalkPrintSavedSettings saved;
    bool ok = false;

    if(!app || !app->storage) {
        return false;
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        return false;
    }

    if(storage_file_open(file, WALKPRINT_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       walkprint_app_storage_read_exact(file, &saved, sizeof(saved)) &&
       saved.magic == WALKPRINT_SETTINGS_MAGIC && saved.version == WALKPRINT_SETTINGS_VERSION) {
        app->density =
            (saved.density <= WALKPRINT_DENSITY_MAX) ? saved.density : WALKPRINT_DENSITY_DEFAULT;

        app->font_size = saved.font_size;
        if(app->font_size < 1U || app->font_size > 10U) {
            app->font_size = 3U;
        }

        app->char_spacing = saved.char_spacing;
        if(app->char_spacing > 10U) {
            app->char_spacing = 2U;
        }

        app->font_family =
            (saved.font_family < (uint8_t)WalkPrintFontFamilyCount) ?
                (WalkPrintFontFamily)saved.font_family :
                WalkPrintFontFamilyClassic;
        app->orientation =
            (saved.orientation < (uint8_t)WalkPrintOrientationCount) ?
                (WalkPrintOrientation)saved.orientation :
                WalkPrintOrientationUpsideDown;

        saved.printer_address[sizeof(saved.printer_address) - 1U] = '\0';
        saved.compose_message[sizeof(saved.compose_message) - 1U] = '\0';
        saved.image_path[sizeof(saved.image_path) - 1U] = '\0';

        walkprint_app_copy_text(
            app->printer_address, sizeof(app->printer_address), saved.printer_address);
        if(saved.compose_message[0] != '\0') {
            walkprint_app_copy_text(
                app->compose_message, sizeof(app->compose_message), saved.compose_message);
        }
        if(app->image_path && saved.image_path[0] != '\0') {
            furi_string_set(app->image_path, saved.image_path);
        }
        ok = true;
    }

    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool walkprint_app_save_settings(WalkPrintApp* app) {
    File* file;
    WalkPrintSavedSettings saved;
    bool ok = false;

    if(!app || !app->storage) {
        return false;
    }

    memset(&saved, 0, sizeof(saved));
    saved.magic = WALKPRINT_SETTINGS_MAGIC;
    saved.version = WALKPRINT_SETTINGS_VERSION;
    saved.density = app->density;
    saved.font_size = app->font_size;
    saved.char_spacing = app->char_spacing;
    saved.font_family = (uint8_t)app->font_family;
    saved.orientation = (uint8_t)app->orientation;
    walkprint_app_copy_text(
        saved.printer_address, sizeof(saved.printer_address), app->printer_address);
    walkprint_app_copy_text(
        saved.compose_message, sizeof(saved.compose_message), app->compose_message);
    if(app->image_path) {
        walkprint_app_copy_text(
            saved.image_path,
            sizeof(saved.image_path),
            furi_string_get_cstr(app->image_path));
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        return false;
    }

    if(storage_file_open(file, WALKPRINT_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = walkprint_app_storage_write_exact(file, &saved, sizeof(saved));
    }

    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool walkprint_app_parse_bmp_header(File* file, WalkPrintBmpInfo* info) {
    uint8_t file_header[14];
    uint8_t dib_prefix[40];
    int32_t signed_width;
    int32_t signed_height;
    uint32_t abs_height;

    if(!file || !info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    if(!walkprint_app_storage_read_exact(file, file_header, sizeof(file_header))) {
        return false;
    }

    if(walkprint_app_read_le16(file_header) != WALKPRINT_BMP_MAGIC) {
        return false;
    }

    info->pixel_data_offset = walkprint_app_read_le32(&file_header[10]);
    if(!walkprint_app_storage_read_exact(file, dib_prefix, sizeof(dib_prefix))) {
        return false;
    }

    info->dib_size = walkprint_app_read_le32(&dib_prefix[0]);
    if(info->dib_size < sizeof(dib_prefix)) {
        return false;
    }

    signed_width = walkprint_app_read_le32_signed(&dib_prefix[4]);
    signed_height = walkprint_app_read_le32_signed(&dib_prefix[8]);
    info->planes = walkprint_app_read_le16(&dib_prefix[12]);
    info->bits_per_pixel = walkprint_app_read_le16(&dib_prefix[14]);
    info->compression = walkprint_app_read_le32(&dib_prefix[16]);
    info->colors_used = walkprint_app_read_le32(&dib_prefix[32]);

    if(signed_width <= 0 || signed_height == 0 || info->planes != 1U) {
        return false;
    }

    abs_height = (signed_height < 0) ? (uint32_t)(-signed_height) : (uint32_t)signed_height;
    info->width = (uint32_t)signed_width;
    info->height = abs_height;
    info->top_down = signed_height < 0;
    info->row_stride = (((uint32_t)info->bits_per_pixel * info->width) + 31U) / 32U * 4U;

    return true;
}

static void walkprint_app_palette_luma(uint8_t* palette_luma, const uint8_t* palette, size_t count) {
    for(size_t i = 0; i < count; i++) {
        const uint8_t blue = palette[(i * 4U) + 0U];
        const uint8_t green = palette[(i * 4U) + 1U];
        const uint8_t red = palette[(i * 4U) + 2U];
        palette_luma[i] = (uint8_t)((red * 30U + green * 59U + blue * 11U) / 100U);
    }
}

static uint8_t walkprint_app_pixel_luma(
    const uint8_t* row,
    size_t x,
    const WalkPrintBmpInfo* info,
    const uint8_t* palette_luma) {
    if(info->bits_per_pixel == 32U) {
        const uint8_t blue = row[x * 4U];
        const uint8_t green = row[x * 4U + 1U];
        const uint8_t red = row[x * 4U + 2U];
        const uint8_t alpha = row[x * 4U + 3U];
        const uint8_t luma = (uint8_t)((red * 30U + green * 59U + blue * 11U) / 100U);
        return (uint8_t)((luma * alpha + 255U * (255U - alpha)) / 255U);
    }

    if(info->bits_per_pixel == 24U) {
        const uint8_t blue = row[x * 3U];
        const uint8_t green = row[x * 3U + 1U];
        const uint8_t red = row[x * 3U + 2U];
        return (uint8_t)((red * 30U + green * 59U + blue * 11U) / 100U);
    }

    if(info->bits_per_pixel == 8U) {
        return palette_luma[row[x]];
    }

    if(info->bits_per_pixel == 4U) {
        const uint8_t byte = row[x / 2U];
        const uint8_t index = (x & 0x01U) == 0 ? (uint8_t)(byte >> 4U) : (uint8_t)(byte & 0x0FU);
        return palette_luma[index];
    }

    if(info->bits_per_pixel == 1U) {
        const uint8_t byte = row[x / 8U];
        const uint8_t bit = (uint8_t)((byte >> (7U - (x % 8U))) & 0x01U);
        return palette_luma[bit];
    }

    return 255U;
}

static bool walkprint_app_prepare_bmp_file(
    WalkPrintApp* app,
    File* file,
    WalkPrintBmpInfo* info,
    uint8_t* palette_luma,
    size_t palette_capacity) {
    uint32_t palette_entries = 0;
    uint8_t palette_raw[256U * 4U];

    if(!app || !file || !info) {
        return false;
    }

    if(!walkprint_app_parse_bmp_header(file, info)) {
        walkprint_app_set_status(app, "BMP read failed", "Invalid BMP header");
        return false;
    }

    if(info->width != WALKPRINT_PRINTER_BITMAP_WIDTH) {
        walkprint_app_set_status(app, "BMP width invalid", "BMP must be 384px wide");
        return false;
    }

    if(info->compression != 0U) {
        walkprint_app_set_status(app, "BMP unsupported", "Only uncompressed BMP works");
        return false;
    }

    if(info->bits_per_pixel != 1U && info->bits_per_pixel != 4U && info->bits_per_pixel != 8U &&
       info->bits_per_pixel != 24U && info->bits_per_pixel != 32U) {
        walkprint_app_set_status(app, "BMP unsupported", "Use 1/4/8/24/32-bit BMP");
        return false;
    }

    if(info->bits_per_pixel <= 8U) {
        palette_entries = info->colors_used;
        if(palette_entries == 0U) {
            palette_entries = 1UL << info->bits_per_pixel;
        }
        if(palette_entries > 256U || palette_entries > palette_capacity) {
            walkprint_app_set_status(app, "BMP unsupported", "Palette too large");
            return false;
        }

        if(!walkprint_app_storage_read_exact(file, palette_raw, palette_entries * 4U)) {
            walkprint_app_set_status(app, "BMP read failed", "Palette read failed");
            return false;
        }

        walkprint_app_palette_luma(palette_luma, palette_raw, palette_entries);
    }

    return true;
}

static bool walkprint_app_flush_raster_chunk(
    WalkPrintApp* app,
    uint8_t* raster_chunk,
    size_t* raster_chunk_length) {
    if(!app || !raster_chunk || !raster_chunk_length) {
        return false;
    }

    if(*raster_chunk_length == 0U) {
        return true;
    }

    if(!walkprint_transport_send(&app->transport, raster_chunk, *raster_chunk_length)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        return false;
    }

    *raster_chunk_length = 0U;
    return true;
}

bool walkprint_app_select_bmp(WalkPrintApp* app) {
    DialogsFileBrowserOptions browser_options;
    bool selected;

    if(!app || !app->dialogs || !app->image_path) {
        return false;
    }

    dialog_file_browser_set_basic_options(&browser_options, ".bmp", NULL);
    browser_options.base_path = STORAGE_EXT_PATH_PREFIX;
    browser_options.skip_assets = true;

    if(furi_string_empty(app->image_path)) {
        furi_string_set(app->image_path, STORAGE_EXT_PATH_PREFIX);
    }

    selected = dialog_file_browser_show(app->dialogs, app->image_path, app->image_path, &browser_options);
    if(!selected) {
        walkprint_app_set_status(app, "BMP selection canceled", "Choose a 384px BMP");
        return false;
    }

    app->bmp_confirm_save_before_print = false;
    if(app->pending_bmp_output_path) {
        furi_string_reset(app->pending_bmp_output_path);
    }
    walkprint_app_save_settings(app);
    walkprint_app_set_status(app, "BMP selected", furi_string_get_cstr(app->image_path));
    return true;
}

void walkprint_app_show_bmp_confirm(WalkPrintApp* app, bool save_before_print) {
    if(!app) {
        return;
    }

    app->bmp_confirm_save_before_print = save_before_print;
    app->screen = WalkPrintScreenConfirmBmp;
    walkprint_app_request_redraw(app);
}

void walkprint_app_prepare_generated_bmp(
    WalkPrintApp* app,
    const char* temp_bmp_path,
    const char* save_bmp_path,
    const char* label) {
    if(!app || !app->image_path || !app->pending_bmp_output_path || !temp_bmp_path || !save_bmp_path) {
        return;
    }

    furi_string_set(app->image_path, temp_bmp_path);
    furi_string_set(app->pending_bmp_output_path, save_bmp_path);
    walkprint_app_set_status(app, label ? label : "Generated BMP", save_bmp_path);
    walkprint_app_show_bmp_confirm(app, true);
}

static bool walkprint_app_copy_file(WalkPrintApp* app, const char* source_path, const char* target_path) {
    File* source = NULL;
    File* target = NULL;
    uint8_t buffer[128];
    bool ok = false;

    if(!app || !app->storage || !source_path || !target_path) {
        return false;
    }

    source = storage_file_alloc(app->storage);
    target = storage_file_alloc(app->storage);
    if(!source || !target) {
        walkprint_app_set_status(app, "BMP save failed", "File alloc failed");
        goto cleanup;
    }

    if(!storage_file_open(source, source_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        walkprint_app_set_status(app, "BMP save failed", "Source open failed");
        goto cleanup;
    }

    if(!storage_file_open(target, target_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        walkprint_app_set_status(app, "BMP save failed", "Target open failed");
        goto cleanup;
    }

    while(true) {
        uint16_t read_count = storage_file_read(source, buffer, sizeof(buffer));
        if(read_count == 0U) {
            break;
        }

        if(storage_file_write(target, buffer, read_count) != read_count) {
            walkprint_app_set_status(app, "BMP save failed", "Write failed");
            goto cleanup;
        }
    }

    ok = true;

cleanup:
    if(source) {
        storage_file_close(source);
        storage_file_free(source);
    }
    if(target) {
        storage_file_close(target);
        storage_file_free(target);
    }

    return ok;
}

bool walkprint_app_confirm_bmp(WalkPrintApp* app) {
    const char* saved_path;

    if(!app || !app->image_path || furi_string_empty(app->image_path)) {
        walkprint_app_set_status(app, "BMP not selected", "Choose a BMP first");
        return false;
    }

    if(app->bmp_confirm_save_before_print) {
        if(!app->pending_bmp_output_path || furi_string_empty(app->pending_bmp_output_path)) {
            walkprint_app_set_status(app, "BMP save failed", "Missing output path");
            return false;
        }

        saved_path = furi_string_get_cstr(app->pending_bmp_output_path);
        if(!walkprint_app_copy_file(app, furi_string_get_cstr(app->image_path), saved_path)) {
            return false;
        }

        furi_string_set(app->image_path, saved_path);
        app->bmp_confirm_save_before_print = false;
        walkprint_app_save_settings(app);
        walkprint_app_set_status(app, "BMP saved", saved_path);
    }

    walkprint_app_queue_send_bmp(app);
    return true;
}

bool walkprint_app_select_text_file(WalkPrintApp* app) {
    DialogsFileBrowserOptions browser_options;
    bool selected;

    if(!app || !app->dialogs || !app->text_path) {
        return false;
    }

    dialog_file_browser_set_basic_options(&browser_options, ".txt", NULL);
    browser_options.base_path = STORAGE_EXT_PATH_PREFIX;
    browser_options.skip_assets = true;

    if(furi_string_empty(app->text_path)) {
        furi_string_set(app->text_path, STORAGE_EXT_PATH_PREFIX);
    }

    selected = dialog_file_browser_show(app->dialogs, app->text_path, app->text_path, &browser_options);
    if(!selected) {
        walkprint_app_set_status(app, "TXT selection canceled", "Choose a .txt file");
        return false;
    }

    walkprint_app_set_status(app, "TXT selected", furi_string_get_cstr(app->text_path));
    return true;
}

bool walkprint_app_send_text_file(WalkPrintApp* app) {
    char page_buffer[WALKPRINT_TEXT_PAGE_BUFFER_SIZE];
    char detail[WALKPRINT_STATUS_TEXT_SIZE];
    bool has_more = false;

    if(!app || !app->text_path || furi_string_empty(app->text_path)) {
        walkprint_app_set_status(app, "TXT not selected", "Choose a .txt file first");
        return false;
    }

    if(!app->text_print_active) {
        if(!walkprint_app_load_text_file_contents(
               app,
               furi_string_get_cstr(app->text_path),
               &app->text_print_buffer)) {
            return false;
        }

        app->text_print_offset = 0U;
        app->text_print_chars_per_line = walkprint_app_text_chars_per_line(app);
        app->text_print_lines_per_page = walkprint_app_text_lines_per_page(app);
        app->text_print_page_count = 0U;
        app->text_print_active = true;
        app->text_print_cancel_requested = false;
        app->busy_cancelable = true;
    }

    if(app->text_print_cancel_requested) {
        snprintf(
            detail,
            sizeof(detail),
            "%u page%s",
            (unsigned)app->text_print_page_count,
            app->text_print_page_count == 1U ? "" : "s");
        walkprint_app_clear_text_print_job(app);
        walkprint_app_set_status(app, "TXT canceled", detail);
        return true;
    }

    if(!walkprint_app_build_text_page(
           app->text_print_buffer,
           &app->text_print_offset,
           app->text_print_chars_per_line,
           app->text_print_lines_per_page,
           page_buffer,
           sizeof(page_buffer),
           &has_more)) {
        if(app->text_print_page_count == 0U) {
            walkprint_app_clear_text_print_job(app);
            walkprint_app_set_status(app, "TXT empty", "No printable text found");
        } else {
            snprintf(
                detail,
                sizeof(detail),
                "%u page%s",
                (unsigned)app->text_print_page_count,
                app->text_print_page_count == 1U ? "" : "s");
            walkprint_app_clear_text_print_job(app);
            walkprint_app_set_status(app, "TXT printed", detail);
        }
        return true;
    }

    app->text_print_page_count++;
    snprintf(
        detail,
        sizeof(detail),
        "Page %u%s",
        (unsigned)app->text_print_page_count,
        has_more ? "  Back cancels" : "");
    walkprint_app_set_status(app, "Printing TXT", detail);

    if(!walkprint_app_send_message_text(app, page_buffer, "TXT page printed")) {
        walkprint_app_clear_text_print_job(app);
        return false;
    }

    if(app->text_print_cancel_requested) {
        snprintf(
            detail,
            sizeof(detail),
            "%u page%s",
            (unsigned)app->text_print_page_count,
            app->text_print_page_count == 1U ? "" : "s");
        walkprint_app_clear_text_print_job(app);
        walkprint_app_set_status(app, "TXT canceled", detail);
        return true;
    }

    if(has_more) {
        snprintf(
            detail,
            sizeof(detail),
            "Page %u done",
            (unsigned)app->text_print_page_count);
        walkprint_app_set_status(app, "Printing TXT", detail);
        walkprint_app_schedule_text_print_page(app);
        return true;
    }

    snprintf(
        detail,
        sizeof(detail),
        "%u page%s",
        (unsigned)app->text_print_page_count,
        app->text_print_page_count == 1U ? "" : "s");
    walkprint_app_clear_text_print_job(app);
    walkprint_app_set_status(app, "TXT printed", detail);
    return true;
}

bool walkprint_app_send_bmp(WalkPrintApp* app) {
    WalkPrintBmpInfo info;
    File* file = NULL;
    bool ok = false;
    uint8_t palette_luma[256];
    uint8_t row_buffer[WALKPRINT_BMP_MAX_ROW_SIZE];
    uint8_t raster_row[WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES];
    uint8_t raster_chunk[WALKPRINT_BMP_RASTER_CHUNK_SIZE];
    size_t raster_chunk_length = 0;
    uint8_t image_header[8];
    uint8_t threshold;

    if(!app || !app->storage || !app->image_path || furi_string_empty(app->image_path)) {
        return false;
    }

    if(!walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        walkprint_app_set_status(app, "BMP open failed", "File alloc failed");
        return false;
    }

    if(!storage_file_open(file, furi_string_get_cstr(app->image_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        walkprint_app_set_status(app, "BMP open failed", "Open from SD failed");
        goto cleanup;
    }

    if(!walkprint_app_prepare_bmp_file(app, file, &info, palette_luma, sizeof(palette_luma))) {
        goto cleanup;
    }

    threshold = (uint8_t)(WALKPRINT_BMP_THRESHOLD_BASE - (app->density / 2U));

    image_header[0] = 0x1D;
    image_header[1] = 0x76;
    image_header[2] = 0x30;
    image_header[3] = 0x00;
    image_header[4] = (uint8_t)(WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES & 0xFFU);
    image_header[5] = (uint8_t)((WALKPRINT_PRINTER_BITMAP_WIDTH_BYTES >> 8U) & 0xFFU);
    image_header[6] = (uint8_t)(info.height & 0xFFU);
    image_header[7] = (uint8_t)((info.height >> 8U) & 0xFFU);

    if(!walkprint_transport_send(&app->transport, walkprint_config_init_frame, WALKPRINT_INIT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        goto cleanup;
    }

    furi_delay_ms(100);

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_start_print_frame, WALKPRINT_START_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        goto cleanup;
    }

    furi_delay_ms(50);

    if(!walkprint_transport_send(&app->transport, image_header, sizeof(image_header))) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        goto cleanup;
    }

    for(uint32_t row = 0; row < info.height; row++) {
        uint32_t bmp_row = info.top_down ? row : (info.height - 1U - row);
        uint32_t row_offset = info.pixel_data_offset + (bmp_row * info.row_stride);

        if(!storage_file_seek(file, row_offset, true)) {
            walkprint_app_set_status(app, "BMP read failed", "Seek failed");
            goto cleanup;
        }

        if(storage_file_read(file, row_buffer, info.row_stride) != info.row_stride) {
            walkprint_app_set_status(app, "BMP read failed", "Row read failed");
            goto cleanup;
        }

        memset(raster_row, 0, sizeof(raster_row));
        for(uint32_t x = 0; x < WALKPRINT_PRINTER_BITMAP_WIDTH; x++) {
            uint8_t luma = walkprint_app_pixel_luma(row_buffer, x, &info, palette_luma);
            if(luma < threshold) {
                raster_row[x / 8U] |= (uint8_t)(1U << (7U - (x % 8U)));
            }
        }

        memcpy(raster_chunk + raster_chunk_length, raster_row, sizeof(raster_row));
        raster_chunk_length += sizeof(raster_row);

        if(raster_chunk_length >= sizeof(raster_chunk) &&
           !walkprint_app_flush_raster_chunk(app, raster_chunk, &raster_chunk_length)) {
            goto cleanup;
        }
    }

    if(!walkprint_app_flush_raster_chunk(app, raster_chunk, &raster_chunk_length)) {
        goto cleanup;
    }

    furi_delay_ms(50);

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_end_print_frame, WALKPRINT_END_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Print failed", app->transport.last_response);
        goto cleanup;
    }

    walkprint_app_set_status(app, "BMP printed", furi_string_get_cstr(app->image_path));
    ok = true;

cleanup:
    if(file) {
        storage_file_close(file);
        storage_file_free(file);
    }

    return ok;
}

bool walkprint_app_discover_printer(WalkPrintApp* app) {
    if(!app) {
        return false;
    }

    if(!app->transport.bridge_ready) {
        walkprint_app_reset_transport(app);
        if(!app->transport.bridge_ready) {
            walkprint_app_set_status(app, "Bridge offline", app->transport.last_response);
            return false;
        }
    }

    if(!walkprint_transport_discover_printer(&app->transport)) {
        walkprint_app_set_status(app, "Discover failed", app->transport.last_response);
        return false;
    }

    if(app->transport.printer_address[0] != '\0') {
        walkprint_app_copy_text(
            app->printer_address,
            sizeof(app->printer_address),
            app->transport.printer_address);
        walkprint_app_save_settings(app);
    }

    walkprint_app_set_status(
        app,
        "Printer found",
        app->transport.printer_name[0] != '\0' ? app->transport.printer_name :
                                                 app->transport.last_response);
    return true;
}

bool walkprint_app_scan_wifi(WalkPrintApp* app) {
    if(!app) {
        return false;
    }

    if(!app->transport.bridge_ready) {
        walkprint_app_reset_transport(app);
        if(!app->transport.bridge_ready) {
            walkprint_app_set_status(app, "Bridge offline", app->transport.last_response);
            return false;
        }
    }

    if(!walkprint_transport_scan_wifi(&app->transport)) {
        walkprint_app_set_status(app, "WiFi scan failed", app->transport.last_response);
        return false;
    }

    walkprint_app_set_status(app, "WiFi scan ok", app->transport.last_response);
    return true;
}

bool walkprint_app_send_feed(WalkPrintApp* app) {
    WalkPrintFrame frame;

    if(!app || !walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    if(!walkprint_protocol_build_feed(&app->protocol, 1, &frame)) {
        walkprint_app_set_status(app, "Feed build failed", "Check protocol placeholders");
        return false;
    }

    if(!walkprint_protocol_send_frame(&app->protocol, &app->transport, &frame)) {
        walkprint_app_set_status(app, "Feed send failed", app->transport.last_response);
        return false;
    }

    walkprint_app_set_status(app, "Feed command sent", "Printer advanced paper");
    return true;
}

void walkprint_app_adjust_density(WalkPrintApp* app, int8_t delta) {
    int density;

    if(!app || delta == 0) {
        return;
    }

    density = (int)app->density + (int)delta;
    if(density < WALKPRINT_DENSITY_MIN) {
        density = WALKPRINT_DENSITY_MIN;
    } else if(density > WALKPRINT_DENSITY_MAX) {
        density = WALKPRINT_DENSITY_MAX;
    }

    app->density = (uint8_t)density;
    walkprint_app_save_settings(app);
    walkprint_app_set_status(app, "Density updated", walkprint_app_printer_address_label(app));
}

void walkprint_app_adjust_font_size(WalkPrintApp* app, int8_t delta) {
    int next_size;

    if(!app || delta == 0) {
        return;
    }

    next_size = (int)app->font_size + (int)delta;
    if(next_size < 1) {
        next_size = 1;
    } else if(next_size > 10) {
        next_size = 10;
    }

    app->font_size = (uint8_t)next_size;
    walkprint_app_save_settings(app);
    walkprint_app_set_status(app, "Font size updated", app->compose_message);
}

void walkprint_app_adjust_char_spacing(WalkPrintApp* app, int8_t delta) {
    int next_spacing;

    if(!app || delta == 0) {
        return;
    }

    next_spacing = (int)app->char_spacing + (int)delta;
    if(next_spacing < 0) {
        next_spacing = 0;
    } else if(next_spacing > 10) {
        next_spacing = 10;
    }

    app->char_spacing = (uint8_t)next_spacing;
    walkprint_app_save_settings(app);
    walkprint_app_set_status(app, "Spacing updated", app->compose_message);
}

void walkprint_app_adjust_font_family(WalkPrintApp* app, int8_t delta) {
    int next_family;

    if(!app || delta == 0) {
        return;
    }

    next_family = (int)app->font_family + (int)delta;
    while(next_family < 0) {
        next_family += (int)WalkPrintFontFamilyCount;
    }
    while(next_family >= (int)WalkPrintFontFamilyCount) {
        next_family -= (int)WalkPrintFontFamilyCount;
    }

    app->font_family = (WalkPrintFontFamily)next_family;
    walkprint_app_save_settings(app);
    walkprint_app_set_status(
        app, "Font family updated", walkprint_protocol_font_family_name(app->font_family));
}

void walkprint_app_adjust_orientation(WalkPrintApp* app, int8_t delta) {
    int next_orientation;

    if(!app || delta == 0) {
        return;
    }

    next_orientation = (int)app->orientation + (int)delta;
    while(next_orientation < 0) {
        next_orientation += (int)WalkPrintOrientationCount;
    }
    while(next_orientation >= (int)WalkPrintOrientationCount) {
        next_orientation -= (int)WalkPrintOrientationCount;
    }

    app->orientation = (WalkPrintOrientation)next_orientation;
    walkprint_app_save_settings(app);
    walkprint_app_set_status(
        app, "Orientation updated", walkprint_protocol_orientation_name(app->orientation));
}

static bool walkprint_app_is_address_separator(size_t index) {
    return index == 2 || index == 5 || index == 8 || index == 11 || index == 14;
}

bool walkprint_app_has_printer_address(const WalkPrintApp* app) {
    return app && app->printer_address[0] != '\0';
}

const char* walkprint_app_printer_address_label(const WalkPrintApp* app) {
    return walkprint_app_has_printer_address(app) ? app->printer_address : "Unset";
}

void walkprint_app_begin_address_edit(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_app_copy_text(
        app->address_edit_buffer,
        sizeof(app->address_edit_buffer),
        walkprint_app_has_printer_address(app) ? app->printer_address :
                                                 WALKPRINT_PRINTER_ADDRESS_TEMPLATE);
    app->address_cursor = 0;
    app->address_edit_dirty = false;
}

void walkprint_app_commit_address_edit(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_app_copy_text(
        app->printer_address, sizeof(app->printer_address), app->address_edit_buffer);
    app->address_edit_dirty = false;
    walkprint_app_save_settings(app);
    walkprint_app_reset_transport(app);
}

void walkprint_app_move_address_cursor(WalkPrintApp* app, int8_t delta) {
    int next_index;

    if(!app || delta == 0) {
        return;
    }

    next_index = (int)app->address_cursor;
    do {
        next_index += (delta > 0) ? 1 : -1;
        if(next_index < 0) {
            next_index = WALKPRINT_PRINTER_ADDRESS_STR_SIZE - 2;
        } else if(next_index >= WALKPRINT_PRINTER_ADDRESS_STR_SIZE - 1) {
            next_index = 0;
        }
    } while(walkprint_app_is_address_separator((size_t)next_index));

    app->address_cursor = (size_t)next_index;
}

void walkprint_app_adjust_address_char(WalkPrintApp* app, int8_t delta) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char* cursor_char;
    const char* found;
    int current_index;
    int next_index;

    if(!app || delta == 0 || walkprint_app_is_address_separator(app->address_cursor)) {
        return;
    }

    cursor_char = &app->address_edit_buffer[app->address_cursor];
    found = strchr(hex_chars, *cursor_char);
    current_index = found ? (int)(found - hex_chars) : 0;
    next_index = (current_index + (int)delta + 16) % 16;
    *cursor_char = hex_chars[next_index];
    app->address_edit_dirty = true;
}

const char* walkprint_app_connection_label(const WalkPrintApp* app) {
    if(!app || !app->transport.initialized || !app->transport.bridge_ready) {
        return "Off";
    }

    return walkprint_transport_is_connected(&app->transport) ? "Connected" : "Ready";
}

static WalkPrintApp* walkprint_app_alloc(void) {
    WalkPrintApp* app = malloc(sizeof(WalkPrintApp));
    WalkPrintMainViewModel* model;

    if(!app) {
        return NULL;
    }

    memset(app, 0, sizeof(WalkPrintApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->view_dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();
    app->text_input = text_input_alloc();
    app->text_print_timer =
        furi_timer_alloc(walkprint_app_text_print_timer_callback, FuriTimerTypeOnce, app);
    app->image_path = furi_string_alloc();
    app->text_path = furi_string_alloc();
    app->pending_bmp_output_path = furi_string_alloc();

    walkprint_app_seed_defaults(app);
    walkprint_app_load_settings(app);
    walkprint_protocol_init(&app->protocol);

    view_allocate_model(app->main_view, ViewModelTypeLockFree, sizeof(WalkPrintMainViewModel));
    model = view_get_model(app->main_view);
    model->app = app;
    view_commit_model(app->main_view, false);
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, walkprint_app_main_view_draw);
    view_set_input_callback(app->main_view, walkprint_app_main_view_input);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, walkprint_app_handle_custom_event);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, walkprint_app_navigation_event);
    view_dispatcher_add_view(app->view_dispatcher, WalkPrintViewMain, app->main_view);
    view_dispatcher_add_view(
        app->view_dispatcher, WalkPrintViewKeyboard, text_input_get_view(app->text_input));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    walkprint_app_reset_transport(app);
    return app;
}

static void walkprint_app_free(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_app_save_settings(app);
    walkprint_transport_disconnect(&app->transport);
    walkprint_app_clear_text_print_job(app);
    walkprint_app_update_led(app);

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, WalkPrintViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, WalkPrintViewKeyboard);
    }
    if(app->text_input) {
        text_input_free(app->text_input);
    }
    if(app->text_print_timer) {
        furi_timer_free(app->text_print_timer);
    }
    if(app->image_path) {
        furi_string_free(app->image_path);
    }

    if(app->text_path) {
        furi_string_free(app->text_path);
    }
    if(app->pending_bmp_output_path) {
        furi_string_free(app->pending_bmp_output_path);
    }
    if(app->main_view) {
        view_free(app->main_view);
    }
    if(app->view_dispatcher) {
        view_dispatcher_free(app->view_dispatcher);
    }
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }
    if(app->dialogs) {
        furi_record_close(RECORD_DIALOGS);
    }
    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }
    if(app->storage) {
        furi_record_close(RECORD_STORAGE);
    }

    free(app);
}

int32_t walkprint_app(void* p) {
    WalkPrintApp* app;

    UNUSED(p);

    app = walkprint_app_alloc();
    if(!app) {
        return -1;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewMain);
    view_dispatcher_run(app->view_dispatcher);

    walkprint_app_free(app);
    return 0;
}
