#include "walkprint_debug.h"
#include "walkprint_config.h"

#include <furi.h>

#include <stdarg.h>
#include <stdio.h>

#define WALKPRINT_LOG_TAG "WalkPrint"
#define WALKPRINT_LOG_BUFFER_SIZE 128

void walkprint_debug_log_info(const char* fmt, ...) {
    char buffer[WALKPRINT_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    FURI_LOG_I(WALKPRINT_LOG_TAG, "%s", buffer);
}

void walkprint_debug_log_warn(const char* fmt, ...) {
    char buffer[WALKPRINT_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    FURI_LOG_W(WALKPRINT_LOG_TAG, "%s", buffer);
}

void walkprint_debug_log_error(const char* fmt, ...) {
    char buffer[WALKPRINT_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    FURI_LOG_E(WALKPRINT_LOG_TAG, "%s", buffer);
}

void walkprint_debug_format_hex_preview(
    const uint8_t* data,
    size_t length,
    char* out,
    size_t out_size) {
    size_t offset = 0;

    if(!out || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if(!data || length == 0) {
        snprintf(out, out_size, "<empty>");
        return;
    }

    for(size_t i = 0; i < length && offset + 4 < out_size; i++) {
        int written = snprintf(
            &out[offset],
            out_size - offset,
            (i + 1U < length) ? "%02X " : "%02X",
            data[i]);
        if(written < 0) {
            break;
        }

        offset += (size_t)written;
        if(offset >= out_size) {
            out[out_size - 1U] = '\0';
            break;
        }
    }

    if(length > 0 && offset >= out_size - 1U) {
        out[out_size - 1U] = '\0';
    }
}

void walkprint_debug_log_frame(const char* label, const uint8_t* data, size_t length) {
    char preview[WALKPRINT_HEX_PREVIEW_SIZE];
    walkprint_debug_format_hex_preview(data, length, preview, sizeof(preview));
    walkprint_debug_log_info("%s (%u bytes): %s", label, (unsigned)length, preview);
}
