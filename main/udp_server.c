/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "bridge.h"
#include "relay.h"


struct Period {
  int start_h;
  int start_m;
  int end_h;
  int end_m;
};

bool create_period(struct Period* period, char * array) {
  if (period == NULL || array == NULL) {
    return false;
  }

  period->start_h = array[0];
  period->start_m = array[1];
  period->end_h = array[2];
  period->end_m = array[3];

  if (period->start_h < 0 || period->start_h > 23 || period->end_h < 0 || period->end_h > 23 ||
      period->start_m < 0 || period->start_m > 59 || period->end_m < 0 || period->end_m > 59) {
    return false;
  }

  return true;
}



QueueHandle_t period_queue = NULL;


#define LED_PIN 2
#define TRANSISTOR_PIN 21
#define TEST_PIN 19

static void led_light(bool val) {
  gpio_set_level(LED_PIN, val);
}
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;


static const char *TAG = "Automatic switch";

void print_period(const struct Period *period) {
  if (period == NULL) {
    ESP_LOGE(TAG, "Error: NULL period pointer");
    return;
  }

  if (period->start_h < 0 || period->start_h > 23 || period->start_m < 0 || period->start_m > 59 ||
      period->end_h < 0 || period->end_h > 23 || period->end_m < 0 || period->end_m > 59) {
    ESP_LOGE(TAG, "Error: invalid start or end time");
    return;
  }

  ESP_LOGI(TAG, "Period -> %dh%02d - %dh%02d", period->start_h, period->start_m, period->end_h, period->end_m);
}


/* UDP server stuff
 */

#define PORT CONFIG_EXAMPLE_PORT


esp_err_t save_string_nvs(const char* key, const char* val) {
    nvs_handle_t nvsh;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsh);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error %s opening NVS", esp_err_to_name(err));
    } else {
        err = nvs_set_str(nvsh, key, val);
        ESP_LOGI(TAG, "NVS set %s: %s",key, esp_err_to_name(err));
        err = nvs_commit(nvsh);
        ESP_LOGI(TAG, "NVS set %s commit: %s", key, esp_err_to_name(err));
        nvs_close(nvsh);
    }
    return ESP_OK;
}


enum MSG_FLAG {
    PERIOD_FLAG,
    ADRESS_FLAG,
    SSID_FLAG
};

bool is_valid_url(const char* str) {
    // Check for NULL input
    if (str == NULL) {
        return false;
    }

    // Minimum length check (needs at least 7 characters for "http://")
    size_t len = strlen(str);
    if (len <= 8) {
        return false;
    }

    // Convert first characters to lowercase for case-insensitive comparison
    char first_letters[9] = {0};
    for (int i = 0; i < 8; i++) {
        first_letters[i] = tolower(str[i]);
    }

    // Check if starts with "http"
    if (strncmp(first_letters, "http://", 7) == 0 || strncmp(first_letters, "https://", 8) == 0) {
        return true;
    }

    return false;
}


static void udp_server_task(void *pvParameters)
{
    char rx_buffer[200];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        struct iovec iov;
        struct msghdr msg;
        struct cmsghdr *cmsgtmp;
        u8_t cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];

        iov.iov_base = rx_buffer;
        iov.iov_len = sizeof(rx_buffer);
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = (struct sockaddr *)&source_addr;
        msg.msg_namelen = socklen;
