#include "navigation.h"

static const app_page_t channel_pages[] = {
    PAGE_CLOCK, PAGE_WEATHER, PAGE_CRYPTO, PAGE_MARKETS, PAGE_NEWS_HOME, PAGE_SETTINGS,
};

static int wrap_index(int value, int count) {
    value %= count;
    return value < 0 ? value + count : value;
}

void navigation_init(navigation_t *navigation) {
    *navigation = (navigation_t){ .page = PAGE_CLOCK, .parent_page = PAGE_CLOCK, .news_category = NEWS_WORLD };
}

void navigation_rotate(navigation_t *navigation, int steps) {
    if (navigation->page == PAGE_NEWS_LIST) {
        navigation->selected_index = (unsigned int)wrap_index((int)navigation->selected_index + steps, 8);
    } else if (navigation->page == PAGE_NEWS_CATEGORY_PICKER) {
        navigation->news_category = (news_category_t)wrap_index((int)navigation->news_category + steps, 3);
    } else if (navigation->page == PAGE_SETTINGS_MENU) {
        navigation->settings_item = (settings_menu_item_t)wrap_index((int)navigation->settings_item + steps, SETTINGS_MENU_COUNT);
    } else if (navigation->page <= PAGE_SETTINGS) {
        navigation->page = channel_pages[wrap_index((int)navigation->page + steps, 6)];
    }
}

void navigation_short_press(navigation_t *navigation) {
    if (navigation->page == PAGE_WEATHER) {
        navigation->weather_forecast = !navigation->weather_forecast;
    } else if (navigation->page == PAGE_NEWS_HOME) {
        navigation->parent_page = PAGE_NEWS_HOME;
        navigation->page = PAGE_NEWS_CATEGORY_PICKER;
    } else if (navigation->page == PAGE_NEWS_CATEGORY_PICKER) {
        navigation->parent_page = PAGE_NEWS_CATEGORY_PICKER;
        navigation->selected_index = 0;
        navigation->page = PAGE_NEWS_LIST;
    } else if (navigation->page == PAGE_NEWS_LIST) {
        navigation->parent_page = PAGE_NEWS_LIST;
        navigation->page = PAGE_NEWS_QR;
    } else if (navigation->page == PAGE_SETTINGS) {
        navigation->parent_page = PAGE_SETTINGS;
        navigation->settings_item = SETTINGS_WIFI;
        navigation->page = PAGE_SETTINGS_MENU;
    } else if (navigation->page == PAGE_SETTINGS_MENU && navigation->settings_item == SETTINGS_DISPLAY_TEST) {
        navigation->parent_page = PAGE_SETTINGS_MENU;
        navigation->page = PAGE_DISPLAY_TEST;
    }
}

bool navigation_long_press(navigation_t *navigation) {
    if (navigation->page == PAGE_PROVISIONING || navigation->page == PAGE_LOADING) return false;
    switch (navigation->page) {
        case PAGE_NEWS_QR: navigation->page = PAGE_NEWS_LIST; return true;
        case PAGE_NEWS_LIST: navigation->page = PAGE_NEWS_CATEGORY_PICKER; return true;
        case PAGE_NEWS_CATEGORY_PICKER: navigation->page = PAGE_NEWS_HOME; return true;
        case PAGE_DISPLAY_TEST: navigation->page = PAGE_SETTINGS_MENU; return true;
        case PAGE_SETTINGS_MENU: navigation->page = PAGE_SETTINGS; return true;
        default: return false;
    }
}

const char *navigation_page_name(app_page_t page) {
    static const char *names[] = { "Clock", "Weather", "Crypto", "Markets", "News", "Settings", "News categories", "News list", "Article QR", "Display test", "Wi-Fi setup", "Loading", "Settings menu" };
    return page <= PAGE_SETTINGS_MENU ? names[page] : "Unknown";
}
