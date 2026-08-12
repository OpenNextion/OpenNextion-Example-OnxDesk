#include "ui.h"

#include <stdbool.h>
#include "esp_lvgl_port.h"
#include "esp_check.h"
#include "lvgl.h"

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_BUFFER_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)
#define DISPLAY_TRANSFER_PIXELS (DISPLAY_WIDTH * 20)

static lv_display_t *lvgl_display;
static lv_obj_t *page_title;
static lv_obj_t *page_hint;
static bool showing_display_test;

static void create_main_widgets(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080B12), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    page_title = lv_label_create(screen);
    lv_obj_set_style_text_color(page_title, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(page_title, LV_FONT_DEFAULT, 0);
    lv_obj_align(page_title, LV_ALIGN_TOP_MID, 0, 38);

    page_hint = lv_label_create(screen);
    lv_label_set_long_mode(page_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(page_hint, 184);
    lv_obj_set_style_text_align(page_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(page_hint, lv_color_hex(0x9FAAC0), 0);
    lv_obj_set_style_text_font(page_hint, LV_FONT_DEFAULT, 0);
    lv_obj_align(page_hint, LV_ALIGN_CENTER, 0, 15);
}

static void add_color_swatch(lv_obj_t *parent, lv_color_t color, int x, int y) {
    lv_obj_t *swatch = lv_obj_create(parent);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 48, 48);
    lv_obj_set_style_radius(swatch, 8, 0);
    lv_obj_set_style_bg_color(swatch, color, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_pos(swatch, x, y);
}

static void render_display_test(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    const lv_color_t colors[] = {
        lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x0000FF), lv_color_hex(0xFFFFFF),
    };
    for (int i = 0; i < 4; i++) add_color_swatch(screen, colors[i], 20 + (i % 2) * 152, 20 + (i / 2) * 152);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "R       G\n\nB       W\n\nDISPLAY TEST");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

esp_err_t app_ui_init(const board_display_t *display) {
    if (display == NULL || display->panel == NULL || display->io == NULL) return ESP_ERR_INVALID_ARG;
    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_config), "ui", "initialize LVGL port");

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = display->io,
        .panel_handle = display->panel,
        .buffer_size = DISPLAY_BUFFER_PIXELS,
        .trans_size = DISPLAY_TRANSFER_PIXELS,
        .double_buffer = false,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        /* The physical panel is installed for a cable-down enclosure orientation:
         * rotate the already-correct logical image 90 degrees clockwise. SPI
         * RGB565 pixels also need byte swapping before GC9A01 transmission. */
        .rotation = { .swap_xy = true, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = false, .buff_spiram = true, .swap_bytes = true },
    };
    lvgl_display = lvgl_port_add_disp(&display_config);
    if (lvgl_display == NULL) return ESP_ERR_NO_MEM;

    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    create_main_widgets();
    lvgl_port_unlock();
    return ESP_OK;
}

void app_ui_render(const navigation_t *navigation) {
    if (navigation == NULL || page_title == NULL || page_hint == NULL) return;
    if (!lvgl_port_lock(0)) return;
    if (navigation->page == PAGE_DISPLAY_TEST) {
        if (!showing_display_test) {
            render_display_test();
            showing_display_test = true;
        }
        lvgl_port_unlock();
        return;
    }
    if (showing_display_test) {
        lv_obj_clean(lv_screen_active());
        create_main_widgets();
        showing_display_test = false;
    }
    lv_label_set_text(page_title, navigation_page_name(navigation->page));
    if (navigation->page == PAGE_NEWS_HOME) {
        lv_label_set_text(page_hint, "World\nBusiness\nTechnology\n\nPress to choose a category");
    } else if (navigation->page == PAGE_NEWS_CATEGORY_PICKER) {
        static const char *categories[] = { "World", "Business", "Technology" };
        lv_label_set_text_fmt(page_hint, "Choose category\n\n%s", categories[navigation->news_category]);
    } else if (navigation->page == PAGE_NEWS_LIST) {
        lv_label_set_text_fmt(page_hint, "Story %u of 8\n\nRotate to browse\nPress for article QR", navigation->selected_index + 1);
    } else if (navigation->page == PAGE_NEWS_QR) {
        lv_label_set_text(page_hint, "Article QR\n\nWill open the original source on your phone");
    } else {
        lv_label_set_text(page_hint, "Hardware bring-up\n\nLVGL 9 display ready");
    }
    lvgl_port_unlock();
}
