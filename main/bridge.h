#ifndef BRIDGE_H
#define BRIDGE_H
esp_err_t send_data(_bme280_res * results);
void init(void);
const TickType_t DELAY = 600000 / portTICK_PERIOD_MS;

static const char *TAG = "BME280_WIFI";
typedef struct _bme280_res {
    float press = 0.0;
    float temp = 0.0;
    float hum = 0.0;
} _bme280_res;
#endif
