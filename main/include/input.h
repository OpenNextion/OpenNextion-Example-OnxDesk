#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    INPUT_EVENT_ROTATE,
    INPUT_EVENT_KEY_SHORT_PRESS,
    INPUT_EVENT_KEY_LONG_PRESS,
    INPUT_EVENT_BOOT_FACTORY_RESET,
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    int value;
} input_event_t;

esp_err_t input_init(void);
QueueHandle_t input_event_queue(void);
