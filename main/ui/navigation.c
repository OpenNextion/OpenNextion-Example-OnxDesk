#include "navigation.h"

static const app_page_t channel_pages[] = {
    PAGE_CLOCK, PAGE_WEATHER, PAGE_CRYPTO, PAGE_MARKETS, PAGE_FOCUS, PAGE_SETTINGS,
};

static const home_page_t channel_bits[] = {
    HOME_CLOCK, HOME_WEATHER, HOME_CRYPTO, HOME_MARKETS, HOME_FOCUS, HOME_SETTINGS,
};

static int wrap_index(int value, int count) {
    value %= count;
    return value < 0 ? value + count : value;
}

void navigation_init(navigation_t *navigation) {
    *navigation = (navigation_t){ .page = PAGE_CLOCK, .parent_page = PAGE_CLOCK,
                                  .focus_duration_seconds = 25 * 60,
                                  .focus_remaining_seconds = 25 * 60 };
}

void navigation_rotate(navigation_t *navigation, int steps, const app_settings_t *settings) {
    if (navigation->page == PAGE_CRYPTO && navigation->crypto_selecting) {
        navigation->crypto_index = (unsigned int)wrap_index((int)navigation->crypto_index + steps, 3);
    } else if (navigation->page == PAGE_MARKETS && navigation->market_selecting) {
        navigation->market_index = (unsigned int)wrap_index((int)navigation->market_index + steps, 3);
    } else if (navigation->page == PAGE_SETTINGS_MENU) {
        navigation->settings_item = (settings_menu_item_t)wrap_index((int)navigation->settings_item + steps, SETTINGS_MENU_COUNT);
    } else if (navigation->page == PAGE_HOME_PAGES) {
        navigation->home_pages_item = (unsigned int)wrap_index((int)navigation->home_pages_item + steps, 4);
    } else if (navigation->page == PAGE_FOCUS && navigation->focus_adjusting && !navigation->focus_running) {
        int duration = (int)(navigation->focus_remaining_seconds == 0 ? navigation->focus_duration_seconds : navigation->focus_remaining_seconds);
        const int direction = steps < 0 ? -1 : 1;
        for (int move = 0; move < (steps < 0 ? -steps : steps); move++) {
            if (direction < 0) duration = duration <= 5 * 60 ? 60 : duration - 5 * 60;
            else duration = duration < 5 * 60 ? 5 * 60 : duration + 5 * 60;
            if (duration > 120 * 60) duration = 120 * 60;
        }
        navigation->focus_duration_seconds = (uint32_t)duration;
        navigation->focus_remaining_seconds = (uint32_t)duration;
    } else if (navigation->page <= PAGE_SETTINGS) {
        int index = (int)navigation->page;
        const int direction = steps < 0 ? -1 : 1;
        for (int move = 0; move < (steps < 0 ? -steps : steps); move++) {
            for (int attempt = 0; attempt < 6; attempt++) {
                index = wrap_index(index + direction, 6);
                if (settings_home_page_enabled(settings, channel_bits[index])) break;
            }
        }
        navigation->page = channel_pages[index];
    }
}

void navigation_short_press(navigation_t *navigation) {
    if (navigation->page == PAGE_WEATHER) {
        navigation->weather_forecast = !navigation->weather_forecast;
    } else if (navigation->page == PAGE_CRYPTO) {
        navigation->crypto_selecting = !navigation->crypto_selecting;
    } else if (navigation->page == PAGE_MARKETS) {
        navigation->market_selecting = !navigation->market_selecting;
    } else if (navigation->page == PAGE_CITY_SETUP) {
        navigation->city_setup_show_qr = true;
    } else if (navigation->page == PAGE_SETTINGS) {
        navigation->parent_page = PAGE_SETTINGS;
        navigation->settings_item = SETTINGS_HOME_PAGES;
        navigation->page = PAGE_SETTINGS_MENU;
    } else if (navigation->page == PAGE_SETTINGS_MENU && navigation->settings_item == SETTINGS_HOME_PAGES) {
        navigation->parent_page = PAGE_SETTINGS_MENU;
        navigation->page = PAGE_HOME_PAGES;
    } else if (navigation->page == PAGE_SETTINGS_MENU && navigation->settings_item == SETTINGS_DISPLAY_TEST) {
        navigation->parent_page = PAGE_SETTINGS_MENU;
        navigation->page = PAGE_DISPLAY_TEST;
    } else if (navigation->page == PAGE_SETTINGS_MENU && navigation->settings_item == SETTINGS_CITY) {
        navigation->parent_page = PAGE_SETTINGS_MENU;
        navigation->city_setup_from_settings = true;
        navigation->city_setup_show_qr = false;
        navigation->page = PAGE_CITY_SETUP;
    } else if (navigation->page == PAGE_SETTINGS_MENU &&
               (navigation->settings_item == SETTINGS_WIFI || navigation->settings_item == SETTINGS_FINNHUB)) {
        navigation->parent_page = PAGE_SETTINGS_MENU;
        navigation->page = PAGE_CONFIG_URL;
    }
}

bool navigation_long_press(navigation_t *navigation) {
    if (navigation->page == PAGE_PROVISIONING || navigation->page == PAGE_LOADING) return false;
    switch (navigation->page) {
        case PAGE_CRYPTO:
            if (navigation->crypto_selecting) {
                navigation->crypto_selecting = false;
                return true;
            }
            return false;
        case PAGE_MARKETS:
            if (navigation->market_selecting) {
                navigation->market_selecting = false;
                return true;
            }
            return false;
        case PAGE_HOME_PAGES: navigation->page = PAGE_SETTINGS_MENU; return true;
        case PAGE_DISPLAY_TEST: navigation->page = PAGE_SETTINGS_MENU; return true;
        case PAGE_CONFIG_URL: navigation->page = navigation->parent_page; return true;
        case PAGE_CITY_SETUP:
            if (navigation->city_setup_show_qr) {
                navigation->city_setup_show_qr = false;
                return true;
            }
            if (navigation->city_setup_from_settings) {
                navigation->city_setup_from_settings = false;
                navigation->page = navigation->parent_page;
                return true;
            }
            return false;
        case PAGE_SETTINGS_MENU: navigation->page = PAGE_SETTINGS; return true;
        default: return false;
    }
}

const char *navigation_page_name(app_page_t page) {
    static const char *names[] = { "Clock", "Weather", "Crypto", "Markets", "Focus", "Settings", "Display test", "Wi-Fi setup", "Loading", "Settings menu", "Local configuration", "City setup", "Wi-Fi test", "Home pages" };
    return page <= PAGE_HOME_PAGES ? names[page] : "Unknown";
}
