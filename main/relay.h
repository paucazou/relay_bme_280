#ifndef RELAY_H
#define RELAY_H
#include "esp_log.h"

#define PIN GPIO_NUM_19

bool is_NC_relay_on();
esp_err_t switch_NC_relay(bool);


#endif
