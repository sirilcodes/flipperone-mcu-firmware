#include "power_meter.h"

#include <api_lock.h>
#include <led/led.h>
#include <power/power.h>

#define POWER_METER_MAX_MESSAGES         (4U)
#define POWER_METER_SAMPLE_PERIOD_MS     (250U)
// The warning states blink at 1 Hz, half the period on and half off
#define POWER_METER_BLINK_HALF_PERIOD_MS (500U)

// VBUS above this means the port negotiated more than the default 5 V, which
// only a PD contract does.
// TODO: a 5 V PD contract is indistinguishable from no PD here. Replace this
// with the negotiated contract once the PD service exposes one (issue #254).
#define POWER_METER_PD_VBUS_MV (5500U)

// Everything in the charger fault registers except IBAT regulation, which is a
// normal operating condition rather than a fault
#define POWER_METER_FAULT0_MASK (0xF7U)
#define POWER_METER_FAULT1_MASK (0xFFU)

struct PowerMeter {
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* timer;
    FuriMessageQueue* message_queue;
    Power* power;
    Led* led;

    PowerMeterInput input;
    bool override;

    // what was last pushed to the led service, so only changes are sent
    LedColor shown[PowerMeterLedCount];
    bool shown_valid;
    bool blink_phase;
};

typedef enum {
    PowerMeterMessageTypeSetOverride,
    PowerMeterMessageTypeClearOverride,
    PowerMeterMessageTypeGetInput,
} PowerMeterMessageType;

typedef struct {
    PowerMeterMessageType type;
    FuriApiLock lock;
    PowerMeterInput* input;
} PowerMeterMessage;

