// Example file - Public Domain
// Need help? https://tinyurl.com/bluepad32-help

#include <string.h>

#include <uni.h>

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <math.h>
#include "esp_attr.h"

#define LEDC_MODE LEDC_HIGH_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT     // Set duty resolution to 13 bits
#define LEDC_DUTY pow(2, LEDC_DUTY_RES - 1) // Set duty to 50%. (2 ** 13) * 50% = 4096

#define PIN_ENABLE 27

#define SERVO_RIGHT_PIN 4
#define SERVO_LEFT_PIN 15

#define LED_BUILTIN 1

#define UART_RX_PIN 16
#define UART_TX_PIN 17

#define TMC_MESSAGE_PAUSE pdMS_TO_TICKS(10)

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

int pin_states[] = {
    PIN_ENABLE, 0,
};

// Declarations
static void trigger_event_on_gamepad(uni_hid_device_t *d);

uint8_t calculate_tmc_crc(uint8_t *data, int size)
{
    uint8_t crc = 0;
    uint8_t byte;
    for (uint8_t i = 0; i < size; ++i)
    {
        byte = data[i];
        for (uint8_t j = 0; j < 8; ++j)
        {
            if ((crc >> 7) ^ (byte & 0x01))
            {
                crc = (crc << 1) ^ 0x07;
            }
            else
            {
                crc = crc << 1;
            }
            byte = byte >> 1;
        }
    }
    return crc;
}

void tmc_set_register(uint8_t address, uint8_t reg, int32_t value) {
    uint8_t frame[8] = {85, address, 0b10000000 | reg};

    for(int i = 0; i < 4; i++) {
        frame[3 + i] = (value >> ((3 - i) * 8)) & 0xFF;
    }

    frame[7] = calculate_tmc_crc(frame, 7);

    uart_write_bytes(UART_NUM_1, frame, sizeof(frame));
}

void tmc_uart_init(){
    // Setup UART buffered IO with event queue
    QueueHandle_t uart_queue;
    const uart_port_t uart_num = UART_NUM_1;

    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_HW_FIFO_LEN(uart_num) + 4, 0, 10, &uart_queue, 0));
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

    ESP_ERROR_CHECK(uart_set_pin(uart_num, UART_TX_PIN, UART_RX_PIN, -1, -1));
}

void set_frequency(int index, int freq)
{
    tmc_set_register(index, 0x22, freq);
}

//
// Platform Overrides
//
static void my_platform_init(int argc, const char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("custom: init()\n");

    for (int i = 0; i < sizeof(pin_states) / sizeof(pin_states[0]); i += 2)
    {
        gpio_set_direction(pin_states[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pin_states[i], pin_states[i + 1]);
    }

    tmc_uart_init();

    vTaskDelay(pdMS_TO_TICKS(100));
    tmc_set_register(0x00, 0x22, 1000);

    /*
    tmc_set_register(0x00, 0x00, 0b11001000);
    vTaskDelay(TMC_MESSAGE_PAUSE);
    tmc_set_register(0x00, 0x10, 0x00001f00);
    vTaskDelay(TMC_MESSAGE_PAUSE);
    tmc_set_register(0x00, 0x6C, 0x1f000053);
    vTaskDelay(TMC_MESSAGE_PAUSE);
    tmc_set_register(0x00, 0x22, 0);

    return;

    tmc_set_register(0x01, 0x00, 0b11000000);
    vTaskDelay(TMC_MESSAGE_PAUSE);
    tmc_set_register(0x01, 0x10, 0x00001000);
    vTaskDelay(TMC_MESSAGE_PAUSE);
    tmc_set_register(0x01, 0x6C, 0x1f000053);
    */
}

static void my_platform_on_init_complete(void)
{
    logi("custom: on_init_complete()\n");

    // Safe to call "unsafe" functions since they are called from BT thread

    // Start scanning
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);

    // Based on runtime condition, you can delete or list the stored BT keys.
    if (0)
        uni_bt_del_keys_unsafe();
    else
        uni_bt_list_keys_unsafe();
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi)
{
    // You can filter discovered devices here.
    // Just return any value different from UNI_ERROR_SUCCESS;
    // @param addr: the Bluetooth address
    // @param name: could be NULL, could be zero-length, or might contain the name.
    // @param cod: Class of Device. See "uni_bt_defines.h" for possible values.
    // @param rssi: Received Signal Strength Indicator (RSSI) measured in dBms. The higher (255) the better.

    // As an example, if you want to filter out keyboards, do:
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD)
    {
        logi("Ignoring keyboard\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }

    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t *d)
{
    logi("custom: device connected: %p\n", d);
}

static void my_platform_on_device_disconnected(uni_hid_device_t *d)
{
    logi("custom: device disconnected: %p\n", d);
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t *d)
{
    logi("custom: device ready: %p\n", d);

    trigger_event_on_gamepad(d);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
    static uni_controller_t prev = {0};
    uni_gamepad_t *gp;

    // Optimization to avoid processing the previous data so that the console
    // does not get spammed with a lot of logs, but remove it from your project.
    if (memcmp(&prev, ctl, sizeof(*ctl)) == 0)
    {
        return;
    }
    prev = *ctl;
    // Print device Id before dumping gamepad.
    // This could be very CPU intensive and might crash the ESP32.
    // Remove these 2 lines in production code.
    //    logi("(%p), id=%d, \n", d, uni_hid_device_get_idx_for_instance(d));
    //    uni_controller_dump(ctl);

    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
    {
        return;
    }

    gp = &ctl->gamepad;

    int collective;

    if (gp->brake){
        collective = -gp->brake;
    }else{
        collective = gp->throttle * 3;
    }

    int axis = gp->axis_rx;

    // set_frequency(0, collective + axis);
    // vTaskDelay(pdMS_TO_TICKS(1));
    set_frequency(1, collective - axis);
    vTaskDelay(pdMS_TO_TICKS(1));
}

static const uni_property_t *my_platform_get_property(uni_property_idx_t idx)
{
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void *data)
{
    switch (event)
    {
    case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON:
    {
        uni_hid_device_t *d = data;

        if (d == NULL)
        {
            loge("ERROR: my_platform_on_oob_event: Invalid NULL device\n");
            return;
        }
        logi("custom: on_device_oob_event(): %d\n", event);

        trigger_event_on_gamepad(d);
        break;
    }

    case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
        logi("custom: Bluetooth enabled: %d\n", (bool)(data));
        break;

    default:
        logi("my_platform_on_oob_event: unsupported event: 0x%04x\n", event);
        break;
    }
}

static void trigger_event_on_gamepad(uni_hid_device_t *d)
{
}

//
// Entry Point
//
struct uni_platform *get_my_platform(void)
{
    static struct uni_platform plat = {
        .name = "custom",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_discovered = my_platform_on_device_discovered,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };

    return &plat;
}
