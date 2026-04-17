#include "app_ui.h"

#include <stdio.h>
#include <string.h>

static const char* const walkprint_main_menu_items[] = {
    "Ping Bridge",
    "Discover Printer",
    "Connect",
    "Print Test Receipt",
    "Print Message",
    "Send Raw Frame",
    "Feed Paper",
    "WiFi Scan",
    "Settings",
    "About",
};

static const char* const walkprint_settings_items[] = {
    "Printer Address",
    "Density",
    "Font Size",
};

static size_t walkprint_ui_main_menu_count(void) {
    return sizeof(walkprint_main_menu_items) / sizeof(walkprint_main_menu_items[0]);
}

static size_t walkprint_ui_settings_count(void) {
    return sizeof(walkprint_settings_items) / sizeof(walkprint_settings_items[0]);
}

static void walkprint_ui_draw_line(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str(canvas, 0, y, text ? text : "");
}

static void walkprint_ui_draw_header(Canvas* canvas, const WalkPrintApp* app) {
    char transport_line[WALKPRINT_STATUS_TEXT_SIZE];
    snprintf(
        transport_line,
        sizeof(transport_line),
        "%s | %s",
        walkprint_transport_name(),
        walkprint_app_connection_label(app));

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    walkprint_ui_draw_line(canvas, 8, WALKPRINT_APP_NAME);
    walkprint_ui_draw_line(canvas, 16, transport_line);
    walkprint_ui_draw_line(canvas, 24, app->status_line);
}

static void walkprint_ui_draw_main_menu(Canvas* canvas, WalkPrintApp* app) {
    size_t item_count = walkprint_ui_main_menu_count();
    size_t first_visible = 0;

    if(app->main_menu_index >= WALKPRINT_MENU_VISIBLE_ITEMS) {
        first_visible = app->main_menu_index - (WALKPRINT_MENU_VISIBLE_ITEMS - 1U);
    }

    for(size_t i = 0; i < WALKPRINT_MENU_VISIBLE_ITEMS; i++) {
        char line[WALKPRINT_STATUS_TEXT_SIZE];
        size_t item_index = first_visible + i;
        uint8_t y = 34 + (uint8_t)(i * 8U);

        if(item_index >= item_count) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %s",
            (item_index == app->main_menu_index) ? '>' : ' ',
            walkprint_main_menu_items[item_index]);
        walkprint_ui_draw_line(canvas, y, line);
    }
}

static void walkprint_ui_draw_busy(Canvas* canvas, WalkPrintApp* app) {
    canvas_set_font(canvas, FontPrimary);
    walkprint_ui_draw_line(canvas, 4, app->status_line);
    canvas_set_font(canvas, FontSecondary);
    walkprint_ui_draw_line(canvas, 24, app->detail_line);
    walkprint_ui_draw_line(canvas, 40, "Bridge and printer");
    walkprint_ui_draw_line(canvas, 48, "can take a moment.");
    walkprint_ui_draw_line(canvas, 58, "Please wait...");
}

static void walkprint_ui_draw_settings(Canvas* canvas, WalkPrintApp* app) {
    char line[WALKPRINT_HEX_PREVIEW_SIZE];

    walkprint_ui_draw_line(canvas, 24, app->detail_line);

    for(size_t i = 0; i < walkprint_ui_settings_count(); i++) {
        uint8_t y = 34 + (uint8_t)(i * 8U);

        switch(i) {
        case 0:
            snprintf(
                line,
                sizeof(line),
                "%c Addr: %s",
                (i == app->settings_index) ? '>' : ' ',
                app->printer_address);
            break;
        case 1:
            snprintf(
                line,
                sizeof(line),
                "%c Density: %u",
                (i == app->settings_index) ? '>' : ' ',
                app->density);
            break;
        case 2:
        default:
            snprintf(
                line,
                sizeof(line),
                "%c Font: %u",
                (i == app->settings_index) ? '>' : ' ',
                app->font_size);
            break;
        }

        walkprint_ui_draw_line(canvas, y, line);
    }
}

