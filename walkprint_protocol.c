#include "walkprint_protocol.h"

#include "walkprint_config.h"
#include "walkprint_debug.h"

#include <stdio.h>
#include <string.h>

#define WALKPRINT_TEST_BITMAP_WIDTH 384U
#define WALKPRINT_TEST_BITMAP_WIDTH_BYTES (WALKPRINT_TEST_BITMAP_WIDTH / 8U)
#define WALKPRINT_TEST_BITMAP_HEIGHT 96U
#define WALKPRINT_TEST_FONT_WIDTH 5U
#define WALKPRINT_TEST_FONT_HEIGHT 7U
#define WALKPRINT_TEST_FONT_SPACING 1U
#define WALKPRINT_TEST_FONT_DEFAULT_SCALE 2U

typedef struct {
    char ch;
    uint8_t columns[WALKPRINT_TEST_FONT_WIDTH];
} WalkPrintGlyph;

static const WalkPrintGlyph walkprint_test_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'"', {0x00, 0x07, 0x00, 0x07, 0x00}},
    {'$', {0x24, 0x2A, 0x7F, 0x2A, 0x12}},
    {'\'', {0x00, 0x03, 0x07, 0x00, 0x00}},
    {'(', {0x00, 0x1C, 0x22, 0x41, 0x00}},
    {')', {0x00, 0x41, 0x22, 0x1C, 0x00}},
    {'*', {0x14, 0x08, 0x3E, 0x08, 0x14}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {',', {0x00, 0x40, 0x20, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'\\', {0x02, 0x04, 0x08, 0x10, 0x20}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static uint8_t walkprint_test_raster_buffer
    [WALKPRINT_TEST_BITMAP_WIDTH_BYTES * WALKPRINT_TEST_BITMAP_HEIGHT];

const char* walkprint_protocol_font_family_name(WalkPrintFontFamily family) {
    switch(family) {
    case WalkPrintFontFamilyBold:
        return "Bold";
    case WalkPrintFontFamilySlant:
        return "Slant";
    case WalkPrintFontFamilyClassic:
    default:
        return "Classic";
    }
}

const char* walkprint_protocol_orientation_name(WalkPrintOrientation orientation) {
    switch(orientation) {
    case WalkPrintOrientationNormal:
        return "Normal";
    case WalkPrintOrientationUpsideDown:
    default:
        return "UpsideDown";
    }
}

static void walkprint_protocol_reset_frame(WalkPrintFrame* frame) {
    if(frame) {
        frame->length = 0;
        memset(frame->data, 0, sizeof(frame->data));
    }
}

static bool walkprint_protocol_append(WalkPrintFrame* frame, const uint8_t* data, size_t length) {
    if(!frame || !data || length == 0) {
        return false;
    }

    if(frame->length + length > sizeof(frame->data)) {
        return false;
    }

    memcpy(&frame->data[frame->length], data, length);
    frame->length += length;
    return true;
}

static const WalkPrintGlyph* walkprint_protocol_find_glyph(char ch) {
    const char normalized = (char)(((ch >= 'a') && (ch <= 'z')) ? (ch - 32) : ch);

    for(size_t i = 0; i < sizeof(walkprint_test_font) / sizeof(walkprint_test_font[0]); i++) {
        if(walkprint_test_font[i].ch == normalized) {
            return &walkprint_test_font[i];
        }
    }

    return &walkprint_test_font[0];
}

static void walkprint_protocol_set_pixel(
    uint8_t* raster,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    WalkPrintOrientation orientation) {
    size_t mapped_x = x;
    size_t mapped_y = y;
    size_t byte_index;
    uint8_t bit_mask;

    if(orientation == WalkPrintOrientationNormal) {
        mapped_x = (width - 1U) - x;
        mapped_y = (height - 1U) - y;
    }

    byte_index = mapped_y * WALKPRINT_TEST_BITMAP_WIDTH_BYTES + (mapped_x / 8U);
    bit_mask = (uint8_t)(1U << (7U - (mapped_x % 8U)));

    raster[byte_index] |= bit_mask;
}

static void walkprint_protocol_set_styled_pixel(
    uint8_t* raster,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    WalkPrintFontFamily family,
    WalkPrintOrientation orientation) {
    size_t styled_x = x;

    if(family == WalkPrintFontFamilySlant) {
        styled_x += y / 3U;
    }

    if(styled_x < width && y < height) {
        walkprint_protocol_set_pixel(raster, styled_x, y, width, height, orientation);
    }

    if(family == WalkPrintFontFamilyBold && (styled_x + 1U) < width) {
        walkprint_protocol_set_pixel(raster, styled_x + 1U, y, width, height, orientation);
    }
}

static void walkprint_protocol_draw_text_line(
    uint8_t* raster,
    size_t y_offset,
    const char* text,
    uint8_t font_scale,
    WalkPrintFontFamily family,
    uint8_t char_spacing,
    WalkPrintOrientation orientation) {
    size_t x_offset = 0;

    if(!raster || !text) {
        return;
    }

    for(size_t i = 0; text[i] != '\0'; i++) {
        const WalkPrintGlyph* glyph = walkprint_protocol_find_glyph(text[i]);

        for(size_t column = 0; column < WALKPRINT_TEST_FONT_WIDTH; column++) {
            const uint8_t bits = glyph->columns[column];

            for(size_t row = 0; row < WALKPRINT_TEST_FONT_HEIGHT; row++) {
                if(bits & (1U << row)) {
                    for(size_t sx = 0; sx < font_scale; sx++) {
                        for(size_t sy = 0; sy < font_scale; sy++) {
                            const size_t px =
                                x_offset + (column * font_scale) + sx;
                            const size_t py =
                                y_offset + (row * font_scale) + sy;

                            if(px < WALKPRINT_TEST_BITMAP_WIDTH && py < WALKPRINT_TEST_BITMAP_HEIGHT) {
                                walkprint_protocol_set_styled_pixel(
                                    raster,
                                    px,
                                    py,
                                    WALKPRINT_TEST_BITMAP_WIDTH,
                                    WALKPRINT_TEST_BITMAP_HEIGHT,
                                    family,
                                    orientation);
                            }
                        }
                    }
                }
            }
        }

        x_offset += (WALKPRINT_TEST_FONT_WIDTH * font_scale) +
                    WALKPRINT_TEST_FONT_SPACING + (size_t)char_spacing;
    }
}

static void walkprint_protocol_draw_text_block(
    uint8_t* raster,
    const char* text,
    uint8_t font_scale,
    WalkPrintFontFamily family,
    uint8_t char_spacing,
    WalkPrintOrientation orientation) {
    size_t line_start = 0;
    size_t y_offset = 2U;
    const size_t text_length = text ? strlen(text) : 0U;
    const size_t line_height = (WALKPRINT_TEST_FONT_HEIGHT * font_scale) + 4U;

    if(!raster || !text || line_height == 0U) {
        return;
    }

    for(size_t i = 0; i <= text_length; i++) {
        if(text[i] == '\n' || text[i] == '\0') {
            char line_buffer[257];
            size_t line_length = i - line_start;

            if(line_length >= sizeof(line_buffer)) {
                line_length = sizeof(line_buffer) - 1U;
            }

            memcpy(line_buffer, &text[line_start], line_length);
            line_buffer[line_length] = '\0';

            if(y_offset + (WALKPRINT_TEST_FONT_HEIGHT * font_scale) > WALKPRINT_TEST_BITMAP_HEIGHT) {
                break;
            }

            walkprint_protocol_draw_text_line(
                raster,
                y_offset,
                line_buffer,
                font_scale,
                family,
                char_spacing,
                orientation);

            y_offset += line_height;
            line_start = i + 1U;
        }
    }
}

static bool walkprint_protocol_build_bitmap_receipt(
    const char* message,
    bool include_density_line,
    uint8_t density,
    uint8_t font_scale,
    WalkPrintFontFamily font_family,
    uint8_t char_spacing,
    WalkPrintOrientation orientation,
    WalkPrintFrame* out_frame) {
    char density_line[16];
    uint8_t header[8];
    uint8_t clamped_scale = font_scale;

    if(!message || !out_frame) {
        return false;
    }

    if(font_family >= WalkPrintFontFamilyCount) {
        font_family = WalkPrintFontFamilyClassic;
    }
    if(orientation >= WalkPrintOrientationCount) {
        orientation = WalkPrintOrientationUpsideDown;
    }

    if(clamped_scale < 1U) {
        clamped_scale = 1U;
    } else if(clamped_scale > 10U) {
        clamped_scale = 10U;
    }

    walkprint_protocol_reset_frame(out_frame);
    memset(walkprint_test_raster_buffer, 0, sizeof(walkprint_test_raster_buffer));

    walkprint_protocol_draw_text_block(
        walkprint_test_raster_buffer,
        message,
        clamped_scale,
        font_family,
        char_spacing,
        orientation);
    if(include_density_line) {
        snprintf(density_line, sizeof(density_line), "DARK %u", density);
        walkprint_protocol_draw_text_line(
            walkprint_test_raster_buffer,
            4U + (WALKPRINT_TEST_FONT_HEIGHT * clamped_scale) + 4U,
            density_line,
            1U,
            WalkPrintFontFamilyClassic,
            1U,
            orientation);
    }

    header[0] = 0x1D;
    header[1] = 0x76;
    header[2] = 0x30;
    header[3] = 0x00;
    header[4] = (uint8_t)(WALKPRINT_TEST_BITMAP_WIDTH_BYTES & 0xFFU);
    header[5] = (uint8_t)((WALKPRINT_TEST_BITMAP_WIDTH_BYTES >> 8U) & 0xFFU);
    header[6] = (uint8_t)(WALKPRINT_TEST_BITMAP_HEIGHT & 0xFFU);
    header[7] = (uint8_t)((WALKPRINT_TEST_BITMAP_HEIGHT >> 8U) & 0xFFU);

    return walkprint_protocol_append(out_frame, header, sizeof(header)) &&
           walkprint_protocol_append(
               out_frame, walkprint_test_raster_buffer, sizeof(walkprint_test_raster_buffer));
}

void walkprint_protocol_init(WalkPrintProtocol* protocol) {
    if(!protocol) {
        return;
    }

    protocol->initialized = true;
    walkprint_debug_log_info("Protocol initialized");
}

bool walkprint_protocol_build_test_receipt(
    WalkPrintProtocol* protocol,
    const char* const* lines,
    size_t line_count,
    uint8_t density,
    WalkPrintFrame* out_frame) {
    UNUSED(lines);
    UNUSED(line_count);

    if(!protocol || !protocol->initialized || !out_frame) {
        return false;
    }

    if(!walkprint_protocol_build_bitmap_receipt(
           "TEST PRINT",
           true,
           density,
           WALKPRINT_TEST_FONT_DEFAULT_SCALE,
           WalkPrintFontFamilyClassic,
           1U,
           WalkPrintOrientationUpsideDown,
           out_frame)) {
        return false;
    }

    walkprint_debug_log_frame("Built test receipt image", out_frame->data, out_frame->length);
    return true;
}

bool walkprint_protocol_build_message_receipt(
    WalkPrintProtocol* protocol,
    const char* message,
    uint8_t density,
    uint8_t font_scale,
    WalkPrintFontFamily font_family,
    uint8_t char_spacing,
    WalkPrintOrientation orientation,
    WalkPrintFrame* out_frame) {
    if(!protocol || !protocol->initialized || !message || !out_frame) {
        return false;
    }

    if(!walkprint_protocol_build_bitmap_receipt(
           message,
           false,
           density,
           font_scale,
           font_family,
           char_spacing,
           orientation,
           out_frame)) {
        return false;
    }

    walkprint_debug_log_frame("Built message receipt image", out_frame->data, out_frame->length);
    return true;
}

bool walkprint_protocol_build_feed(
    WalkPrintProtocol* protocol,
    uint8_t line_count,
    WalkPrintFrame* out_frame) {
    if(!protocol || !protocol->initialized || !out_frame) {
        return false;
    }

    walkprint_protocol_reset_frame(out_frame);

    for(uint8_t i = 0; i < line_count; i++) {
        if(!walkprint_protocol_append(
               out_frame, walkprint_config_feed_frame, WALKPRINT_FEED_FRAME_LEN)) {
            return false;
        }
    }

    walkprint_debug_log_frame("Built feed frame", out_frame->data, out_frame->length);
    return true;
}

bool walkprint_protocol_build_raw(
    WalkPrintProtocol* protocol,
    const uint8_t* raw_data,
    size_t raw_length,
    WalkPrintFrame* out_frame) {
    if(!protocol || !protocol->initialized || !raw_data || raw_length == 0 || !out_frame) {
        return false;
    }

    walkprint_protocol_reset_frame(out_frame);

    if(!walkprint_protocol_append(out_frame, raw_data, raw_length)) {
        return false;
    }

    walkprint_debug_log_frame("Built raw frame", out_frame->data, out_frame->length);
    return true;
}

bool walkprint_protocol_send_frame(
    WalkPrintProtocol* protocol,
    WalkPrintTransport* transport,
    const WalkPrintFrame* frame) {
    if(!protocol || !protocol->initialized || !transport || !frame || frame->length == 0) {
        return false;
    }

    if(!walkprint_transport_is_connected(transport)) {
        walkprint_debug_log_warn("Refusing to send frame while disconnected");
        return false;
    }

    walkprint_debug_log_frame("Sending frame", frame->data, frame->length);
    return walkprint_transport_send(transport, frame->data, frame->length);
}
