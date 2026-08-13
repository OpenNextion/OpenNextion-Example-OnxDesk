#pragma once

#include <stdbool.h>

typedef enum {
    PAGE_CLOCK = 0,
    PAGE_WEATHER,
    PAGE_CRYPTO,
    PAGE_MARKETS,
    PAGE_NEWS_HOME,
    PAGE_SETTINGS,
    PAGE_NEWS_CATEGORY_PICKER,
    PAGE_NEWS_LIST,
    PAGE_NEWS_QR,
    PAGE_DISPLAY_TEST,
    PAGE_PROVISIONING,
    PAGE_LOADING,
    PAGE_SETTINGS_MENU,
    PAGE_CONFIG_URL,
    PAGE_CITY_SETUP,
} app_page_t;

typedef enum {
    SETTINGS_WIFI = 0,
    SETTINGS_CITY,
    SETTINGS_SENSITIVITY,
    SETTINGS_FINNHUB,
    SETTINGS_ABOUT,
    SETTINGS_DISPLAY_TEST,
    SETTINGS_MENU_COUNT,
} settings_menu_item_t;

typedef enum {
    NEWS_WORLD = 0,
    NEWS_BUSINESS,
    NEWS_TECHNOLOGY,
} news_category_t;

typedef struct {
    app_page_t page;
    app_page_t parent_page;
    news_category_t news_category;
    unsigned int selected_index;
    settings_menu_item_t settings_item;
    bool weather_forecast;
    bool city_setup_show_qr;
} navigation_t;

void navigation_init(navigation_t *navigation);
void navigation_rotate(navigation_t *navigation, int steps);
void navigation_short_press(navigation_t *navigation);
bool navigation_long_press(navigation_t *navigation);
const char *navigation_page_name(app_page_t page);
