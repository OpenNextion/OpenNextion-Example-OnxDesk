idf_component_register(
    SRCS
        "app_main.c"
        "board/board.c"
        "board/input.c"
        "storage/settings.c"
        "network/network.c"
        "ui/navigation.c"
        "ui/ui.c"
        "services/providers.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver esp_lcd esp_timer esp_psram nvs_flash spi_flash esp_lcd_gc9a01 esp_lvgl_port lvgl
        esp_event esp_netif esp_wifi esp_http_server
)
