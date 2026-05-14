#include <vector>
#include <emm_structs.h>
#include <settings.h>
#include <emm.h>
#include <print>
#include <cassert>

void print_requested_power(std::span<ControlPowerInfo> power_infos) {
	for (const ControlPowerInfo &pi: power_infos) {
		if (&pi != &power_infos[0])
			std::print(",");
		std::print("{:8.1f}", pi.requested_power);
	}
	std::print("\n");
}

int main() {
	int id = 2;
	std::vector<InverterGroup> inverter_powers{
		InverterGroup{
			.inverter = {.device_id = id++, .exp_w = 20},
			.bat_soc = 70
		},
		InverterGroup{
			.inverter = {.device_id = id++, .exp_w = 5000},
			.bat_soc = 60
		},
	};
	std::vector<ControlPowerInfo> control_infos{
		ControlPowerInfo{
			.power_max = 10000,
			.power_max_cha = 10000,
			.power_max_discha = 10000,
			.bat_priority = 1,
			.last_connection_s = time_us_64()
		},
		ControlPowerInfo{
			.power_max = 10000,
			.power_max_cha = 10000,
			.power_max_discha = 10000,
			.bat_priority = 1,
			.last_connection_s = time_us_64()
		},
	};
	settings s {.max_export = 40000};
	for (int i: range(10)) {
		emm().update_power(5000, inverter_powers, control_infos, s);
		print_requested_power(control_infos);
		assert(control_infos[0].requested_power == control_infos[1].requested_power);
	}
	return 0;
}

