#pragma once

#include "walkprint_app.h"

#include <gui/canvas.h>

void app_ui_draw(Canvas* canvas, WalkPrintApp* app);
void app_ui_handle_input(WalkPrintApp* app, const InputEvent* input_event);
