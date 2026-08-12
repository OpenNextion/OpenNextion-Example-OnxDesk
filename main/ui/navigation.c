#include "navigation.h"

static const app_page_t channel_pages[] = {
    PAGE_CLOCK, PAGE_WEATHER, PAGE_CRYPTO, PAGE_MARKETS, PAGE_NEWS_HOME, PAGE_SETTINGS,
};

void navigation_init(navigation_t *navigation) {
    *navigation = (navigation_t){ .page = PAGE_CLOCK, .parent_page = PAGE_CLOCK, .news_category = NEWS_WORLD };
}

void navigation_rotate(navigation_t *navigation, int steps) {
    if (navigation->page == PAGE_NEWS_LIST) {
        navigation->selected_index = (unsigned int)((int)navigation->selected_index + steps + 8) % 8;
    } else if (navigation->page == PAGE_NEWS_CATEGORY_PICKER) {
        navigation->news_category = (news_category_t)(((int)navigation->news_category + steps + 3) % 3);
    } else if (navigation->page <= PAGE_SETTINGS) {
        navigation->page = channel_pages[((int)navigation->page + steps + 6) % 6];
    }
}

void navigation_short_press(navigation_t *navigation) {
    if (navigation->page == PAGE_NEWS_HOME) {
        navigation->parent_page = PAGE_NEWS_HOME;
        navigation->page = PAGE_NEWS_CATEGORY_PICKER;
    } else if (navigation->page == PAGE_NEWS_CATEGORY_PICKER) {
        navigation->parent_page = PAGE_NEWS_CATEGORY_PICKER;
        navigation->selected_index = 0;
        navigation->page = PAGE_NEWS_LIST;
    } else if (navigation->page == PAGE_NEWS_LIST) {
        navigation->parent_page = PAGE_NEWS_LIST;
        navigation->page = PAGE_NEWS_QR;
    }
}

bool navigation_long_press(navigation_t *navigation) {
    if (navigation->page <= PAGE_SETTINGS) return false;
    navigation->page = navigation->parent_page;
    navigation->parent_page = PAGE_NEWS_HOME;
    return true;
}

const char *navigation_page_name(app_page_t page) {
    static const char *names[] = { "Clock", "Weather", "Crypto", "Markets", "News", "Settings", "News categories", "News list", "Article QR" };
    return page <= PAGE_NEWS_QR ? names[page] : "Unknown";
}
