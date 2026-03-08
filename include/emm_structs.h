#pragma once

#include "static_types.h"

struct ModbusTcpAddr {
    uint32_t ip;
    uint16_t port;
    uint8_t modbus_id;

    bool operator<=>(const ModbusTcpAddr &o) const = default;
};

struct PowerInfo {
	int device_id;
	float imp_w;
	float exp_w;
};

struct InverterGroup {
	PowerInfo inverter, pv, battery;
};

struct ControlPowerInfo {
	float min_soc;		// min soc of the battery
	float soc;		// soc of the battery
	float power_max;	// maximum
	float power_max_cha;	// maximum battery charge
	float power_max_discha;	// maximum battery discharge
	float requested_power;	// requested power by the emm
	int fill_priority;	// the higher the more urgent it is to fill the battery of this inverter
	int drain_priority;	// the higher the more urgent it is to drain the battery of this inverter
};

constexpr inline float max_exp_pow_avail(const InverterGroup &ig, const ControlPowerInfo &pi) {
	float bat_avail = pi.soc > pi.min_soc ? pi.power_max_discha: 0;
	return std::min(ig.pv.exp_w + bat_avail, pi.power_max);
}
constexpr inline float max_imp_pow_avail(const InverterGroup &ig, const ControlPowerInfo &pi) {
	return pi.soc >= 100 ? 0: std::max(.0f, pi.power_max_cha - ig.pv.exp_w);
}

constexpr int HOME_ID = 0;
constexpr int GRID_ID = 1;
constexpr int METER_ID = 2;
inline int get_next_device_id() {static int cur_id{METER_ID}; return ++cur_id;}