static void walkprint_ui_draw_address_editor(Canvas* canvas, WalkPrintApp* app) {
    char cursor_line[WALKPRINT_PRINTER_ADDRESS_STR_SIZE];

    memset(cursor_line, ' ', sizeof(cursor_line));
    cursor_line[sizeof(cursor_line) - 1U] = '\0';
    if(app->address_cursor < sizeof(cursor_line) - 1U) {
        cursor_line[app->address_cursor] = '^';
    }

    walkprint_ui_draw_line(canvas, 24, "Edit printer MAC/ID");
    walkprint_ui_draw_line(canvas, 34, app->printer_address);
    walkprint_ui_draw_line(canvas, 42, cursor_line);
    walkprint_ui_draw_line(canvas, 50, "L/R move U/D change");
    walkprint_ui_draw_line(canvas, 58, "OK/Back save");
}

static void walkprint_ui_draw_hex_wrapped(
    Canvas* canvas,
    uint8_t y,
    const char* preview,
    size_t first_len) {
    char line_one[WALKPRINT_HEX_PREVIEW_SIZE];
    char line_two[WALKPRINT_HEX_PREVIEW_SIZE];
    size_t preview_len = strlen(preview);

    memset(line_one, 0, sizeof(line_one));
    memset(line_two, 0, sizeof(line_two));

    if(preview_len <= first_len) {
        snprintf(line_one, sizeof(line_one), "%s", preview);
    } else {
        snprintf(line_one, sizeof(line_one), "%.*s", (int)first_len, preview);
        snprintf(line_two, sizeof(line_two), "%s", preview + first_len);
    }

    walkprint_ui_draw_line(canvas, y, line_one);
    walkprint_ui_draw_line(canvas, y + 8, line_two);
}

static void walkprint_ui_draw_raw_frame(Canvas* canvas, WalkPrintApp* app) {
    walkprint_ui_draw_line(canvas, 24, "Configured frame:");
    walkprint_ui_draw_hex_wrapped(canvas, 34, app->raw_frame_preview, 20);
    walkprint_ui_draw_line(canvas, 50, app->detail_line);
    walkprint_ui_draw_line(canvas, 58, "OK send Back return");
}

static void walkprint_ui_draw_about(Canvas* canvas) {
    walkprint_ui_draw_line(canvas, 24, "ESP32 bridge over");
    walkprint_ui_draw_line(canvas, 32, "Flipper pins 13/14");
    walkprint_ui_draw_line(canvas, 40, "115200 8N1");
    walkprint_ui_draw_line(canvas, 48, "BT + WiFi via");
    walkprint_ui_draw_line(canvas, 56, "external bridge");
}

void app_ui_draw(Canvas* canvas, WalkPrintApp* app) {
    if(!canvas || !app) {
        return;
    }

    if(app->screen == WalkPrintScreenBusy) {
        canvas_clear(canvas);
        walkprint_ui_draw_busy(canvas, app);
        return;
    }

    walkprint_ui_draw_header(canvas, app);

    switch(app->screen) {
    case WalkPrintScreenMainMenu:
        walkprint_ui_draw_main_menu(canvas, app);
        break;
    case WalkPrintScreenSettings:
        walkprint_ui_draw_settings(canvas, app);
        break;
    case WalkPrintScreenEditAddress:
        walkprint_ui_draw_address_editor(canvas, app);
        break;
    case WalkPrintScreenEditMessage:
        walkprint_ui_draw_busy(canvas, app);
        break;
    case WalkPrintScreenRawFrame:
        walkprint_ui_draw_raw_frame(canvas, app);
        break;
    case WalkPrintScreenAbout:
        walkprint_ui_draw_about(canvas);
        break;
    default:
        break;
    }
}

static bool walkprint_ui_accept_input_type(const InputEvent* input_event) {
    return input_event->type == InputTypeShort || input_event->type == InputTypeRepeat;
}

