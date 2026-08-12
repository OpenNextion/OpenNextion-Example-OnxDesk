#pragma once

#include "board.h"
#include "navigation.h"

esp_err_t app_ui_init(const board_display_t *display);
void app_ui_render(const navigation_t *navigation);
