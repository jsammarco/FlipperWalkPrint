#include "walkprint_app.h"

#include "app_ui.h"
#include "walkprint_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    WalkPrintViewMain = 0,
    WalkPrintViewKeyboard,
} WalkPrintViewId;

typedef enum {
    WalkPrintCustomEventPingBridge = 1,
    WalkPrintCustomEventDiscoverPrinter,
    WalkPrintCustomEventConnect,
    WalkPrintCustomEventPrintTest,
    WalkPrintCustomEventPrintMessage,
    WalkPrintCustomEventSendRaw,
    WalkPrintCustomEventFeedPaper,
    WalkPrintCustomEventScanWifi,
} WalkPrintCustomEvent;

typedef struct {
    WalkPrintApp* app;
} WalkPrintMainViewModel;

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

static void walkprint_app_queue_busy_action(
    WalkPrintApp* app,
    uint32_t event,
    const char* status,
    const char* detail) {
    if(!app || !app->view_dispatcher) {
        return;
    }

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

static void walkprint_app_seed_defaults(WalkPrintApp* app) {
    app->screen = WalkPrintScreenMainMenu;
    app->running = true;
    app->main_menu_index = 0;
    app->settings_index = 0;
    app->address_cursor = 0;
    app->density = WALKPRINT_DENSITY_DEFAULT;
    app->font_size = 2;
    app->font_family = WalkPrintFontFamilyClassic;

    walkprint_app_copy_text(
        app->printer_address,
        sizeof(app->printer_address),
        WALKPRINT_DEFAULT_PRINTER_ADDRESS);
    walkprint_app_copy_text(app->compose_message, sizeof(app->compose_message), "HELLO");
    walkprint_debug_format_hex_preview(
        walkprint_config_raw_frame,
        WALKPRINT_RAW_FRAME_LEN,
        app->raw_frame_preview,
        sizeof(app->raw_frame_preview));
    walkprint_app_set_status(app, "Ready", "ESP32 bridge target");
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
    walkprint_app_queue_send_message(app);
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
    case WalkPrintCustomEventPrintTest:
        walkprint_app_send_test_receipt(app);
        break;
    case WalkPrintCustomEventPrintMessage:
        walkprint_app_send_message(app);
        break;
    case WalkPrintCustomEventSendRaw:
        walkprint_app_send_configured_raw_frame(app);
        break;
    case WalkPrintCustomEventFeedPaper:
        walkprint_app_send_feed(app);
        break;
    case WalkPrintCustomEventScanWifi:
        walkprint_app_scan_wifi(app);
        break;
    default:
        handled = false;
        break;
    }

    if(handled) {
        app->screen = WalkPrintScreenMainMenu;
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
        walkprint_app_set_status(app, "Ready", app->compose_message);
        return true;
    }

    return false;
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

void walkprint_app_show_keyboard(WalkPrintApp* app) {
    if(!app || !app->text_input || !app->view_dispatcher) {
        return;
    }

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Message to print");
    text_input_set_minimum_length(app->text_input, 1U);
    text_input_set_result_callback(
        app->text_input,
        walkprint_app_text_input_done,
        app,
        app->compose_message,
        sizeof(app->compose_message),
        false);
    app->screen = WalkPrintScreenEditMessage;
    walkprint_app_set_status(app, "Editing message", "Use keyboard to type");
    view_dispatcher_switch_to_view(app->view_dispatcher, WalkPrintViewKeyboard);
}

void walkprint_app_queue_ping_bridge(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPingBridge, "Checking bridge", "Pinging ESP32 UART bridge");
}

void walkprint_app_queue_discover_printer(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app,
        WalkPrintCustomEventDiscoverPrinter,
        "Discovering printer",
        "Scanning Bluetooth devices");
}

void walkprint_app_queue_connect(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventConnect, "Connecting printer", "Opening Bluetooth link");
}

void walkprint_app_queue_send_test_receipt(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPrintTest, "Printing test", "Sending raster receipt");
}

void walkprint_app_queue_send_message(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventPrintMessage, "Printing message", "Rendering custom text");
}

void walkprint_app_queue_send_feed(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventFeedPaper, "Feeding paper", "Sending feed command");
}

void walkprint_app_queue_send_configured_raw_frame(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventSendRaw, "Sending raw frame", "Writing configured bytes");
}

void walkprint_app_queue_scan_wifi(WalkPrintApp* app) {
    walkprint_app_queue_busy_action(
        app, WalkPrintCustomEventScanWifi, "Scanning WiFi", "Querying bridge radios");
}

