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
	bool enable_control{};
	float soc_balance_range{20.f}; // soc difference at which one inverter covers all of the power, linear split below

	// update the control infos of all inverters with a new home power usage
	// home_new should be given as positive for power consumed from home, negative for power gotten from home
	void update_power(float home_new, std::span<InverterGroup> inverter_powers, std::span<ControlPowerInfo> inverter_control_values, const settings &s);
	// takes over the persisted control settings
	void apply_settings(const settings &s) {
		enable_control = s.enable_emm;
		invert_home = s.invert_home;
		filter_alpha = s.filter_alpha;
	}
};

inline EMM& emm() {
	static EMM e{};
	return e;
}
