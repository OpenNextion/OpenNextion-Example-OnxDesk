#include "input.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#define ENCODER_A_GPIO GPIO_NUM_48
#define ENCODER_B_GPIO GPIO_NUM_47
#define KEY_GPIO GPIO_NUM_9
#define BOOT_GPIO GPIO_NUM_0
#define INPUT_QUEUE_LENGTH 16
#define POLL_PERIOD_MS 20
#define KEY_LONG_PRESS_MS 800
#define BOOT_FACTORY_RESET_MS 3000

static const char *TAG = "input";
static QueueHandle_t event_queue;
static volatile uint8_t encoder_state;
static volatile int8_t encoder_transitions;

static const int8_t encoder_transition_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static void IRAM_ATTR encoder_isr(void *argument) {
    (void)argument;
    const int a = gpio_get_level(ENCODER_A_GPIO);
    const int b = gpio_get_level(ENCODER_B_GPIO);
    const uint8_t next_state = (uint8_t)((a << 1) | b);
    encoder_transitions += encoder_transition_table[(encoder_state << 2) | next_state];
    encoder_state = next_state;
    if (encoder_transitions > -4 && encoder_transitions < 4) return;
    input_event_t event = { .type = INPUT_EVENT_ROTATE, .value = encoder_transitions > 0 ? 1 : -1 };
    encoder_transitions = 0;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendFromISR(event_queue, &event, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static void send_event(input_event_type_t type) {
    const input_event_t event = { .type = type };
    xQueueSend(event_queue, &event, 0);
}

static void input_poll_task(void *argument) {
    (void)argument;
    bool key_down = false;
    bool key_long_press_sent = false;
    bool boot_down = false;
    bool boot_reset_sent = false;
    int64_t key_started_us = 0;
    int64_t boot_started_us = 0;

    while (true) {
        const int64_t now = esp_timer_get_time();
        const bool key_pressed = gpio_get_level(KEY_GPIO) == 0;
        const bool boot_pressed = gpio_get_level(BOOT_GPIO) == 0;

        if (key_pressed && !key_down) {
            key_down = true;
            key_long_press_sent = false;
            key_started_us = now;
        } else if (!key_pressed && key_down) {
            key_down = false;
            if (!key_long_press_sent) send_event(INPUT_EVENT_KEY_SHORT_PRESS);
        } else if (key_pressed && !key_long_press_sent && now - key_started_us >= KEY_LONG_PRESS_MS * 1000LL) {
            key_long_press_sent = true;
            send_event(INPUT_EVENT_KEY_LONG_PRESS);
        }

        if (boot_pressed && !boot_down) {
            boot_down = true;
            boot_reset_sent = false;
            boot_started_us = now;
        } else if (!boot_pressed) {
            boot_down = false;
        } else if (!boot_reset_sent && now - boot_started_us >= BOOT_FACTORY_RESET_MS * 1000LL) {
            boot_reset_sent = true;
            send_event(INPUT_EVENT_BOOT_FACTORY_RESET);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

esp_err_t input_init(void) {
    event_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(input_event_t));
    if (event_queue == NULL) return ESP_ERR_NO_MEM;

    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO) | (1ULL << KEY_GPIO) | (1ULL << BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), TAG, "configure inputs");
    encoder_state = (uint8_t)((gpio_get_level(ENCODER_A_GPIO) << 1) | gpio_get_level(ENCODER_B_GPIO));
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(ENCODER_A_GPIO, GPIO_INTR_ANYEDGE), TAG, "configure encoder interrupt");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(ENCODER_B_GPIO, GPIO_INTR_ANYEDGE), TAG, "configure encoder interrupt");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_IRAM), TAG, "install GPIO ISR service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ENCODER_A_GPIO, encoder_isr, NULL), TAG, "add encoder ISR");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ENCODER_B_GPIO, encoder_isr, NULL), TAG, "add encoder ISR");

    BaseType_t created = xTaskCreate(input_poll_task, "input_poll", 3072, NULL, 10, NULL);
    if (created != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "inputs ready: encoder=%d/%d, KEY=%d, BOOT=%d", ENCODER_A_GPIO, ENCODER_B_GPIO, KEY_GPIO, BOOT_GPIO);
    return ESP_OK;
}

QueueHandle_t input_event_queue(void) {
    return event_queue;
}
