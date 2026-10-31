#pragma once
#include <furi.h>

#include "power_meter_state.h"

#define RECORD_POWER_METER "power_meter"
typedef struct PowerMeter PowerMeter;

#ifdef __cplusplus
extern "C" {
#endif

/** Measurements the LEDs are currently driven from. */
void power_meter_get_input(PowerMeter* instance, PowerMeterInput* input);

/** Drive the LEDs from the given numbers instead of the sensors. */
void power_meter_set_override(PowerMeter* instance, const PowerMeterInput* input);

/** Go back to driving the LEDs from the sensors. */
void power_meter_clear_override(PowerMeter* instance);

#ifdef __cplusplus
}
#endif
