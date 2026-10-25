#include <vector>
#include <emm_structs.h>
#include <settings.h>
#include <emm.h>
#include <print>
#include <cassert>
#include <cmath>

void print_requested_power(std::span<ControlPowerInfo> power_infos) {
	for (const ControlPowerInfo &pi: power_infos) {
		if (&pi != &power_infos[0])
			std::print(",");
		std::print("{:8.1f}", pi.requested_power);
	}
	std::print("\n");
}

uint32_t now_s() { return time_us_64() / 1000000; }

ControlPowerInfo control_info(float power_max, float power_max_cha) {
	return ControlPowerInfo{
		.power_max = power_max,
		.power_max_cha = power_max_cha,
		.power_max_discha = power_max,
		.bat_priority = 1,
		.last_connection_s = now_s()
	};
}

bool near(float a, float b) { return std::abs(a - b) < .01f; }

// two equal inverters share the home power equally
void test_equal_split() {
	int id = 3;
	std::vector<InverterGroup> inverter_powers{
		InverterGroup{.inverter = {.device_id = id++, .exp_w = 20}, .bat_soc = 70},
		InverterGroup{.inverter = {.device_id = id++, .exp_w = 5000}, .bat_soc = 60},
	};
	std::vector<ControlPowerInfo> control_infos{control_info(10000, 10000), control_info(10000, 10000)};
	settings s {.max_export = 40000};
	for (int i: range(10)) {
		emm().update_power(5000, inverter_powers, control_infos, s);
		print_requested_power(control_infos);
		assert(control_infos[0].requested_power == control_infos[1].requested_power);
	}
}

// an unreachable inverter must not get a share of the power, the reachable one has to cover it
void test_inactive_inverter() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{
		InverterGroup{.pv = {.exp_w = 6000}, .bat_soc = 70},
		InverterGroup{.bat_soc = 0},
	};
	std::vector<ControlPowerInfo> control_infos{control_info(10000, 10000), control_info(10000, 10000)};
	control_infos[1].last_connection_s = now_s() - 100;
	control_infos[1].requested_power = 1234;
	settings s{};
	e.update_power(5000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 5000));
	assert(control_infos[1].requested_power == 0);
}

// several full inverters must not push more power into a fillable inverter than it can take up
void test_full_inverters_respect_fill_capacity() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{
		InverterGroup{.bat_soc = 100},
		InverterGroup{.bat_soc = 100},
		InverterGroup{.bat_soc = 50},
	};
	std::vector<ControlPowerInfo> control_infos{control_info(3000, 3000), control_info(3000, 3000), control_info(3000, 2000)};
	settings s{}; // no grid export allowed
	e.update_power(0, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	float sum{};
	for (const ControlPowerInfo &c: control_infos)
		sum += c.requested_power;
	assert(control_infos[2].requested_power >= -2000 - .01f);
	assert(near(sum, 0)); // nothing is exported to the grid
}

// without any fillable inverter the full inverters may only export up to max_export
void test_full_inverters_without_fillable() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 100}};
	std::vector<ControlPowerInfo> control_infos{control_info(3000, 3000)};
	settings s{.max_export = 500};
	e.update_power(1000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 1500)); // home power + allowed export
}

// a force charged inverter only takes the part of the charge power from the ac side that pv does not cover
void test_force_charge() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{
		InverterGroup{.pv = {.exp_w = 500}, .bat_soc = 5},
		InverterGroup{.pv = {.exp_w = 0}, .bat_soc = 80},
	};
	std::vector<ControlPowerInfo> control_infos{control_info(5000, 2000), control_info(5000, 5000)};
	control_infos[0].min_soc = 10;
	settings s{};
	e.update_power(1000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, -1500));
	assert(near(control_infos[1].requested_power, 2500)); // home power + the charging inverter
}

int main() {
	test_equal_split();
	test_inactive_inverter();
	test_full_inverters_respect_fill_capacity();
	test_full_inverters_without_fillable();
	test_force_charge();
	std::println("All tests passed");
	return 0;
}
