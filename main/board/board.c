#include "board.h"

#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_gc9a01.h"
#include "esp_log.h"

#define LCD_HOST SPI2_HOST
#define LCD_H_RES 240
#define LCD_V_RES 240
#define LCD_SCLK_GPIO 5
#define LCD_MOSI_GPIO 1
#define LCD_CS_GPIO 2
#define LCD_DC_GPIO 3
#define LCD_BL_GPIO 6
#define LCD_RST_GPIO 8

static const char *TAG = "board";

size_t board_psram_bytes(void) {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

esp_err_t board_init(board_display_t *display) {
    if (display == NULL) return ESP_ERR_INVALID_ARG;

    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO) | (1ULL << LCD_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "configure LCD GPIOs");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BL_GPIO, 1), TAG, "enable backlight");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_RST_GPIO, 0), TAG, "assert LCD reset");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_RST_GPIO, 1), TAG, "release LCD reset");
    vTaskDelay(pdMS_TO_TICKS(200));

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_SCLK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "initialize LCD SPI bus");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &display->io), TAG, "create LCD panel I/O");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(display->io, &panel_config, &display->panel), TAG, "create GC9A01 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), TAG, "reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG, "initialize panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display->panel, false), TAG, "disable inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG, "enable panel");

    ESP_LOGI(TAG, "GC9A01 ready: %dx%d, PSRAM: %u bytes", LCD_H_RES, LCD_V_RES, (unsigned)board_psram_bytes());
    return ESP_OK;
}
