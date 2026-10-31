#pragma once
#include <furi.h>
#include <led/led.h>

/**
 * Power Meter LED state machine, see https://github.com/flipperdevices/flipperone-mcu-firmware/issues/254
 *
 * Pure logic: it takes a set of measurements and produces the colour of every
 * LED the power meter owns. No I2C, no LED service, so it can be exercised
 * from the CLI with made up numbers.
 */

// Power drawn by the system below which the battery indication stays dark.
// TODO: the real figure is not measured yet (issue #254), 0.3 W is the value
// the interactive demo uses.
#define POWER_METER_BASE_CONSUMPTION_W (0.3f)

// IBUS chevrons light up one by one as the input power grows
#define POWER_METER_BUS_W_1 (0.0f)
#define POWER_METER_BUS_W_2 (7.5f)
#define POWER_METER_BUS_W_3 (15.0f)
#define POWER_METER_BUS_W_4 (27.0f)

// IBAT bar: below BAT_W_BAR_MAX the four segments fill up 2 W at a time,
// above it the whole bar lights in one of three warning colour schemes
#define POWER_METER_BAT_W_1       (0.0f)
#define POWER_METER_BAT_W_2       (2.0f)
#define POWER_METER_BAT_W_3       (4.0f)
#define POWER_METER_BAT_W_4       (6.0f)
#define POWER_METER_BAT_W_BAR_MAX (8.0f)
#define POWER_METER_BAT_W_WARN    (12.0f)
#define POWER_METER_BAT_W_HIGH    (24.0f)

// Charge level below which the battery outline blinks red
#define POWER_METER_CHARGE_CRITICAL_PCT (5U)
#define POWER_METER_CHARGE_FULL_PCT     (100U)

typedef enum {
    // the plug, closest to the USB-C1 connector
    PowerMeterLedPlug,
    // IBUS chevrons, 1 is the one next to the plug
    PowerMeterLedBus1,
    PowerMeterLedBus2,
    PowerMeterLedBus3,
    PowerMeterLedBus4,
    // the battery icon in the middle of the row
    PowerMeterLedBatteryOutline,
    PowerMeterLedBatteryCenter,
    // IBAT bar, 1 is the one next to the battery icon
    PowerMeterLedBat1,
    PowerMeterLedBat2,
    PowerMeterLedBat3,
    PowerMeterLedBat4,

    PowerMeterLedCount, // internal use only, keep it last
} PowerMeterLed;

typedef struct {
    float bus_w; // power coming in over USB-C1
    float system_w; // what the whole system consumes
    uint8_t charge_pct; // battery state of charge
    bool pd_active; // a PD contract is in place
    bool error; // the charger reports a fault
} PowerMeterInput;

typedef struct {
    LedColor color[PowerMeterLedCount];
    bool blink[PowerMeterLedCount];
} PowerMeterLeds;

#ifdef __cplusplus
extern "C" {
#endif

/** Turn a set of measurements into the colour of every power meter LED. */
void power_meter_state_render(const PowerMeterInput* input, PowerMeterLeds* leds);

/** Power the battery delivers to cover what the input does not, in watts. */
float power_meter_state_battery_w(const PowerMeterInput* input);

/** LedType driving the given power meter LED. */
LedType power_meter_state_led_type(PowerMeterLed led);

#ifdef __cplusplus
}
#endif