static bool power_meter_colors_equal(LedColor a, LedColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void power_meter_sample(PowerMeter* instance, PowerMeterInput* input) {
    memset(input, 0x00, sizeof(PowerMeterInput));

    // The power_* getters leave their result untouched and log an error when
    // the device did not come up, so ask what is there before reading anything
    PowerDevice devices = 0;
    power_is_device_initialized(instance->power, &devices);

    if(devices & PowerDeviceBq25792) {
        int16_t ibus_ma = 0;
        uint16_t vbus_mv = 0;
        if(power_bq25792_get_ibus_ma(instance->power, &ibus_ma) && power_bq25792_get_vbus_mv(instance->power, &vbus_mv)) {
            // IBUS goes negative when the port sources power, which the
            // indication treats the same as nothing coming in
            if(ibus_ma > 0) {
                input->bus_w = ((float)ibus_ma / 1000.0f) * ((float)vbus_mv / 1000.0f);
            }
            input->pd_active = vbus_mv >= POWER_METER_PD_VBUS_MV;
        }

        Bq25792FaultStatusReg fault = {0};
        if(power_bq25792_get_charger_fault(instance->power, &fault)) {
            input->error = (fault.data[0] & POWER_METER_FAULT0_MASK) || (fault.data[1] & POWER_METER_FAULT1_MASK);
        }
    }

    if(devices & PowerDeviceIna219) {
        input->system_w = power_ina219_get_power_w(instance->power);
    }

    if(devices & PowerDeviceBq28z620) {
        uint8_t charge_pct = 0;
        if(power_bq28z620_get_relative_state_of_charge(instance->power, &charge_pct)) {
            input->charge_pct = charge_pct;
        }
    }
}

static void power_meter_apply(PowerMeter* instance, const PowerMeterLeds* leds) {
    for(size_t i = 0; i < PowerMeterLedCount; i++) {
        LedColor color = leds->color[i];
        if(leds->blink[i] && !instance->blink_phase) {
            color = LED_COLOR_BLACK;
        }

        if(instance->shown_valid && power_meter_colors_equal(instance->shown[i], color)) {
            continue;
        }

        led_set_color_single(instance->led, power_meter_state_led_type(i), color);
        instance->shown[i] = color;
    }
    instance->shown_valid = true;
}

static void power_meter_update(PowerMeter* instance) {
    if(!instance->override) {
        power_meter_sample(instance, &instance->input);
    }

    const uint32_t blink_ticks = furi_ms_to_ticks(POWER_METER_BLINK_HALF_PERIOD_MS);
    instance->blink_phase = (furi_get_tick() / blink_ticks) % 2 == 0;

    PowerMeterLeds leds;
    power_meter_state_render(&instance->input, &leds);
    power_meter_apply(instance, &leds);
}

static void power_meter_timer_callback(void* context) {
    furi_assert(context);
    PowerMeter* instance = (PowerMeter*)context;
    power_meter_update(instance);
}

static void power_meter_message_queue_callback(FuriEventLoopObject* object, void* context) {
    furi_assert(context);
    PowerMeter* instance = (PowerMeter*)context;
    furi_assert(object == instance->message_queue);

    PowerMeterMessage msg;
    furi_check(furi_message_queue_get(instance->message_queue, &msg, 0) == FuriStatusOk);

    switch(msg.type) {
    case PowerMeterMessageTypeSetOverride:
        instance->input = *msg.input;
        instance->override = true;
        power_meter_update(instance);
        break;
    case PowerMeterMessageTypeClearOverride:
        instance->override = false;
        power_meter_update(instance);
        break;
    case PowerMeterMessageTypeGetInput:
        *msg.input = instance->input;
        break;
    default:
        furi_crash("Invalid message type");
        break;
    }

    if(msg.lock) {
        api_lock_unlock(msg.lock);
    }
}

static void power_meter_send_message(PowerMeter* instance, PowerMeterMessage* message) {
    message->lock = api_lock_alloc_locked();
    furi_check(furi_message_queue_put(instance->message_queue, message, FuriWaitForever) == FuriStatusOk);
    api_lock_wait_unlock_and_free(message->lock);
}

void power_meter_get_input(PowerMeter* instance, PowerMeterInput* input) {
    furi_check(instance);
    furi_check(input);

    PowerMeterMessage msg = {
        .type = PowerMeterMessageTypeGetInput,
        .input = input,
    };
    power_meter_send_message(instance, &msg);
}

void power_meter_set_override(PowerMeter* instance, const PowerMeterInput* input) {
    furi_check(instance);
    furi_check(input);

    PowerMeterInput copy = *input;
    PowerMeterMessage msg = {
        .type = PowerMeterMessageTypeSetOverride,
        .input = &copy,
    };
    power_meter_send_message(instance, &msg);
}

void power_meter_clear_override(PowerMeter* instance) {
    furi_check(instance);

    PowerMeterMessage msg = {
        .type = PowerMeterMessageTypeClearOverride,
    };
    power_meter_send_message(instance, &msg);
}

static PowerMeter* power_meter_alloc(void) {
    PowerMeter* instance = (PowerMeter*)malloc(sizeof(PowerMeter));
    memset(instance, 0x00, sizeof(PowerMeter));
    instance->event_loop = furi_event_loop_alloc();
    instance->message_queue = furi_message_queue_alloc(POWER_METER_MAX_MESSAGES, sizeof(PowerMeterMessage));

    instance->power = furi_record_open(RECORD_POWER);
    instance->led = furi_record_open(RECORD_LEDS);

    furi_event_loop_subscribe_message_queue(instance->event_loop, instance->message_queue, FuriEventLoopEventIn, power_meter_message_queue_callback, instance);
    instance->timer = furi_event_loop_timer_alloc(instance->event_loop, power_meter_timer_callback, FuriEventLoopTimerTypePeriodic, instance);
    furi_event_loop_timer_start(instance->timer, POWER_METER_SAMPLE_PERIOD_MS);

    furi_record_create(RECORD_POWER_METER, instance);

    return instance;
}

int32_t power_meter_srv(void* p) {
    UNUSED(p);

    PowerMeter* instance = power_meter_alloc();
    furi_event_loop_run(instance->event_loop);

    return 0;
}