#endif

        while (1) {
            ESP_LOGI(TAG, "Waiting for data");
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
            int len = recvmsg(sock, &msg, 0);
#else
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
#endif
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
                    for ( cmsgtmp = CMSG_FIRSTHDR(&msg); cmsgtmp != NULL; cmsgtmp = CMSG_NXTHDR(&msg, cmsgtmp) ) {
                        if ( cmsgtmp->cmsg_level == IPPROTO_IP && cmsgtmp->cmsg_type == IP_PKTINFO ) {
                            struct in_pktinfo *pktinfo;
                            pktinfo = (struct in_pktinfo*)CMSG_DATA(cmsgtmp);
                            ESP_LOGI(TAG, "dest ip: %s\n", inet_ntoa(pktinfo->ipi_addr));
                        }
                    }
#endif
                } else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
                int err;
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                /* Types de messages possibles :
                 * - 4 bytes = changement d'horaire pour l'interrupteur
                 * - x bytes = nouvelle adresse d'envoi des informations
                 * - y bytes = nouveau ssid/password
                 *  
                 */
                bool restart_udp_server = false;
                switch (rx_buffer[0]) {
                    case PERIOD_FLAG:
                        if (len != 5) {
                            ESP_LOGE(TAG, "Incomplete message for new period. %d bytes received",len);
                            err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                            restart_udp_server = true;
                            break;
                        }

                        ESP_LOGI(TAG, "Msg decoded: %d:%d %d:%d",
                                rx_buffer[1],
                                rx_buffer[2],
                                rx_buffer[3],
                                rx_buffer[4]);
                        struct Period p;
                        if (create_period(&p, &rx_buffer[1])) {
                            xQueueSend(period_queue, &p, 0);
                            ESP_LOGI(TAG, "Sending answer: %s", rx_buffer);
                            err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                        }
                        else {
                            err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                        }

                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                            restart_udp_server = true;
                            break;
                        }
                        break;
                    case ADRESS_FLAG:
                        if (len > 200) {
                            ESP_LOGE(TAG, "Adress received too long: %d", len);
                            err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                            restart_udp_server = true;
                            break;
                        } else {
                            char addr[len];
                            strncpy(addr, &rx_buffer[1], len -1);
                            addr[len-1] = '\0';
                            if (!is_valid_url(addr)) {
                                ESP_LOGE(TAG, "'%s' is not a valid url.", addr);
                                err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                                restart_udp_server = true;
                                break;
                            } else {
                                save_string_nvs("adress", addr);
                                ESP_LOGI(TAG, "New adress set: %s", addr);
                                err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                                if (err < 0) {
                                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                                    restart_udp_server = true;
                                    break;
                                }
                            }
                        }
                        break;
                    case SSID_FLAG:
                        if (len != 32 + 64 + 1) { // SSID + Password + flag 
                            ESP_LOGE(TAG, "Incomplete message for new SSID and Password. %d bytes received", len);
                            err = sendto(sock, "invalid", 7, 0, (struct sockaddr *) & source_addr, sizeof(source_addr));
                            restart_udp_server = true;
                            break;
                        }
                        char ssid[33], pass[65];
                        strncpy(ssid, &rx_buffer[1], 32);
                        strncpy(pass, &rx_buffer[33], 64);
                        save_string_nvs("ssid", ssid);
                        save_string_nvs("pass", pass);
                        ESP_LOGI(TAG, "New ssid and password saved: %s - %s", ssid, pass);
                        err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                            restart_udp_server = true;
                            break;
                        }
                        // TODO il faut redémarrer le wifi aussitôt !
                        const int sec = 10;
                        ESP_LOGI(TAG, "Wifi reset. Restarting ESP-32 in %d seconds", sec);
                        vTaskDelay((sec * 1000)/ portTICK_PERIOD_MS);
                        esp_restart();

                        break;
                    default:
                        ESP_LOGE(TAG, "Unknown flag sent: %d",rx_buffer[0]);
                }
                if (restart_udp_server) {
                    break;
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}


void time_test() {
       time_t rawtime;
   struct tm *info;
   char buffer[80];

   time( &rawtime );

   info = localtime( &rawtime );

   strftime(buffer,80,"%x - %I:%M%p", info);
   printf("Formatted date & time : |%s|\n", buffer );
}



static void relay_toggle() {
    static bool is_open = false;
    gpio_set_level(TRANSISTOR_PIN,is_open);
    is_open = ! is_open;
    //ESP_LOGI(TAG,"Open? %d",gpio_get_level(TEST_PIN));
}

static bool is_relay_on() {
    // true if relay is on
    //return !gpio_get_level(TEST_PIN);
    // relay is on if pin is off
    return !gpio_get_level(TRANSISTOR_PIN);
}

