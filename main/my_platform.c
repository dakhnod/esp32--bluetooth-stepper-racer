// Example file - Public Domain
// Need help? https://tinyurl.com/bluepad32-help

#include <string.h>

#include <uni.h>

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <math.h>
#include "esp_attr.h"
#include <btstack.h>
#include <esp_intr_alloc.h>
#include <freertos/freertos.h>
#include <freertos/task.h>

#define LEDC_MODE               LEDC_HIGH_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               pow(2, LEDC_DUTY_RES - 1) // Set duty to 50%. (2 ** 13) * 50% = 4096

#define LEDC_SERVO_DUTY_RES     LEDC_TIMER_11_BIT
#define LEDC_SERVO_FREQUENCY    50
#define LEDC_SERVO_DUTY         150

#define PIN_ENABLE 27

#define LEFT_DIR 25
#define RIGHT_DIR 32

#define LEFT_STEP 26
#define RIGHT_STEP 33

#define SERVO_RIGHT_PIN 4
#define SERVO_LEFT_PIN 15

#define LED_BUILTIN 1

#define PIN_PDN_UART 16

#define PIN_VIBRATION_SENSOR 23

#define PIN_LEFT_SERVO 5
#define PIN_RIGHT_SERVO 4

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

void rumble(void *context);
static btstack_context_callback_registration_t callback_registration = {
    .callback = rumble
};

int pin_states[] = {
    PIN_ENABLE, 1,
    PIN_PDN_UART, 1,
    LEFT_DIR, 0,
    RIGHT_DIR, 1,
    // LEFT_STEP, 0,
    // RIGHT_STEP, 0
};

int step_pins[] = {LEFT_STEP, RIGHT_STEP};

static esp_timer_handle_t step_timers[2];

// Declarations
static void trigger_event_on_gamepad(uni_hid_device_t* d);

volatile int step_intervals[] = {0, 0};
volatile bool step_states[] = {false, false};

void IRAM_ATTR step_timer_callback(void *arg) {
    int index = (int) arg;

    if(step_intervals[index] == 0) {
        return;
    }

    bool state = step_states[index];

    gpio_set_level(step_pins[index], state);

    esp_timer_restart(step_timers[index], step_intervals[index]);

    step_states[index] = !state;
}

void set_frequency(int index, int freq) {
    // step_intervals[index] = 2000000 / freq;
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = index,
        .freq_hz          = freq,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
        // .clk_cfg          = LEDC_USE_REF_TICK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
}

void configure_channel(int index) {
    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = index,
        .timer_sel      = index,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = step_pins[index],
        .duty           = LEDC_DUTY,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // ledc_stop(LEDC_MODE, index, 0);
}

void configure_servo_pwm(int index) {
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_HIGH_SPEED_MODE,
        .channel        = index + 2,
        .timer_sel      = 2,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = index ? PIN_RIGHT_SERVO : PIN_LEFT_SERVO,
        .duty           = LEDC_SERVO_DUTY,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void rumble(void *context) {
    uni_hid_device_t* d;

    d = uni_hid_device_get_first_device_with_state(UNI_BT_CONN_STATE_DEVICE_READY);

    // Safety checks in case the gamepad got disconnected while the callback was scheduled
    if (!d) return;
    if (!uni_bt_conn_is_connected(&d->conn)) return;

    if (d->report_parser.play_dual_rumble != NULL) {
        d->report_parser.play_dual_rumble(d, 0, 400, 0x80, 0x80);
    }
}

void IRAM_ATTR gpio_isr_vibration_handler(void* arg) {
    // Notify the task waiting on the semaphore
    static unsigned long lastExecution = 0;
    unsigned long now = xTaskGetTickCountFromISR();

    unsigned long delta = now - lastExecution;

    if(delta < 1000) {
        return;
    }
    lastExecution = now;

    btstack_run_loop_execute_on_main_thread(&callback_registration);
}

//
// Platform Overrides
//
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("custom: init()\n");

    // configure_channel(0);
    // configure_channel(1);

    // set_frequency(0, 1000);
    // set_frequency(1, 1000);

    for(int i = 0; i < sizeof(pin_states) / sizeof(pin_states[0]); i += 2) {
        gpio_set_direction(pin_states[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pin_states[i], pin_states[i + 1]);
    }

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_HIGH_SPEED_MODE,
        .duty_resolution  = LEDC_SERVO_DUTY_RES,
        .timer_num        = 2,
        .freq_hz          = LEDC_SERVO_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
        // .clk_cfg          = LEDC_USE_REF_TICK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    configure_servo_pwm(0);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VIBRATION_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        loge("Failed to configure GPIO: %s", esp_err_to_name(err));
        return;
    }

    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    gpio_isr_handler_add(PIN_VIBRATION_SENSOR, gpio_isr_vibration_handler, NULL);

    return;

    err = gpio_isr_register(gpio_isr_vibration_handler, NULL, ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE, NULL);
    if (err != ESP_OK) {
        loge("Failed gpio_isr_register: %s", esp_err_to_name(err));
        return;
    }

    logi("Interrupt registered\n");
}


