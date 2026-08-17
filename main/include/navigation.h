#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

typedef enum {
    PAGE_CLOCK = 0,
    PAGE_WEATHER,
    PAGE_CRYPTO,
    PAGE_MARKETS,
    PAGE_FOCUS,
    PAGE_SETTINGS,
    PAGE_DISPLAY_TEST,
    PAGE_PROVISIONING,
    PAGE_LOADING,
    PAGE_SETTINGS_MENU,
    PAGE_CONFIG_URL,
    PAGE_CITY_SETUP,
    PAGE_WIFI_TEST,
    PAGE_HOME_PAGES,
} app_page_t;

typedef enum {
    SETTINGS_HOME_PAGES = 0,
    SETTINGS_WIFI,
    SETTINGS_CITY,
    SETTINGS_SENSITIVITY,
    SETTINGS_FINNHUB,
    SETTINGS_ABOUT,
    SETTINGS_DISPLAY_TEST,
    SETTINGS_MENU_COUNT,
} settings_menu_item_t;

typedef struct {
    app_page_t page;
    app_page_t parent_page;
    unsigned int crypto_index;
    bool crypto_selecting;
    unsigned int market_index;
    bool market_selecting;
    settings_menu_item_t settings_item;
    bool weather_forecast;
    bool city_setup_show_qr;
    bool city_setup_from_settings;
    unsigned int home_pages_item;
    uint32_t focus_duration_seconds;
    uint32_t focus_remaining_seconds;
    int64_t focus_deadline_us;
    int64_t focus_alert_until_us;
    bool focus_running;
    bool focus_paused;
    bool focus_adjusting;
    bool focus_alert_flash_on;
} navigation_t;

void navigation_init(navigation_t *navigation);
void navigation_rotate(navigation_t *navigation, int steps, const app_settings_t *settings);
void navigation_short_press(navigation_t *navigation);
bool navigation_long_press(navigation_t *navigation);
const char *navigation_page_name(app_page_t page);
