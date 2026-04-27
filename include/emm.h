#pragma once
#include "emm_structs.h"
#include "settings.h"

struct InverterPower {
	int device_id;
	int requested_w; // sign is relevant: negative power means inverter takes power from grid, positive power means export to grid
};

struct EMM {
	float filter_alpha{.1f}; // fraction of history power used to filter the incoming home_power (0 is using only home power, 1 is only using history home power)
	float home_power{}; // this is the value that is approximated. Positive means power is consumed
	static_vector<InverterPower, 32> inverter_target_power{};
	bool invert_home{};

	// update the control infos of all inverters with a new home power usage
	// home_new should be given as positive for power consumed from home, negative for power gotten from home
	void update_power(float home_new, std::span<InverterGroup> inverter_powers, std::span<ControlPowerInfo> inverter_control_values, const settings &s);
};

inline EMM& emm() {
	static EMM e{};
	return e;
}
