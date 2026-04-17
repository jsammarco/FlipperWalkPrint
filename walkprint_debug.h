#pragma once

#include <stddef.h>
#include <stdint.h>

void walkprint_debug_log_info(const char* fmt, ...);
void walkprint_debug_log_warn(const char* fmt, ...);
void walkprint_debug_log_error(const char* fmt, ...);
void walkprint_debug_log_frame(const char* label, const uint8_t* data, size_t length);
void walkprint_debug_format_hex_preview(
    const uint8_t* data,
    size_t length,
    char* out,
    size_t out_size);
