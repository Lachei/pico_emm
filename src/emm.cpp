#include "emm.h"
#include "ranges_util.h"
#include <cmath>

static static_vector<uint8_t, MAX_INVERTERS> full_inverter;
static static_vector<uint8_t, MAX_INVERTERS> fillable_inverter;
static static_vector<uint8_t, MAX_INVERTERS> fill_inverter;
static static_vector<uint8_t, MAX_INVERTERS> fillable_full_inverter;

void EMM::update_power(float home_new, std::span<InverterGroup> inverter_powers, std::span<ControlPowerInfo> inverter_control_values, const settings &s) {
	full_inverter.clear();
	fillable_inverter.clear();
	fill_inverter.clear();
	fillable_full_inverter.clear();
	// update approximated power
	if (invert_home)
		home_new = -home_new;
	home_power = std::lerp(home_new, home_power, filter_alpha);
	float needed_power = home_power;
	// collect inverter extra power they can take up
	// eg. battery is not full and their pv does not supply full power
	// this is needed to avoid toggeling all inverters even though 1 inverter still needs power for filling up a battery
	float inverter_fillable_power{};
	float priority_sum{};
	for (int i: range(inverter_powers.size())) {
		float imp_avail = max_imp_pow_avail(inverter_powers[i], inverter_control_values[i]);
		inverter_fillable_power += imp_avail;
		// inverters which are below the min_soc are force charged with full power -> add to home_power (which )
		if (requires_charge(inverter_powers[i], inverter_control_values[i])) {
			needed_power += imp_avail;
			inverter_control_values[i].requested_power = -inverter_control_values[i].power_max_cha;
			fill_inverter.push(i);
		}
		else if (inverter_powers[i].bat_soc < 98) { // only inverters which do not need to get charged can be required to export
			fillable_inverter.push(i);
			fillable_full_inverter.push(i);
			priority_sum += 1.f / inverter_control_values[i].bat_priority;
		} else {
			full_inverter.push(i);
			fillable_full_inverter.push(i);
			priority_sum += 1.f / inverter_control_values[i].bat_priority;
		}
	}

	// trying to distribute the needed power to all inverters according to the bat priorities
	float remaining_power{needed_power};
	for (int i: fillable_full_inverter) {
		float prio = 1.f / (inverter_control_values[i].bat_priority * priority_sum);
		inverter_control_values[i].requested_power = std::min(inverter_control_values[i].power_max, prio * remaining_power);
		remaining_power -= inverter_powers[i].inverter.exp_w; // use the real export power to account for empty batteries
	}

	// distributing the rest of the needed power
	for (int i: fillable_full_inverter) {
		if (remaining_power <= 0)
			break;
		// if there is more power available, request more juice
		float power_avail = max_exp_pow_avail(inverter_powers[i], inverter_control_values[i]) - inverter_control_values[i].requested_power;
		power_avail = std::min(remaining_power, power_avail);
		if (power_avail > 0) {
			remaining_power -= power_avail;
			inverter_control_values[i].requested_power += power_avail;
		}
	}
	// distributing overpower from full inverters (simply settings the export to max pow of inverter with a power ramp from 98 to 99 soc)
	float remaining_export = s.max_export;
	for (int i: full_inverter) {
		float max = inverter_control_values[i].power_max;
		float export_power = std::clamp(std::lerp(0.f, max, inverter_powers[i].bat_soc - 98.f), 0.f, max);
		if (export_power < 0)
			continue;
		// distribute the available power evenly accross other importable inverter
		if (export_power > 0) {
			float p = std::min(export_power, inverter_fillable_power);
      			export_power -= p;
			for (int j: fillable_inverter)
				inverter_control_values[j].requested_power -= p / fillable_inverter.size();
			inverter_control_values[i].requested_power += p;
		}
		// export rest to grid
		if (export_power > 0) {
			float rem = std::min(export_power, remaining_export);
			inverter_control_values[i].requested_power += rem;
			remaining_export -= rem;
		}
	}
}

