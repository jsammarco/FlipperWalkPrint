#include "app_ui.h"

#include <stdio.h>
#include <string.h>

static const char* const walkprint_main_menu_items[] = {
    "Ping Bridge",
    "Discover Printer",
    "Connect",
    "Print Message",
    "Print BMP",
    "Feed Paper",
    "WiFi Scan",
    "Settings",
    "About",
};

static const char* const walkprint_settings_items[] = {
    "Printer Address",
    "Density",
    "Font Size",
    "Char Spacing",
    "Font Family",
    "Orientation",
};

#define WALKPRINT_SETTINGS_VISIBLE_ITEMS 5U
#define WALKPRINT_WIFI_VISIBLE_ITEMS 5U

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
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 12, WALKPRINT_APP_NAME);
    canvas_draw_str(canvas, 94, 12, walkprint_app_connection_label(app));
    walkprint_ui_draw_line(canvas, 26, app->status_line);
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
    walkprint_ui_draw_line(canvas, 12, app->status_line);
    canvas_set_font(canvas, FontSecondary);
    walkprint_ui_draw_line(canvas, 28, app->detail_line);
    walkprint_ui_draw_line(canvas, 40, "Bridge and printer");
    walkprint_ui_draw_line(canvas, 48, "can take a moment.");
    walkprint_ui_draw_line(canvas, 58, "Please wait...");
}

