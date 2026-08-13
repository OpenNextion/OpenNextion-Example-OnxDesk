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
} app_page_t;

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
    bool weather_forecast;
} navigation_t;

void navigation_init(navigation_t *navigation);
void navigation_rotate(navigation_t *navigation, int steps);
void navigation_short_press(navigation_t *navigation);
bool navigation_long_press(navigation_t *navigation);
const char *navigation_page_name(app_page_t page);
