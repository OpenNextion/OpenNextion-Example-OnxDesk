#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    HOME_CLOCK = 1 << 0,
    HOME_WEATHER = 1 << 1,
    HOME_CRYPTO = 1 << 2,
    HOME_MARKETS = 1 << 3,
    HOME_FOCUS = 1 << 4,
    HOME_SETTINGS = 1 << 5,
} home_page_t;

#define HOME_PAGE_MASK_DEFAULT (HOME_CLOCK | HOME_WEATHER | HOME_CRYPTO | HOME_MARKETS | HOME_FOCUS | HOME_SETTINGS)
#define HOME_PAGE_MASK_OPTIONAL (HOME_WEATHER | HOME_CRYPTO | HOME_MARKETS | HOME_FOCUS)

typedef enum {
    ENCODER_SENSITIVITY_LOW = 0,
    ENCODER_SENSITIVITY_MEDIUM = 1,
    ENCODER_SENSITIVITY_HIGH = 2,
} encoder_sensitivity_t;

typedef struct app_settings {
    home_page_t home_page;
    uint8_t brightness_percent;
    uint8_t encoder_step; /* encoder_sensitivity_t; Medium is the factory default */
    char city[64];
    double latitude;
    double longitude;
    char timezone[48];
    char finnhub_api_key[96];
} app_settings_t;

esp_err_t settings_init(void);
esp_err_t settings_load(app_settings_t *settings);
esp_err_t settings_save(const app_settings_t *settings);
esp_err_t settings_factory_reset(void);
esp_err_t settings_get_setup_ssid(char *ssid, size_t ssid_size);
bool settings_has_market_key(const app_settings_t *settings);
bool settings_home_page_enabled(const app_settings_t *settings, home_page_t page);
bool settings_toggle_optional_home_page(app_settings_t *settings, home_page_t page);
