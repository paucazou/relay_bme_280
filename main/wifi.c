/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"


#include "esp_http_client.h"

#include "rom/ets_sys.h"
#include <stdio.h>
#include "sdkconfig.h"
#include "bridge.h"

#define LED_PIN 2
#define TAG "BMX"

// LED

static void led_blink() {
    gpio_set_level(LED_PIN,1);
    vTaskDelay(10/portTICK_PERIOD_MS);
    gpio_set_level(LED_PIN,0);
}

static void led_light(bool val) {
    gpio_set_level(LED_PIN, val);
}

static void led_pin_init() {
    //gpio_pad_select_gpio(LED_PIN); // deprecated?
    esp_rom_gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN,GPIO_MODE_OUTPUT);
}

// NVS

esp_err_t read_write_nvs_value_str(const char* key, char* fallback, size_t l) {
    esp_err_t err;
    nvs_handle_t handle;
    err = nvs_open("storage",NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error (%s) opening NVS handle. Falling back to default...",esp_err_to_name(err));
        return err;
    } 

    err = nvs_get_str(handle, key, fallback, &l);
    switch (err) {
        case ESP_OK:
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            // saving fallback
            nvs_set_str(handle, key, fallback);
            err = nvs_commit(handle);
            ESP_LOGI(TAG, "Saving key %s",key);
            break;
        default:
            ESP_LOGE(TAG, "Error (%s) reading key %s",esp_err_to_name(err), key);
    }
    nvs_close(handle);
    return err;
}


/* HTTP
 */
esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1



static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;
    static bool default_ssid = false;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        led_light(true);
        s_retry_num = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        led_light(false);
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else if (!default_ssid) {
            ESP_LOGE(TAG, "AP abandoned. Falling back to default SSID...");
            s_retry_num = 0;
            default_ssid = true;
            esp_wifi_disconnect();
            esp_wifi_stop();
            ESP_LOGI(TAG, "Wifi stopped");

            wifi_config_t wifi_config = {
                    .sta = {
                        .ssid = EXAMPLE_ESP_WIFI_SSID,
                        .password = EXAMPLE_ESP_WIFI_PASS,
                        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                        .pmf_cfg = {
                            .capable = true,
                            .required = false
                        },
                    },
                };
            ESP_LOGI(TAG, "Trying default AP");
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
            ESP_ERROR_CHECK(esp_wifi_start() );


        } else {
            ESP_LOGI(TAG,"Default AP not found.");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        led_light(true);
        s_retry_num = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) 
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_got_ip));

    // load from nvs storage
    char ssid[33] = EXAMPLE_ESP_WIFI_SSID;
    char pass[65] = EXAMPLE_ESP_WIFI_PASS;
    read_write_nvs_value_str("ssid",ssid, sizeof(ssid));
    read_write_nvs_value_str("pass",pass, sizeof(pass));

    wifi_config_t wifi_config = {
        .sta = {
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
             .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strncpy((char*)wifi_config.sta.ssid, (char*)&ssid[0], 32);
    strncpy((char*)wifi_config.sta.password, (char*)&pass[0], 64);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s",
                 ssid);
        led_light(true);
    } else {
        led_light(false);

        if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID"
                 );
            const int sec = 30;
            ESP_LOGI(TAG, "Impossible to connect. Restarting in %d seconds", sec);
            vTaskDelay( (sec * 1000) / portTICK_PERIOD_MS);
            esp_restart();
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    }

}


void init(void)
{
    /* TODO
     * à sauvegarder : start_hour / end_hour - adresse où envoyer les données
     * mise à jour date et heure au réveil. Est-il nécessaire de remettre à jour de temps à autres?
     * un moyen de mettre à jour la date et l'heure et l'adresse
     */
    led_pin_init();

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    ESP_LOGI(TAG, "ESP wifi set up");
}

esp_err_t send_data(const _bme280_res* results) {
    char url[200] = "http://palantir/thermo/update-sensor.php"; // default
    if (read_write_nvs_value_str("adress", url, sizeof(url)) != ESP_OK) {
        ESP_LOGE(TAG, "URl default: %s",url);
    }
    ESP_LOGI(TAG, "URl: %s",url);

    // HTTP
    esp_http_client_config_t config = {
       //.url = "http://palantir:8765/update-sensor",
       //.url = "https://househomestuff.000webhostapp.com/update-sensor.php",
       .event_handler = _http_event_handle,
       .port = 80,
    };
    config.url = url;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //esp_http_client_set_url(client, url);
    const char* data_base = "temp=%f&hum=%f&press=%f&source=%d\n"; 
    char data[200];
    sprintf(data,data_base,results->temp,results->hum,results->press,CONFIG_BME_ID);
    printf(data);
    esp_http_client_set_post_field (client,data,strlen(data));
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
       ESP_LOGI(TAG, "Status = %d, content_length = %" PRId64,
               esp_http_client_get_status_code(client),
               esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
    return err;
}
