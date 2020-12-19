#ifndef BRIDGE_H
#define BRIDGE_H
void init(void);

static const char *TAG = "BME280_WIFI";
typedef struct _bme280_res {
    float temp ;
    float press;
    float hum;
} _bme280_res;
esp_err_t send_data(const _bme280_res * results);
#endif