static void pin_init() {
// led
  esp_rom_gpio_pad_select_gpio(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

// relay 
  esp_rom_gpio_pad_select_gpio(TRANSISTOR_PIN);
  gpio_set_direction(TRANSISTOR_PIN, GPIO_MODE_INPUT_OUTPUT);
  // first call: transistor is now off, relay is on
  gpio_set_level(TRANSISTOR_PIN, false);
  // test3d
  //esp_rom_gpio_pad_select_gpio(TEST_PIN);
  //gpio_set_direction(TEST_PIN,GPIO_MODE_INPUT);
}

struct tm* fill_time() {
    time_t rawtime;
    time(&rawtime);
    return localtime( &rawtime);
}

bool is_time_in(struct tm* current_time, struct Period* period) {
    // Convert the start and end times to minutes since midnight
    int start_time_minutes = period->start_h* 60 + period->start_m;
    int end_time_minutes = period->end_h * 60 + period->end_m;

    // Convert the current time to minutes since midnight
    int current_time_minutes = current_time->tm_hour * 60 + current_time->tm_min;

    // Check if the current time is within the given period
    if (start_time_minutes <= end_time_minutes) {
        // Start time is before or equal to end time
        return start_time_minutes <= current_time_minutes && current_time_minutes < end_time_minutes;
    } else {
        // Start time is after end time
        return start_time_minutes <= current_time_minutes || current_time_minutes < end_time_minutes;
    }
}



void light_manager(void *pvParameter) {
    struct Period p = { 7, 0, 22, 0 }; // default values
    // trying to load from NVS
    nvs_handle_t nvsh;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvsh);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error loading Period from NVS");
    } else {
        //char arr[10];
        size_t size = sizeof(p);
        err = nvs_get_blob(nvsh, "period", &p, &size);
        print_period(&p);
        switch (err) {
            case ESP_OK:
                //create_period(&p, arr); // probably useless when saved as a blob
                ESP_LOGI(TAG, "Period loaded");
                print_period(&p);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG,"Period not loaded from NVS: not initialized yet. Falling back to default");
                break;
            default:
                ESP_LOGE(TAG, "Error (%s) when loading Period from NVS. Falling back to default",esp_err_to_name(err));
        }
    }
    nvs_close(nvsh);
    ESP_LOGI(TAG, "Period set");
    print_period(&p);

    while (1) {
        if (uxQueueMessagesWaiting(period_queue)) {
            xQueueReceive(period_queue, &p, 0);
            ESP_LOGI(TAG, "New period set.");
            print_period(&p);
            // save in NVS 
            nvs_handle_t nvsh;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsh);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error %s opening NVS", esp_err_to_name(err));
            } else {
                /*
                char arr[10] = { p.start_h, p.start_m, p.end_h, p.end_m , '\0'};
                * TODO erreur ici: nvs enregistre chaque byte, mais si ce byte est égal à 0, il le considère comme la fin de la chaîne e
                 * ne va pas plus loin. Il faut faire en sorte que tous les bytes soient enregistrés.
                 * utiliser un set_blob à la place (et get blob de l'autre côté)
                 */
                err = nvs_set_blob(nvsh, "period",&p, sizeof(p));
                ESP_LOGI(TAG, "NVS set period: %s",esp_err_to_name(err));
                err = nvs_commit(nvsh);
                ESP_LOGI(TAG, "NVS set period commit: %s",esp_err_to_name(err));
                nvs_close(nvsh);
                print_period(&p);
            }
        }
        struct tm* time = fill_time();
        if (time->tm_year == 70) {
            ESP_LOGE(TAG, "Time not yet updated");
        } else {
            if (is_time_in(time, &p)) {
                switch_NC_relay(true);
            } else {
                switch_NC_relay(false);
            }
        }
        sleep(1);
    }
}

void init_udp_and_lamp(void)
{
    // local time
    setenv("TZ","UTC-1",1);
    tzset();

    // update time
    ESP_LOGI(TAG, "Set SNTP update");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    // pin init
    pin_init();

    // queue to transmit messages
    period_queue = xQueueCreate(5, sizeof(struct Period));

    // udp server
#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif

    xTaskCreate(light_manager, "light_manager", 4096, NULL, 5, NULL);
}
