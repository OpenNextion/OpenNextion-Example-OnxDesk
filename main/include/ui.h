#pragma once

#include "board.h"
#include "navigation.h"
#include "settings.h"

esp_err_t app_ui_init(const board_display_t *display);
void app_ui_render(const navigation_t *navigation, const app_settings_t *settings);
