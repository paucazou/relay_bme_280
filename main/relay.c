/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "relay.h"

#define TAG "Relay"
bool IS_RELAY_ON = true;

bool is_NC_relay_on() {
    // true if Normally Closed relay is closed,
    // ie, current is on
    return IS_RELAY_ON;
}


esp_err_t switch_off_NC_relay() {
    esp_err_t err = gpio_set_direction(PIN, GPIO_MODE_INPUT_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(PIN, 0);
    if (err != ESP_OK) {
        return err;
    }
    IS_RELAY_ON = false;
    return ESP_OK;
}

esp_err_t switch_on_NC_relay() {
    esp_err_t err = gpio_set_direction(PIN, GPIO_MODE_DISABLE);
    if (err != ESP_OK) {
        return err;
    }
    IS_RELAY_ON = true;
    return ESP_OK;
}

esp_err_t switch_NC_relay(bool v) {
    if (is_NC_relay_on() + v != 1) {
        // in this case, the relay is off and we want to off it,
        // or the relay is on and we want to on it
        // in every case, we must do nothing
#ifdef DEBUG
        ESP_LOGI(TAG,"Same state (%d). Do nothing.\n", is_NC_relay_on() + v);
#endif
        return ESP_OK;
    }
    else if (v) {
        // we must switch on
        return switch_on_NC_relay();
    } else {
        // switch off
        // We should use gpio_set_level but actually,
        // the esp_32 doesnt' work properly and there is enough current
        // to switch the relay
        return switch_off_NC_relay();
    }

}
#ifdef DEBUG
#define ON ESP_LOGI(TAG," is_NC_relay_on? %d\n", is_NC_relay_on())
#define DELAY vTaskDelay ( 3000 / portTICK_PERIOD_MS)
void app_main(void)
{

    while (1) {
        ESP_LOGI(TAG,"Relay opened (default)\n");
        ON;

        DELAY;
        ESP_LOGI(TAG,"\nClose NC relay.\n");
        switch_NC_relay(false);
        ON;

        DELAY;
        ESP_LOGI(TAG,"\nClose again NC relay. Supposed to do nothing\n");
        switch_NC_relay(false);
        ON;

        DELAY;
        switch_NC_relay(true);
        ESP_LOGI(TAG,"\nOpen NC relay\n");
        ON;

        DELAY;
        ESP_LOGI(TAG,"\nOpen again NC relay. Supposed to do nothing\n");
        switch_NC_relay(true);
        ON;

        DELAY;
        ESP_LOGI(TAG,"\nRestarting...\n");

    }
}
#endif