void walkprint_app_reset_transport(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_transport_disconnect(&app->transport);

    if(!walkprint_transport_init(
           &app->transport, walkprint_transport_live_ops(), app->printer_address)) {
        walkprint_app_set_status(app, "Transport init failed", "Check config");
        return;
    }

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

    if(!app->transport.bridge_ready) {
        walkprint_app_reset_transport(app);
        if(!app->transport.bridge_ready) {
            walkprint_app_set_status(app, "Bridge offline", app->transport.last_response);
            return false;
        }
    }

    if(walkprint_transport_connect(&app->transport)) {
        walkprint_app_set_status(
            app,
            "Connected",
            app->transport.printer_name[0] != '\0' ? app->transport.printer_name :
                                                     app->printer_address);
        return true;
    }

    walkprint_app_set_status(app, "Connect failed", app->transport.last_response);
    return false;
}

void walkprint_app_disconnect(WalkPrintApp* app) {
    if(!app) {
        return;
    }

    walkprint_transport_disconnect(&app->transport);
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

bool walkprint_app_send_test_receipt(WalkPrintApp* app) {
    WalkPrintFrame frame;

    if(!app || !walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    if(!walkprint_protocol_build_test_receipt(
           &app->protocol,
           walkprint_demo_receipt_lines,
           WALKPRINT_DEMO_RECEIPT_LINE_COUNT,
           app->density,
           &frame)) {
        walkprint_app_set_status(app, "Receipt build failed", "See logs");
        return false;
    }

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_init_frame, WALKPRINT_INIT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Receipt send failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_transport_send(
           &app->transport,
           walkprint_config_start_print_frame,
           WALKPRINT_START_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Receipt send failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_protocol_send_frame(&app->protocol, &app->transport, &frame)) {
        walkprint_app_set_status(app, "Receipt send failed", app->transport.last_response);
        return false;
    }

    furi_delay_ms(500);

    if(!walkprint_transport_send(
           &app->transport, walkprint_config_end_print_frame, WALKPRINT_END_PRINT_FRAME_LEN)) {
        walkprint_app_set_status(app, "Receipt send failed", app->transport.last_response);
        return false;
    }

    walkprint_app_set_status(app, "Receipt sent", "Raster image payload");
    return true;
}

bool walkprint_app_send_message(WalkPrintApp* app) {
    WalkPrintFrame frame;

    if(!app || !walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    if(!walkprint_protocol_build_message_receipt(
           &app->protocol,
           app->compose_message,
           app->density,
           app->font_size,
           app->font_family,
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

    walkprint_app_set_status(app, "Message printed", app->compose_message);
    return true;
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

bool walkprint_app_send_configured_raw_frame(WalkPrintApp* app) {
    WalkPrintFrame frame;

    if(!app || !walkprint_app_require_connection(app, "Use Connect first")) {
        return false;
    }

    if(!walkprint_protocol_build_raw(
           &app->protocol, walkprint_config_raw_frame, WALKPRINT_RAW_FRAME_LEN, &frame)) {
        walkprint_app_set_status(app, "Raw build failed", "Check config bytes");
        return false;
    }

    if(!walkprint_protocol_send_frame(&app->protocol, &app->transport, &frame)) {
        walkprint_app_set_status(app, "Raw send failed", app->transport.last_response);
        return false;
    }

    walkprint_app_set_status(app, "Raw frame sent", app->raw_frame_preview);
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
    walkprint_app_set_status(app, "Density updated", app->printer_address);
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
    walkprint_app_set_status(app, "Font size updated", app->compose_message);
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
    walkprint_app_set_status(
        app, "Font family updated", walkprint_protocol_font_family_name(app->font_family));
}

static bool walkprint_app_is_address_separator(size_t index) {
    return index == 2 || index == 5 || index == 8 || index == 11 || index == 14;
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

    cursor_char = &app->printer_address[app->address_cursor];
    found = strchr(hex_chars, *cursor_char);
    current_index = found ? (int)(found - hex_chars) : 0;
    next_index = (current_index + (int)delta + 16) % 16;
    *cursor_char = hex_chars[next_index];

    walkprint_app_reset_transport(app);
}

const char* walkprint_app_connection_label(const WalkPrintApp* app) {
    if(!app || !app->transport.initialized || !app->transport.bridge_ready) {
        return "bridge-offline";
    }

    return walkprint_transport_is_connected(&app->transport) ? "printer-connected" :
                                                               "bridge-ready";
}

static WalkPrintApp* walkprint_app_alloc(void) {
    WalkPrintApp* app = malloc(sizeof(WalkPrintApp));
    WalkPrintMainViewModel* model;

    if(!app) {
        return NULL;
    }

    memset(app, 0, sizeof(WalkPrintApp));
    walkprint_app_seed_defaults(app);
    walkprint_protocol_init(&app->protocol);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();
    app->text_input = text_input_alloc();

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

    walkprint_transport_disconnect(&app->transport);

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, WalkPrintViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, WalkPrintViewKeyboard);
    }
    if(app->text_input) {
        text_input_free(app->text_input);
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
