#include "power_meter_cli.h"

#include <args.h>
#include <power_meter/power_meter.h>

static void power_meter_cli_help(void) {
    printf(
        "Usage: power_meter [<BUS_W> <SYSTEM_W> <CHARGE_PCT> <PD> <ERROR>]\r\n"
        "Without arguments prints what the LEDs are currently driven from.\r\n"
        "With arguments drives them from the given numbers instead, so the\r\n"
        "indication can be walked through without a bench supply.\r\n"
        "\tBUS_W \t\tpower coming in over USB-C1, milliwatts\r\n"
        "\tSYSTEM_W \tsystem consumption, milliwatts\r\n"
        "\tCHARGE_PCT \tbattery state of charge, 0-100\r\n"
        "\tPD \t\t1 if a PD contract is in place\r\n"
        "\tERROR \t\t1 to simulate a charger fault\r\n"
        "Use: power_meter off \tto go back to the sensors\r\n");
}

static void power_meter_cli_print(PowerMeter* power_meter) {
    PowerMeterInput input;
    power_meter_get_input(power_meter, &input);

    printf(
        "bus %.2f W, system %.2f W, charge %d%%, pd %d, error %d\r\n",
        (double)input.bus_w,
        (double)input.system_w,
        input.charge_pct,
        input.pd_active,
        input.error);
    printf("battery %.2f W\r\n", (double)power_meter_state_battery_w(&input));
}

void power_meter_cli(Cli* cli, FuriString* args, void* context) {
    UNUSED(cli);
    UNUSED(context);

    PowerMeter* power_meter = furi_record_open(RECORD_POWER_METER);

    do {
        if(furi_string_size(args) < 1) {
            power_meter_cli_print(power_meter);
            break;
        }

        if(furi_string_cmp_str(args, "off") == 0) {
            power_meter_clear_override(power_meter);
            printf("Driving the LEDs from the sensors again\r\n");
            break;
        }

        // milliwatts on the command line, so the values stay integers
        int bus_mw = 0;
        int system_mw = 0;
        int charge_pct = 0;
        int pd = 0;
        int error = 0;
        if(!args_read_int_and_trim(args, &bus_mw) || !args_read_int_and_trim(args, &system_mw) || !args_read_int_and_trim(args, &charge_pct) ||
           !args_read_int_and_trim(args, &pd) || !args_read_int_and_trim(args, &error)) {
            power_meter_cli_help();
            break;
        }

        if(charge_pct < 0 || charge_pct > 100) {
            printf("CHARGE_PCT must be 0-100\r\n");
            break;
        }

        PowerMeterInput input = {
            .bus_w = (float)bus_mw / 1000.0f,
            .system_w = (float)system_mw / 1000.0f,
            .charge_pct = (uint8_t)charge_pct,
            .pd_active = pd != 0,
            .error = error != 0,
        };
        power_meter_set_override(power_meter, &input);
        power_meter_cli_print(power_meter);
    } while(false);

    furi_record_close(RECORD_POWER_METER);
}
