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

uint32_t now_s() { return std::max<uint32_t>(time_us_64() / 1000000, 1); } // 0 means never connected

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
		InverterGroup{.inverter = {.device_id = id++, .exp_w = 20}, .bat_soc = 60},
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
	control_infos[1].last_connection_s = 0; // never connected
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

// an inverter which can not deliver its share (battery at min soc, no pv) must not get it, the other covers it
void test_share_limited_by_available_power() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 10}, InverterGroup{.bat_soc = 80}};
	std::vector<ControlPowerInfo> control_infos{control_info(5000, 5000), control_info(5000, 5000)};
	control_infos[0].min_soc = 10;
	settings s{};
	e.update_power(2000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 0));
	assert(near(control_infos[1].requested_power, 2000));
}

// a small discharge limit caps the share, the rest goes to the other inverter
void test_share_limited_by_discharge() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 50}, InverterGroup{.bat_soc = 50}};
	std::vector<ControlPowerInfo> control_infos{control_info(5000, 5000), control_info(5000, 5000)};
	control_infos[0].power_max_discha = 500;
	settings s{};
	e.update_power(4000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 500));
	assert(near(control_infos[1].requested_power, 3500));
}

// the grid export of a full inverter is limited by what the inverter can deliver
void test_full_inverter_export_respects_power_max() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 100}};
	std::vector<ControlPowerInfo> control_infos{control_info(3000, 3000)};
	settings s{.max_export = 1000};
	e.update_power(2500, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 3000));
}

// a surplus from the home side is taken up by batteries that are not full, up to their charge power
void test_surplus_taken_up() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 50}, InverterGroup{.bat_soc = 100}};
	std::vector<ControlPowerInfo> control_infos{control_info(5000, 2000), control_info(5000, 5000)};
	settings s{};
	e.update_power(-3000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, -2000));
	assert(control_infos[1].requested_power <= .01f); // full battery does not export on top of the surplus
}

// the split of two inverters scales linearly with the soc difference, 20 or more gives everything to the fuller one
void test_soc_balancing_two() {
	struct Case { float soc_a, soc_b, share_a; };
	for (Case c: {Case{50, 50, .5f}, Case{55, 50, .625f}, Case{60, 50, .75f}, Case{70, 50, 1.f}, Case{90, 50, 1.f}, Case{40, 50, .25f}, Case{30, 50, 0.f}}) {
		EMM e{.filter_alpha = 0};
		std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = c.soc_a}, InverterGroup{.bat_soc = c.soc_b}};
		std::vector<ControlPowerInfo> control_infos{control_info(5000, 5000), control_info(5000, 5000)};
		settings s{};
		e.update_power(4000, inverter_powers, control_infos, s);
		std::print("soc {} vs {}: ", c.soc_a, c.soc_b);
		print_requested_power(control_infos);
		assert(near(control_infos[0].requested_power, 4000 * c.share_a));
		assert(near(control_infos[1].requested_power, 4000 * (1 - c.share_a)));
	}
}

// if the fuller inverter can not deliver everything the other one covers the rest
void test_soc_balancing_limited() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 80}, InverterGroup{.bat_soc = 40}};
	std::vector<ControlPowerInfo> control_infos{control_info(3000, 3000), control_info(5000, 5000)};
	settings s{};
	e.update_power(4000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, 3000));
	assert(near(control_infos[1].requested_power, 1000));
}

// a surplus is taken up preferably by the emptier battery
void test_soc_balancing_surplus() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 60}, InverterGroup{.bat_soc = 50}};
	std::vector<ControlPowerInfo> control_infos{control_info(5000, 5000), control_info(5000, 5000)};
	settings s{};
	e.update_power(-2000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	assert(near(control_infos[0].requested_power, -500));
	assert(near(control_infos[1].requested_power, -1500));
}

// with three inverters the shares add up and the fullest one delivers the most
void test_soc_balancing_three() {
	EMM e{.filter_alpha = 0};
	std::vector<InverterGroup> inverter_powers{InverterGroup{.bat_soc = 80}, InverterGroup{.bat_soc = 70}, InverterGroup{.bat_soc = 40}};
	std::vector<ControlPowerInfo> control_infos{control_info(9000, 9000), control_info(9000, 9000), control_info(9000, 9000)};
	settings s{};
	e.update_power(3000, inverter_powers, control_infos, s);
	print_requested_power(control_infos);
	// pairwise: 80 vs 70 -> .75/.25, 80 vs 40 -> 1/0, 70 vs 40 -> 1/0, weights 1.75, 1.25, 0 of 3
	assert(near(control_infos[0].requested_power, 1750));
	assert(near(control_infos[1].requested_power, 1250));
	assert(near(control_infos[2].requested_power, 0));
}

int main() {
	test_equal_split();
	test_inactive_inverter();
	test_full_inverters_respect_fill_capacity();
	test_full_inverters_without_fillable();
	test_force_charge();
	test_share_limited_by_available_power();
	test_share_limited_by_discharge();
	test_full_inverter_export_respects_power_max();
	test_surplus_taken_up();
	test_soc_balancing_two();
	test_soc_balancing_limited();
	test_soc_balancing_surplus();
	test_soc_balancing_three();
	std::println("All tests passed");
	return 0;
}
