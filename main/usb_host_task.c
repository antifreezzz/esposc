#include "usb_host_task.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static const char *TAG = "usb";

static usb_host_client_handle_t s_client;

static void log_device(uint8_t addr)
{
    usb_device_handle_t dev;
    if (usb_host_device_open(s_client, addr, &dev) != ESP_OK) {
        ESP_LOGW(TAG, "addr %d: open failed", addr);
        return;
    }
    const usb_device_desc_t *desc;
    if (usb_host_get_device_descriptor(dev, &desc) == ESP_OK) {
        ESP_LOGI(TAG, "new device addr %d: %04x:%04x class 0x%02x", addr,
                 desc->idVendor, desc->idProduct, desc->bDeviceClass);
    }
    usb_host_device_close(s_client, dev);
}

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        log_device(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "device gone");
    }
}

static void client_task(void *arg)
{
    (void)arg;
    const usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {.client_event_callback = client_cb, .callback_arg = NULL},
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &s_client));
    for (;;) {
        usb_host_client_handle_events(s_client, portMAX_DELAY);
    }
}

static void host_lib_task(void *arg)
{
    TaskHandle_t parent = arg;
    const usb_host_config_t cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_ERROR_CHECK(usb_host_install(&cfg));
    xTaskNotifyGive(parent);
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

void usb_host_start(void)
{
    xTaskCreatePinnedToCore(host_lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xTaskCreatePinnedToCore(client_task, "usb_log", 4096, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "usb host started, waiting for devices");
}