static void walkprint_ui_handle_main_menu(WalkPrintApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->main_menu_index > 0) {
            app->main_menu_index--;
        }
        break;
    case InputKeyDown:
        if(app->main_menu_index + 1U < walkprint_ui_main_menu_count()) {
            app->main_menu_index++;
        }
        break;
    case InputKeyOk:
        switch(app->main_menu_index) {
        case 0:
            walkprint_app_queue_ping_bridge(app);
            break;
        case 1:
            walkprint_app_queue_discover_printer(app);
            break;
        case 2:
            walkprint_app_queue_connect(app);
            break;
        case 3:
            walkprint_app_queue_send_test_receipt(app);
            break;
        case 4:
            walkprint_app_show_keyboard(app);
            break;
        case 5:
            app->screen = WalkPrintScreenRawFrame;
            walkprint_app_set_status(app, "Raw frame screen", app->raw_frame_preview);
            break;
        case 6:
            walkprint_app_queue_send_feed(app);
            break;
        case 7:
            walkprint_app_queue_scan_wifi(app);
            break;
        case 8:
            app->screen = WalkPrintScreenSettings;
            walkprint_app_set_status(app, "Settings", "Address and density");
            break;
        case 9:
            app->screen = WalkPrintScreenAbout;
            walkprint_app_set_status(app, "About", "ESP32 UART bridge");
            break;
        default:
            break;
        }
        break;
    case InputKeyBack:
        app->running = false;
        break;
    default:
        break;
    }
}

static void walkprint_ui_handle_settings(WalkPrintApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->settings_index > 0) {
            app->settings_index--;
        }
        break;
    case InputKeyDown:
        if(app->settings_index + 1U < walkprint_ui_settings_count()) {
            app->settings_index++;
        }
        break;
    case InputKeyLeft:
        if(app->settings_index == 1) {
            walkprint_app_adjust_density(app, -1);
        } else if(app->settings_index == 2) {
            walkprint_app_adjust_font_size(app, -1);
        }
        break;
    case InputKeyRight:
        if(app->settings_index == 1) {
            walkprint_app_adjust_density(app, 1);
        } else if(app->settings_index == 2) {
            walkprint_app_adjust_font_size(app, 1);
        }
        break;
    case InputKeyOk:
        if(app->settings_index == 0) {
            app->screen = WalkPrintScreenEditAddress;
            walkprint_app_set_status(app, "Editing address", app->printer_address);
        } else if(app->settings_index == 1) {
            walkprint_app_adjust_density(app, 1);
        } else {
            walkprint_app_adjust_font_size(app, 1);
        }
        break;
    case InputKeyBack:
        app->screen = WalkPrintScreenMainMenu;
        walkprint_app_set_status(app, "Ready", app->detail_line);
        break;
    default:
        break;
    }
}

static void walkprint_ui_handle_address_editor(WalkPrintApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyLeft:
        walkprint_app_move_address_cursor(app, -1);
        break;
    case InputKeyRight:
        walkprint_app_move_address_cursor(app, 1);
        break;
    case InputKeyUp:
        walkprint_app_adjust_address_char(app, 1);
        break;
    case InputKeyDown:
        walkprint_app_adjust_address_char(app, -1);
        break;
    case InputKeyOk:
    case InputKeyBack:
        app->screen = WalkPrintScreenSettings;
        walkprint_app_set_status(app, "Address updated", app->printer_address);
        break;
    default:
        break;
    }
}

static void walkprint_ui_handle_raw_frame(WalkPrintApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyOk:
        walkprint_app_queue_send_configured_raw_frame(app);
        break;
    case InputKeyBack:
        app->screen = WalkPrintScreenMainMenu;
        walkprint_app_set_status(app, "Ready", app->detail_line);
        break;
    default:
        break;
    }
}

static void walkprint_ui_handle_about(WalkPrintApp* app, const InputEvent* input_event) {
    if(input_event->key == InputKeyOk || input_event->key == InputKeyBack) {
        app->screen = WalkPrintScreenMainMenu;
        walkprint_app_set_status(app, "Ready", app->detail_line);
    }
}

void app_ui_handle_input(WalkPrintApp* app, const InputEvent* input_event) {
    if(!app || !input_event || !walkprint_ui_accept_input_type(input_event)) {
        return;
    }

    switch(app->screen) {
    case WalkPrintScreenMainMenu:
        walkprint_ui_handle_main_menu(app, input_event);
        break;
    case WalkPrintScreenSettings:
        walkprint_ui_handle_settings(app, input_event);
        break;
    case WalkPrintScreenEditAddress:
        walkprint_ui_handle_address_editor(app, input_event);
        break;
    case WalkPrintScreenBusy:
        break;
    case WalkPrintScreenEditMessage:
        break;
    case WalkPrintScreenRawFrame:
        walkprint_ui_handle_raw_frame(app, input_event);
        break;
    case WalkPrintScreenAbout:
        walkprint_ui_handle_about(app, input_event);
        break;
    default:
        break;
    }

    walkprint_app_request_redraw(app);
}