static void my_platform_on_init_complete(void) {
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

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    // You can filter discovered devices here.
    // Just return any value different from UNI_ERROR_SUCCESS;
    // @param addr: the Bluetooth address
    // @param name: could be NULL, could be zero-length, or might contain the name.
    // @param cod: Class of Device. See "uni_bt_defines.h" for possible values.
    // @param rssi: Received Signal Strength Indicator (RSSI) measured in dBms. The higher (255) the better.

    // As an example, if you want to filter out keyboards, do:
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        logi("Ignoring keyboard\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }

    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("custom: device connected: %p\n", d);
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("custom: device disconnected: %p\n", d);
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("custom: device ready: %p\n", d);

    trigger_event_on_gamepad(d);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    static uni_controller_t prev = {0};
    uni_gamepad_t* gp;

    // Optimization to avoid processing the previous data so that the console
    // does not get spammed with a lot of logs, but remove it from your project.
    if (memcmp(&prev, ctl, sizeof(*ctl)) == 0) {
        return;
    }
    prev = *ctl;
    // Print device Id before dumping gamepad.
    // This could be very CPU intensive and might crash the ESP32.
    // Remove these 2 lines in production code.
    //    logi("(%p), id=%d, \n", d, uni_hid_device_get_idx_for_instance(d));
    //    uni_controller_dump(ctl);

    if(ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    gp = &ctl->gamepad;

    int collective;

    if(gp->brake) {
        collective = gp->brake * -3;
    }else{
        collective = gp->throttle * 6;
    }

    int axis_x = gp->axis_rx;
    if(abs(axis_x) < 200) {
       axis_x = 0;
    }

    int frequency_left = collective + (axis_x * 3);
    int frequency_right = collective - (axis_x * 3);

    gpio_set_level(PIN_ENABLE, !(frequency_left || frequency_right));

    // logi("left: %d  right: %d:  axis: %d\n", frequency_left, frequency_right, axis_x);

    gpio_set_level(RIGHT_DIR, frequency_right > 0);
    gpio_set_level(LEFT_DIR, frequency_left < 0);

    static bool pwm_left_enabled = false;
    static bool pwm_right_enabled = false;

    if(frequency_left) {
        if(!pwm_left_enabled){
            configure_channel(0);
            pwm_left_enabled = true;
        }
        set_frequency(0, abs(frequency_left));
    }else if(pwm_left_enabled) {
        ledc_stop(LEDC_MODE, 1, 0);
        pwm_left_enabled = false;
    }

    if(frequency_right) {
        if(!pwm_right_enabled){
            configure_channel(1);
            pwm_right_enabled = true;
        }
        set_frequency(1, abs(frequency_right));
    }else if(pwm_right_enabled) {
        ledc_stop(LEDC_MODE, 1, 1);
        pwm_right_enabled = false;
    }
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t* d = data;

            if (d == NULL) {
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

static void trigger_event_on_gamepad(uni_hid_device_t* d) {
    
}

//
// Entry Point
//
struct uni_platform* get_my_platform(void) {
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
