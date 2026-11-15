#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"

static const char *TAG = "USB_HUB_TEST";
static usb_host_client_handle_t client_handle;
static usb_device_handle_t open_devices[8];
static uint8_t open_device_addresses[8];
static usb_device_handle_t midi_device;
static usb_transfer_t *midi_tx;
static uint8_t midi_interface;
static uint8_t midi_endpoint_out;
static bool midi_connected;
static bool midi_ever_connected;
static TickType_t midi_disconnected_at;
static bool midi_tx_busy;
static usb_transfer_status_t midi_tx_status = USB_TRANSFER_STATUS_COMPLETED;
static portMUX_TYPE midi_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t midi_queue;
static SemaphoreHandle_t midi_tx_done;

static uint8_t preset_bank;
static bool drive_enabled;
static bool delay_enabled;
static bool reverb_enabled;
static bool tuner_enabled;

typedef enum {
    PEDAL_NONE,
    PEDAL_POCKET_MASTER,
    PEDAL_GP5,
} pedal_type_t;

static pedal_type_t pedal_type;

typedef struct {
    size_t length;
    uint8_t bytes[32];
} queued_midi_message_t;

extern size_t gp5_build_preset_change(uint8_t preset_index, uint8_t *output,
                                      size_t capacity);

enum {
    SWITCH_BANK_DOWN,
    SWITCH_BANK_UP,
    SWITCH_DRIVE,
    SWITCH_DELAY,
    SWITCH_REVERB,
    SWITCH_A,
    SWITCH_B,
    SWITCH_C,
    SWITCH_D,
    SWITCH_UTILITY,
    SWITCH_COUNT,
};

static const gpio_num_t switch_gpios[SWITCH_COUNT] = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8,
    GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13,
};

static const char *switch_names[SWITCH_COUNT] = {
    "BANK-", "BANK+", "DRIVE", "DELAY", "REVERB",
    "A", "B", "C", "D", "UTILITY",
};

static void midi_tx_callback(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "USB-MIDI transmit failed, status=%u", transfer->status);
    }
    midi_tx_status = transfer->status;
    portENTER_CRITICAL(&midi_lock);
    midi_tx_busy = false;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        midi_connected = true;
        midi_disconnected_at = 0;
    } else {
        midi_connected = false;
        if (midi_disconnected_at == 0) {
            midi_disconnected_at = xTaskGetTickCount();
        }
    }
    portEXIT_CRITICAL(&midi_lock);
    if (midi_tx_done != NULL) {
        xSemaphoreGive(midi_tx_done);
    }
}

