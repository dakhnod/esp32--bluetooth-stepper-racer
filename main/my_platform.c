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

#define LEDC_MODE               LEDC_HIGH_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               pow(2, LEDC_DUTY_RES - 1) // Set duty to 50%. (2 ** 13) * 50% = 4096

#define PIN_ENABLE 27

#define LEFT_DIR 25
#define RIGHT_DIR 32

#define LEFT_STEP 26
#define RIGHT_STEP 33

#define SERVO_RIGHT_PIN 4
#define SERVO_LEFT_PIN 15

#define LED_BUILTIN 1

#define PIN_PDN_UART 16

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

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

    return;

    const esp_timer_create_args_t left_step_timer_args = {
        .callback = &step_timer_callback,
        .name = "left_step_timer",
        .dispatch_method = ESP_TIMER_ISR,
        .arg = (void*) 0
    };
    ESP_ERROR_CHECK(esp_timer_create(&left_step_timer_args, step_timers + 0));

    const esp_timer_create_args_t right_step_timer_args = {
        .callback = &step_timer_callback,
        .name = "right_step_timer",
        .dispatch_method = ESP_TIMER_ISR,
        .arg = (void*) 1
    };
    ESP_ERROR_CHECK(esp_timer_create(&right_step_timer_args, step_timers + 1));

    for(int i = 0; i < sizeof(pin_states) / sizeof(pin_states[0]); i += 2) {
        gpio_set_direction(pin_states[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pin_states[i], pin_states[i + 1]);
    }

    ESP_ERROR_CHECK(esp_timer_start_periodic(step_timers[0], 1000));
    ESP_ERROR_CHECK(esp_timer_start_periodic(step_timers[1], 1000));
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
    static uint8_t leds = 0;
    static uint8_t enabled = true;
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

    static bool pwm_enabled = false;

    static int last_throttle = 0;
    static int last_rx = 0;

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


    return;

    if(collective == 0) {
        if(!pwm_enabled) {
            return;
        }
        gpio_set_level(PIN_ENABLE, 1);

        ledc_stop(LEDC_MODE, 0, 0);
        ledc_stop(LEDC_MODE, 1, 0);

        pwm_enabled = false;
        return;
    }

    collective *= 6;

    int axis = gp->axis_rx * 3;

    set_frequency(0, max(collective + axis, 1));
    set_frequency(1, max(collective - axis, 1));

    if(!pwm_enabled) {
        gpio_set_level(PIN_ENABLE, 0);
        configure_channel(0);
        configure_channel(1);
    }

    pwm_enabled = true;
    logi("axis: %d\n", collective);
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