static void walkprint_ui_draw_settings(Canvas* canvas, WalkPrintApp* app) {
    char line[WALKPRINT_HEX_PREVIEW_SIZE];
    const char* family_name = walkprint_protocol_font_family_name(app->font_family);
    const char* orientation_name = walkprint_protocol_orientation_name(app->orientation);
    size_t item_count = walkprint_ui_settings_count();
    size_t first_visible = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    walkprint_ui_draw_line(canvas, 12, "Settings");
    canvas_set_font(canvas, FontSecondary);
    walkprint_ui_draw_line(canvas, 22, "L/R change  Back done");

    if(app->settings_index >= WALKPRINT_SETTINGS_VISIBLE_ITEMS) {
        first_visible = app->settings_index - (WALKPRINT_SETTINGS_VISIBLE_ITEMS - 1U);
    }

    for(size_t i = 0; i < WALKPRINT_SETTINGS_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        uint8_t y = 34 + (uint8_t)(i * 8U);

        if(item_index >= item_count) {
            break;
        }

        switch(item_index) {
        case 0:
            snprintf(
                line,
                sizeof(line),
                "%c Addr: %s",
                (item_index == app->settings_index) ? '>' : ' ',
                walkprint_app_printer_address_label(app));
            break;
        case 1:
            snprintf(
                line,
                sizeof(line),
                "%c Density: %u",
                (item_index == app->settings_index) ? '>' : ' ',
                app->density);
            break;
        case 2:
            snprintf(
                line,
                sizeof(line),
                "%c Font: %u",
                (item_index == app->settings_index) ? '>' : ' ',
                app->font_size);
            break;
        case 3:
            snprintf(
                line,
                sizeof(line),
                "%c Spacing: +%u",
                (item_index == app->settings_index) ? '>' : ' ',
                app->char_spacing);
            break;
        case 4:
            snprintf(
                line,
                sizeof(line),
                "%c Family: %s",
                (item_index == app->settings_index) ? '>' : ' ',
                family_name);
            break;
        case 5:
            snprintf(
                line,
                sizeof(line),
                "%c Flip: %s",
                (item_index == app->settings_index) ? '>' : ' ',
                orientation_name);
            break;
        default:
            snprintf(
                line, sizeof(line), "%c ?", (item_index == app->settings_index) ? '>' : ' ');
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
    walkprint_ui_draw_line(canvas, 34, app->address_edit_buffer);
    walkprint_ui_draw_line(canvas, 42, cursor_line);
    walkprint_ui_draw_line(canvas, 50, "L/R move U/D change");
    walkprint_ui_draw_line(canvas, 58, "OK/Back save");
}

static void walkprint_ui_draw_about(Canvas* canvas) {
    walkprint_ui_draw_line(canvas, 24, "WalkPrint by");
    walkprint_ui_draw_line(canvas, 32, "ConsultingJoe.com");
    walkprint_ui_draw_line(canvas, 40, "ESP32 bridge over");
    walkprint_ui_draw_line(canvas, 48, "Flipper pins 13/14");
    walkprint_ui_draw_line(canvas, 56, "115200 8N1");
}

static void walkprint_ui_draw_wifi_results(Canvas* canvas, WalkPrintApp* app) {
    size_t first_visible = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    walkprint_ui_draw_line(canvas, 12, "WiFi Results");
    canvas_set_font(canvas, FontSecondary);
    walkprint_ui_draw_line(canvas, 22, "Up/Down scroll  Back done");

    if(app->wifi_results_index >= WALKPRINT_WIFI_VISIBLE_ITEMS) {
        first_visible = app->wifi_results_index - (WALKPRINT_WIFI_VISIBLE_ITEMS - 1U);
    }

    if(app->transport.wifi_network_count == 0) {
        walkprint_ui_draw_line(canvas, 38, "No networks found");
        return;
    }

    for(size_t i = 0; i < WALKPRINT_WIFI_VISIBLE_ITEMS; i++) {
        char line[WALKPRINT_STATUS_TEXT_SIZE + 4U];
        size_t item_index = first_visible + i;
        uint8_t y = 34 + (uint8_t)(i * 8U);

        if(item_index >= app->transport.wifi_network_count) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %s",
            (item_index == app->wifi_results_index) ? '>' : ' ',
            app->transport.wifi_networks[item_index]);
        walkprint_ui_draw_line(canvas, y, line);
    }
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

    switch(app->screen) {
    case WalkPrintScreenMainMenu:
        walkprint_ui_draw_header(canvas, app);
        walkprint_ui_draw_main_menu(canvas, app);
        break;
    case WalkPrintScreenSettings:
        walkprint_ui_draw_settings(canvas, app);
        break;
    case WalkPrintScreenWifiResults:
        walkprint_ui_draw_wifi_results(canvas, app);
        break;
    case WalkPrintScreenEditAddress:
        walkprint_ui_draw_header(canvas, app);
        walkprint_ui_draw_address_editor(canvas, app);
        break;
    case WalkPrintScreenEditMessage:
        walkprint_ui_draw_busy(canvas, app);
        break;
    case WalkPrintScreenAbout:
        walkprint_ui_draw_header(canvas, app);
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
            walkprint_app_show_keyboard(app);
            break;
        case 4:
            if(walkprint_app_select_bmp(app)) {
                walkprint_app_queue_send_bmp(app);
            }
            break;
        case 5:
            walkprint_app_queue_send_feed(app);
            break;
        case 6:
            walkprint_app_queue_scan_wifi(app);
            break;
        case 7:
            app->screen = WalkPrintScreenSettings;
            walkprint_app_set_status(app, "Settings", "Address font and density");
            break;
        case 8:
            app->screen = WalkPrintScreenAbout;
            walkprint_app_set_status(app, "About", "ConsultingJoe.com");
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
        } else if(app->settings_index == 3) {
            walkprint_app_adjust_char_spacing(app, -1);
        } else if(app->settings_index == 4) {
            walkprint_app_adjust_font_family(app, -1);
        } else if(app->settings_index == 5) {
            walkprint_app_adjust_orientation(app, -1);
        }
        break;
    case InputKeyRight:
        if(app->settings_index == 1) {
            walkprint_app_adjust_density(app, 1);
        } else if(app->settings_index == 2) {
            walkprint_app_adjust_font_size(app, 1);
        } else if(app->settings_index == 3) {
            walkprint_app_adjust_char_spacing(app, 1);
        } else if(app->settings_index == 4) {
            walkprint_app_adjust_font_family(app, 1);
        } else if(app->settings_index == 5) {
            walkprint_app_adjust_orientation(app, 1);
        }
        break;
    case InputKeyOk:
        if(app->settings_index == 0) {
            walkprint_app_begin_address_edit(app);
            app->screen = WalkPrintScreenEditAddress;
            walkprint_app_set_status(app, "Editing address", app->address_edit_buffer);
        } else if(app->settings_index == 1) {
            walkprint_app_adjust_density(app, 1);
        } else if(app->settings_index == 2) {
            walkprint_app_adjust_font_size(app, 1);
        } else if(app->settings_index == 3) {
            walkprint_app_adjust_char_spacing(app, 1);
        } else if(app->settings_index == 4) {
            walkprint_app_adjust_font_family(app, 1);
        } else {
            walkprint_app_adjust_orientation(app, 1);
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

static void walkprint_ui_handle_wifi_results(WalkPrintApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->wifi_results_index > 0) {
            app->wifi_results_index--;
        }
        break;
    case InputKeyDown:
        if(app->wifi_results_index + 1U < app->transport.wifi_network_count) {
            app->wifi_results_index++;
        }
        break;
    case InputKeyBack:
    case InputKeyOk:
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
        walkprint_app_commit_address_edit(app);
        app->screen = WalkPrintScreenSettings;
        walkprint_app_set_status(app, "Address updated", app->printer_address);
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
    case WalkPrintScreenWifiResults:
        walkprint_ui_handle_wifi_results(app, input_event);
        break;
    case WalkPrintScreenEditAddress:
        walkprint_ui_handle_address_editor(app, input_event);
        break;
    case WalkPrintScreenBusy:
        break;
    case WalkPrintScreenEditMessage:
        break;
    case WalkPrintScreenAbout:
        walkprint_ui_handle_about(app, input_event);
        break;
    default:
        break;
    }

    walkprint_app_request_redraw(app);
}