static bool submit_midi_message(const uint8_t *midi, size_t length)
{
    if (midi == NULL || length == 0) {
        return false;
    }
    portENTER_CRITICAL(&midi_lock);
    if (!midi_connected || midi_device == NULL || midi_tx == NULL || midi_tx_busy) {
        portEXIT_CRITICAL(&midi_lock);
        ESP_LOGW(TAG, "MIDI command not submitted: device disconnected or transmitter busy");
        return false;
    }
    midi_tx_busy = true;
    midi_tx->device_handle = midi_device;
    midi_tx->bEndpointAddress = midi_endpoint_out;
    size_t usb_length = 0;
    if ((midi[0] & 0xF0) == 0xB0 && length == 3) {
        midi_tx->data_buffer[usb_length++] = 0x0B;
        memcpy(midi_tx->data_buffer + usb_length, midi, 3);
        usb_length += 3;
    } else if (midi[0] == 0xF0) {
        size_t offset = 0;
        while (offset < length) {
            size_t remaining = length - offset;
            size_t count = remaining > 3 ? 3 : remaining;
            uint8_t cin = 0x04;
            if (remaining <= 3 && midi[offset + count - 1] == 0xF7) {
                cin = count == 1 ? 0x05 : count == 2 ? 0x06 : 0x07;
            }
            if (usb_length + 4 > midi_tx->data_buffer_size) {
                midi_tx_busy = false;
                portEXIT_CRITICAL(&midi_lock);
                ESP_LOGE(TAG, "MIDI message exceeds USB transfer buffer");
                return false;
            }
            midi_tx->data_buffer[usb_length++] = cin;
            for (size_t byte = 0; byte < 3; ++byte) {
                midi_tx->data_buffer[usb_length++] =
                    byte < count ? midi[offset + byte] : 0;
            }
            offset += count;
        }
    } else {
        midi_tx_busy = false;
        portEXIT_CRITICAL(&midi_lock);
        return false;
    }
    midi_tx->num_bytes = usb_length;
    portEXIT_CRITICAL(&midi_lock);

    esp_err_t error = usb_host_transfer_submit(midi_tx);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&midi_lock);
        midi_tx_busy = false;
        portEXIT_CRITICAL(&midi_lock);
        ESP_LOGE(TAG, "USB-MIDI submit failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

// Switch events are queued so a quick press sequence cannot be lost while the
// previous USB bulk transfer is still completing.
static bool send_midi_message(const uint8_t *midi, size_t length)
{
    if (midi == NULL || length == 0 ||
        length > sizeof(((queued_midi_message_t *)0)->bytes) ||
        midi_queue == NULL) {
        return false;
    }
    queued_midi_message_t message = {.length = length};
    memcpy(message.bytes, midi, length);
    if (xQueueSend(midi_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MIDI command queue full; command dropped");
        return false;
    }
    return true;
}

static void midi_task(void *arg)
{
    (void)arg;
    queued_midi_message_t message;
    while (true) {
        if (xQueueReceive(midi_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // Give the GP-5 time to process each SysEx command. Retry transient
        // busy/disconnected windows while retaining the queued action.
        for (unsigned attempt = 0; attempt < 20; ++attempt) {
            while (xSemaphoreTake(midi_tx_done, 0) == pdTRUE) {}
            if (submit_midi_message(message.bytes, message.length)) {
                if (xSemaphoreTake(midi_tx_done, pdMS_TO_TICKS(750)) == pdTRUE) {
                    if (midi_tx_status == USB_TRANSFER_STATUS_COMPLETED) {
                        // The GP-5 can stop answering when preset SysEx
                        // messages arrive too close together.
                        vTaskDelay(pdMS_TO_TICKS(400));
                        break;
                    }
                    ESP_LOGW(TAG, "USB transfer completed with status=%u; retrying command",
                             midi_tx_status);
                } else {
                    // Do not reuse a USB transfer buffer until its callback
                    // confirms completion. A cable replug generates the
                    // disconnect event and lets the host enumerate afresh.
                    ESP_LOGE(TAG, "USB-MIDI transmit stalled; reconnect the pedal USB cable");
                    portENTER_CRITICAL(&midi_lock);
                    midi_connected = false;
                    if (midi_disconnected_at == 0) {
                        midi_disconnected_at = xTaskGetTickCount();
                    }
                    portEXIT_CRITICAL(&midi_lock);
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            if (attempt == 19) {
                ESP_LOGE(TAG, "USB-MIDI command abandoned after 20 retries");
            }
        }
    }
}

static bool send_control_change(uint8_t control, uint8_t value)
{
    const uint8_t message[] = {0xB0, control, value};
    bool sent = send_midi_message(message, sizeof(message));
    if (sent) {
        ESP_LOGI(TAG, "MIDI CC%u=%u queued", control, value);
    }
    return sent;
}

static void handle_switch_press(unsigned index)
{
    if (index == SWITCH_BANK_DOWN) {
        if (preset_bank > 0) {
            --preset_bank;
        }
        ESP_LOGI(TAG, "Bank %u (presets %u-%u)", preset_bank + 1,
                 preset_bank * 4 + 1, preset_bank * 4 + 4);
        return;
    }
    if (index == SWITCH_BANK_UP) {
        if (preset_bank < 24) {
            ++preset_bank;
        }
        ESP_LOGI(TAG, "Bank %u (presets %u-%u)", preset_bank + 1,
                 preset_bank * 4 + 1, preset_bank * 4 + 4);
        return;
    }
    if (index >= SWITCH_A && index <= SWITCH_D) {
        uint8_t preset = preset_bank * 4 + (index - SWITCH_A) + 1;
        if (pedal_type == PEDAL_GP5) {
            uint8_t message[24];
            size_t length = gp5_build_preset_change(preset - 1, message,
                                                     sizeof(message));
            ESP_LOGI(TAG, "Select Valeton GP-5 preset P%02u", preset);
            if (send_midi_message(message, length)) {
                ESP_LOGI(TAG, "GP-5 preset SysEx queued");
            }
        } else {
            ESP_LOGI(TAG, "Select Pocket Master preset %s%02u",
                     preset <= 50 ? "P" : "F", preset <= 50 ? preset : preset - 50);
            send_control_change(1, preset);
        }
        return;
    }

    uint8_t control = 0;
    bool *state = NULL;
    if (index == SWITCH_DRIVE) {
        control = pedal_type == PEDAL_GP5 ? 50 : 45;
        state = &drive_enabled;
    } else if (index == SWITCH_DELAY) {
        control = pedal_type == PEDAL_GP5 ? 56 : 50;
        state = &delay_enabled;
    } else if (index == SWITCH_REVERB) {
        control = pedal_type == PEDAL_GP5 ? 57 : 51;
        state = &reverb_enabled;
    } else if (index == SWITCH_UTILITY) {
        control = 58;
        state = &tuner_enabled;
    }
    if (state != NULL) {
        bool target = !*state;
        bool sent = send_control_change(control, target ? 127 : 0);
        if (sent) {
            *state = target;
        }
    }
}

static void switch_task(void *arg)
{
    (void)arg;
    const gpio_config_t config = {
        .pin_bit_mask = ((1ULL << 14) - (1ULL << 4)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    bool stable[SWITCH_COUNT];
    bool candidate[SWITCH_COUNT];
    TickType_t candidate_since[SWITCH_COUNT];
    TickType_t now = xTaskGetTickCount();
    for (unsigned index = 0; index < SWITCH_COUNT; ++index) {
        stable[index] = gpio_get_level(switch_gpios[index]) == 0;
        candidate[index] = stable[index];
        candidate_since[index] = now;
    }
    ESP_LOGI(TAG, "Footswitch scanner active on GPIO4-GPIO13");

    const TickType_t debounce_ticks = pdMS_TO_TICKS(25);
    while (true) {
        now = xTaskGetTickCount();
        for (unsigned index = 0; index < SWITCH_COUNT; ++index) {
            bool pressed = gpio_get_level(switch_gpios[index]) == 0;
            if (pressed != candidate[index]) {
                candidate[index] = pressed;
                candidate_since[index] = now;
            } else if (candidate[index] != stable[index] &&
                       now - candidate_since[index] >= debounce_ticks) {
                stable[index] = candidate[index];
                ESP_LOGI(TAG, "Switch %s %s", switch_names[index],
                         stable[index] ? "PRESSED" : "RELEASED");
                if (stable[index]) {
                    handle_switch_press(index);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool discover_midi_interface(usb_device_handle_t device)
{
    const usb_config_desc_t *configuration = NULL;
    if (usb_host_get_active_config_descriptor(device, &configuration) != ESP_OK ||
        configuration == NULL) {
        return false;
    }
    for (uint8_t interface_number = 0; interface_number < 16; ++interface_number) {
        int interface_offset = 0;
        const usb_intf_desc_t *interface = usb_parse_interface_descriptor(
            configuration, interface_number, 0, &interface_offset);
        if (interface == NULL || interface->bInterfaceClass != USB_CLASS_AUDIO ||
            interface->bInterfaceSubClass != 3) {
            continue;
        }
        uint8_t endpoint_out = 0;
        for (uint8_t endpoint_index = 0;
             endpoint_index < interface->bNumEndpoints; ++endpoint_index) {
            int endpoint_offset = interface_offset;
            const usb_ep_desc_t *endpoint = usb_parse_endpoint_descriptor_by_index(
                interface, endpoint_index, configuration->wTotalLength,
                &endpoint_offset);
            if (endpoint != NULL && (endpoint->bmAttributes & 0x03) == 0x02 &&
                (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0) {
                endpoint_out = endpoint->bEndpointAddress;
            }
        }
        if (endpoint_out != 0) {
            midi_interface = interface->bInterfaceNumber;
            midi_endpoint_out = endpoint_out;
            ESP_LOGI(TAG, "USB-MIDI interface=%u OUT endpoint=0x%02X",
                     midi_interface, midi_endpoint_out);
            return true;
        }
    }
    return false;
}

static bool open_midi_device(usb_device_handle_t device,
                             const usb_device_desc_t *descriptor)
{
    if (descriptor->idVendor != 0x84EF ||
        (descriptor->idProduct != 0x0284 && descriptor->idProduct != 0x0184)) {
        return false;
    }
    if (!discover_midi_interface(device)) {
        ESP_LOGE(TAG, "Pocket Master has no usable USB-MIDI interface");
        return false;
    }
    esp_err_t error = usb_host_interface_claim(client_handle, device,
                                                midi_interface, 0);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "USB-MIDI interface claim failed: %s", esp_err_to_name(error));
        return false;
    }
    if (midi_tx == NULL) {
        error = usb_host_transfer_alloc(64, 0, &midi_tx);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "USB-MIDI transfer allocation failed: %s", esp_err_to_name(error));
            usb_host_interface_release(client_handle, device, midi_interface);
            return false;
        }
        midi_tx->callback = midi_tx_callback;
    }
    portENTER_CRITICAL(&midi_lock);
    midi_device = device;
    pedal_type = descriptor->idProduct == 0x0184 ? PEDAL_GP5 :
                                                   PEDAL_POCKET_MASTER;
    midi_connected = true;
    midi_ever_connected = true;
    midi_disconnected_at = 0;
    midi_tx_busy = false;
    portEXIT_CRITICAL(&midi_lock);
    ESP_LOGI(TAG, "USB-MIDI READY: %s footswitch control is active",
             pedal_type == PEDAL_GP5 ? "Valeton GP-5" : "Pocket Master");
    return true;
}

static bool device_address_is_open(uint8_t address)
{
    for (size_t i = 0; i < sizeof(open_devices) / sizeof(open_devices[0]); ++i) {
        if (open_devices[i] != NULL && open_device_addresses[i] == address) {
            return true;
        }
    }
    return false;
}

static void remember_device(usb_device_handle_t device, uint8_t address)
{
    for (size_t index = 0; index < sizeof(open_devices) / sizeof(open_devices[0]); ++index) {
        if (open_devices[index] == NULL) {
            open_devices[index] = device;
            open_device_addresses[index] = address;
            return;
        }
    }
    ESP_LOGW(TAG, "Device table full; closing extra device");
    usb_host_device_close(client_handle, device);
}

static void forget_device(usb_device_handle_t device)
{
    for (size_t index = 0; index < sizeof(open_devices) / sizeof(open_devices[0]); ++index) {
        if (open_devices[index] == device) {
            open_devices[index] = NULL;
            open_device_addresses[index] = 0;
            return;
        }
    }
}

static void print_device(uint8_t address)
{
    usb_device_handle_t device = NULL;
    esp_err_t error = usb_host_device_open(client_handle, address, &device);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Could not open address %u: %s", address, esp_err_to_name(error));
        return;
    }

    const usb_device_desc_t *descriptor = NULL;
    error = usb_host_get_device_descriptor(device, &descriptor);
    if (error != ESP_OK || descriptor == NULL) {
        ESP_LOGE(TAG, "Could not read cached descriptor for address %u: %s",
                 address, esp_err_to_name(error));
        usb_host_device_close(client_handle, device);
        return;
    }

    ESP_LOGI(TAG,
             "ENUMERATED address=%u VID:PID=%04X:%04X class=%02X configurations=%u",
             address, descriptor->idVendor, descriptor->idProduct,
             descriptor->bDeviceClass, descriptor->bNumConfigurations);
    if (descriptor->idVendor == 0x84EF &&
        (descriptor->idProduct == 0x0284 || descriptor->idProduct == 0x0184)) {
        ESP_LOGI(TAG, "SUCCESS: %s reached through its internal hub",
                 descriptor->idProduct == 0x0184 ? "Valeton GP-5" :
                                                   "Sonicake Pocket Master");
        if (!open_midi_device(device, descriptor)) {
            usb_host_device_close(client_handle, device);
            return;
        }
    } else if (descriptor->idVendor == 0x214B && descriptor->idProduct == 0x7250) {
        ESP_LOGI(TAG, "Pocket Master internal hub reached; waiting for downstream device");
    }
    remember_device(device, address);
}

static void retry_unopened_devices(void)
{
    uint8_t addresses[8];
    int count = 0;
    esp_err_t error = usb_host_device_addr_list_fill(8, addresses, &count);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "USB device scan failed: %s", esp_err_to_name(error));
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (!device_address_is_open(addresses[i])) {
            ESP_LOGI(TAG, "Retrying open for enumerated USB address %u", addresses[i]);
            print_device(addresses[i]);
        }
    }
}

static void client_event_callback(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        print_device(event->new_dev.address);
        return;
    }
    if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGW(TAG, "Device disconnected");
        if (event->dev_gone.dev_hdl == midi_device) {
            portENTER_CRITICAL(&midi_lock);
            midi_connected = false;
            midi_device = NULL;
            midi_tx_busy = false;
            midi_disconnected_at = xTaskGetTickCount();
            portEXIT_CRITICAL(&midi_lock);
        }
        forget_device(event->dev_gone.dev_hdl);
        usb_host_device_close(client_handle, event->dev_gone.dev_hdl);
    }
}

static void daemon_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t event_flags = 0;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
    }
}

static void client_task(void *arg)
{
    (void)arg;
    while (true) {
        esp_err_t error = usb_host_client_handle_events(client_handle, pdMS_TO_TICKS(1000));
        if (error == ESP_ERR_TIMEOUT) {
            portENTER_CRITICAL(&midi_lock);
            const bool recover_host = midi_ever_connected && !midi_connected &&
                midi_disconnected_at != 0 &&
                xTaskGetTickCount() - midi_disconnected_at >= pdMS_TO_TICKS(5000);
            portEXIT_CRITICAL(&midi_lock);
            if (recover_host) {
                ESP_LOGE(TAG, "Pedal stayed disconnected for 5 seconds; restarting USB host for recovery");
                esp_restart();
            }
            retry_unopened_devices();
        } else {
            ESP_ERROR_CHECK(error);
        }
    }
}

void app_main(void)
{
    esp_log_level_set("HUB", ESP_LOG_DEBUG);
    esp_log_level_set("EXT_HUB", ESP_LOG_DEBUG);
    esp_log_level_set("EXT_PORT", ESP_LOG_DEBUG);
    esp_log_level_set("ENUM", ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "ESP32-S3 hub timing diagnostic");
    ESP_LOGI(TAG,
             "Timings: downstream power=%d ms, downstream reset recovery=%d ms, root reset recovery=%d ms, attempts=5",
             CONFIG_USB_HOST_EXT_PORT_CUSTOM_POWER_ON_DELAY_MS,
             CONFIG_USB_HOST_EXT_PORT_RESET_RECOVERY_DELAY_MS,
             CONFIG_USB_HOST_RESET_RECOVERY_MS);

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = 0,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_callback,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_handle));

    midi_queue = xQueueCreate(8, sizeof(queued_midi_message_t));
    midi_tx_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(midi_queue != NULL && midi_tx_done != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t daemon_created = xTaskCreate(daemon_task, "usb-daemon", 4096, NULL, 3, NULL);
    BaseType_t client_created = xTaskCreate(client_task, "usb-client", 4096, NULL, 4, NULL);
    BaseType_t switches_created = xTaskCreate(switch_task, "switches", 4096, NULL, 2, NULL);
    BaseType_t midi_created = xTaskCreate(midi_task, "midi-tx", 4096, NULL, 3, NULL);
    ESP_ERROR_CHECK(daemon_created == pdPASS && client_created == pdPASS &&
                    switches_created == pdPASS && midi_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "USB host active; connect the powered Pocket Master");
}
