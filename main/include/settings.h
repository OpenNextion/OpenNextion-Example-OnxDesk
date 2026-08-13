#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    HOME_CLOCK = 0,
    HOME_WEATHER,
    HOME_CRYPTO,
    HOME_MARKETS,
    HOME_NEWS,
} home_page_t;

typedef enum {
    ENCODER_SENSITIVITY_LOW = 0,
    ENCODER_SENSITIVITY_MEDIUM = 1,
    ENCODER_SENSITIVITY_HIGH = 2,
} encoder_sensitivity_t;

typedef struct {
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
bool settings_has_market_key(const app_settings_t *settings);
