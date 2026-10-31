#include "power_meter_state.h"

// The wattmeter LEDs in the order power_meter_state_render fills them in.
// Index 0 of each row is the LED closest to the battery icon, which is the
// first one to light up.
//
// Both rows run outwards from the battery icon, which follows from the WS2812
// chain order in led.h: line 3 is plug, UsbWatt1..4, battery centre, so
// UsbWatt1 is the chevron next to the plug, and line 2 is power, battery
// outline, BatteryWatt1..4, so BatteryWatt1 is the segment next to the icon.
static const LedType power_meter_led_types[PowerMeterLedCount] = {
    [PowerMeterLedPlug] = LedTypeUsbCharging,
    [PowerMeterLedBus1] = LedTypeUsbWatt1,
    [PowerMeterLedBus2] = LedTypeUsbWatt2,
    [PowerMeterLedBus3] = LedTypeUsbWatt3,
    [PowerMeterLedBus4] = LedTypeUsbWatt4,
    [PowerMeterLedBatteryOutline] = LedTypeBatteryOutline,
    [PowerMeterLedBatteryCenter] = LedTypeBatteryCenter,
    [PowerMeterLedBat1] = LedTypeBatteryWatt1,
    [PowerMeterLedBat2] = LedTypeBatteryWatt2,
    [PowerMeterLedBat3] = LedTypeBatteryWatt3,
    [PowerMeterLedBat4] = LedTypeBatteryWatt4,
};

// Colours are the nearest LED_COLOR_* to the reference renders in the demo.
// Hues measured off those renders, for whoever has to check them again:
// bar 2 W steps 61 / 33 / 5 deg, over-range rows below, battery icon warning
// 43 deg and the no-PD plug 44 deg, both of which land nearest ORANGE even
// though the demo calls those layers yellow.

// Over 8 W the bar stops filling up and the whole of it turns one scheme
static const LedColor power_meter_bat_bar_warn[4] = {
    // 68, 63, 6, 6 deg
    LED_COLOR_YELLOW,
    LED_COLOR_YELLOW,
    LED_COLOR_RED,
    LED_COLOR_RED,
};

static const LedColor power_meter_bat_bar_high[4] = {
    // 33, 35, 5, 5 deg
    LED_COLOR_ORANGE,
    LED_COLOR_ORANGE,
    LED_COLOR_RED,
    LED_COLOR_RED,
};

static const LedColor power_meter_bat_bar_over[4] = {
    // 24, 9, 6, 6 deg: the demo keeps warming the lowest segment rather than
    // turning the whole bar red, so it stays orange here
    LED_COLOR_ORANGE,
    LED_COLOR_RED,
    LED_COLOR_RED,
    LED_COLOR_RED,
};

static void power_meter_set(PowerMeterLeds* leds, PowerMeterLed led, LedColor color, bool blink) {
    leds->color[led] = color;
    leds->blink[led] = blink;
}

float power_meter_state_battery_w(const PowerMeterInput* input) {
    furi_check(input);
    // With a cable plugged in every watt the input does not cover comes out of
    // the battery. On battery alone the system idles above zero, so the
    // indication only starts once it draws more than the base consumption.
    float base = (input->bus_w > 0.0f) ? 0.0f : POWER_METER_BASE_CONSUMPTION_W;
    return input->system_w - input->bus_w - base;
}

LedType power_meter_state_led_type(PowerMeterLed led) {
    furi_check(led < PowerMeterLedCount);
    return power_meter_led_types[led];
}

void power_meter_state_render(const PowerMeterInput* input, PowerMeterLeds* leds) {
    furi_check(input);
    furi_check(leds);

    memset(leds, 0x00, sizeof(PowerMeterLeds));

    const bool powered = input->bus_w > 0.0f;
    const bool full = input->charge_pct >= POWER_METER_CHARGE_FULL_PCT;
    const float battery_w = power_meter_state_battery_w(input);

    // Plugged in and the input covers what the system eats: the rest tops up
    // the battery. A charger fault kills the charging indication entirely.
    const bool charging = powered && input->system_w <= input->bus_w && !input->error;
    // Plugged in but the input falls short, so the battery makes up the
    // difference. Unlike charging this keeps being shown in the error state.
    const bool draining = battery_w > 0.0f;
    const bool critical = (input->charge_pct < POWER_METER_CHARGE_CRITICAL_PCT) && !charging;
    const bool deficit = powered && input->system_w > input->bus_w;

    // The plug sits on whenever power comes in, green on a PD contract and
    // orange without one.
    if(powered) {
        LedColor plug = input->pd_active ? LED_COLOR_GREEN : LED_COLOR_ORANGE;
        power_meter_set(leds, PowerMeterLedPlug, plug, false);
    }

    // An idle full battery shows the plug alone, energy only flows through the
    // chevrons once there is something to charge or something to power.
    const bool chevrons = !input->error && (!full || input->system_w > 0.0f);
    const float bus_steps[4] = {
        POWER_METER_BUS_W_1,
        POWER_METER_BUS_W_2,
        POWER_METER_BUS_W_3,
        POWER_METER_BUS_W_4,
    };
    for(size_t i = 0; i < COUNT_OF(bus_steps); i++) {
        if(chevrons && input->bus_w > bus_steps[i]) {
            power_meter_set(leds, PowerMeterLedBus1 + i, LED_COLOR_GREEN, false);
        }
    }

    // Battery icon. The warnings take over the whole icon, so they are checked
    // before the green ones.
    if(input->error) {
        power_meter_set(leds, PowerMeterLedBatteryOutline, LED_COLOR_RED, true);
        power_meter_set(leds, PowerMeterLedBatteryCenter, LED_COLOR_RED, true);
    } else if(critical) {
        power_meter_set(leds, PowerMeterLedBatteryOutline, LED_COLOR_RED, true);
    } else if(deficit) {
        power_meter_set(leds, PowerMeterLedBatteryOutline, LED_COLOR_ORANGE, true);
        power_meter_set(leds, PowerMeterLedBatteryCenter, LED_COLOR_ORANGE, true);
    } else {
        // The bolt means power is flowing into a battery that can take it, and
        // the outline joins it there or when the battery feeds the system.
        const bool center = powered && !full;
        if(center) {
            power_meter_set(leds, PowerMeterLedBatteryCenter, LED_COLOR_GREEN, false);
        }
        if(center || draining) {
            power_meter_set(leds, PowerMeterLedBatteryOutline, LED_COLOR_GREEN, false);
        }
    }

    // IBAT bar. Up to 8 W the segments fill up one colour at a time, past that
    // the whole bar turns a single warning scheme.
    if(input->error) {
        return;
    }

    if(battery_w > POWER_METER_BAT_W_HIGH) {
        for(size_t i = 0; i < COUNT_OF(power_meter_bat_bar_over); i++) {
            power_meter_set(leds, PowerMeterLedBat1 + i, power_meter_bat_bar_over[i], false);
        }
    } else if(battery_w > POWER_METER_BAT_W_WARN) {
        for(size_t i = 0; i < COUNT_OF(power_meter_bat_bar_high); i++) {
            power_meter_set(leds, PowerMeterLedBat1 + i, power_meter_bat_bar_high[i], false);
        }
    } else if(battery_w > POWER_METER_BAT_W_BAR_MAX) {
        for(size_t i = 0; i < COUNT_OF(power_meter_bat_bar_warn); i++) {
            power_meter_set(leds, PowerMeterLedBat1 + i, power_meter_bat_bar_warn[i], false);
        }
    } else {
        const float bat_steps[4] = {
            POWER_METER_BAT_W_1,
            POWER_METER_BAT_W_2,
            POWER_METER_BAT_W_3,
            POWER_METER_BAT_W_4,
        };
        const LedColor bat_colors[4] = {
            LED_COLOR_GREEN,
            LED_COLOR_YELLOW,
            LED_COLOR_ORANGE,
            LED_COLOR_RED,
        };
        for(size_t i = 0; i < COUNT_OF(bat_steps); i++) {
            if(battery_w > bat_steps[i]) {
                power_meter_set(leds, PowerMeterLedBat1 + i, bat_colors[i], false);
            }
        }
    }
}
