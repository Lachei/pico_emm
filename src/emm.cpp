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
	float inverter_fillable_power{}; // power the fillable (not full, not force charged) inverters can take up
	for (int i: range(inverter_powers.size())) {
		// inverters that are not reachable can neither take up nor deliver power
		if (!inverter_control_values[i].is_active()) {
			inverter_control_values[i].requested_power = 0;
			continue;
		}
		if (inverter_control_values[i].bat_priority == 0)
			inverter_control_values[i].bat_priority = 1;
		float imp_avail = max_imp_pow_avail(inverter_powers[i], inverter_control_values[i]);
		// inverters which are below the min_soc are force charged with full power -> add to home_power (which )
		if (requires_charge(inverter_powers[i], inverter_control_values[i])) {
			needed_power += imp_avail;
			// the battery charges with full power, pv covers part of it, the rest is taken from the ac side
			inverter_control_values[i].requested_power = -imp_avail;
			fill_inverter.push(i);
		}
		else if (inverter_powers[i].bat_soc < 98) { // only inverters which do not need to get charged can be required to export
			inverter_fillable_power += imp_avail;
			fillable_inverter.push(i);
			fillable_full_inverter.push(i);
		} else {
			full_inverter.push(i);
			fillable_full_inverter.push(i);
		}
	}

	// share weights to even out the socs: every pair of inverters splits its part linearly by the soc difference,
	// equal socs split 50/50, a difference of soc_balance_range or more gives everything to one of them.
	// When power is delivered the fuller battery takes more, when a surplus is taken up the emptier one.
	// The battery priority additionally shifts the split towards keeping higher priority batteries fuller.
	const bool deliver = needed_power >= 0;
	static std::array<float, MAX_INVERTERS> share_weight{};
	float weight_sum{};
	for (int i: fillable_full_inverter) {
		float w = fillable_full_inverter.size() == 1 ? 1.f: 0.f;
		for (int j: fillable_full_inverter) {
			if (j == i)
				continue;
			float soc_diff = inverter_powers[i].bat_soc - inverter_powers[j].bat_soc;
			if (!deliver)
				soc_diff = -soc_diff;
			w += std::clamp(.5f + soc_diff / (2.f * soc_balance_range), 0.f, 1.f);
		}
		float prio = inverter_control_values[i].bat_priority;
		share_weight[i] = deliver ? w / prio: w * prio;
		weight_sum += share_weight[i];
	}

	// trying to distribute the needed power to all inverters according to the share weights.
	// Each share is limited to what the inverter can actually deliver (pv + battery discharge, power_max) or take up,
	// the part that could not be covered is distributed to the other inverters below
	float covered_power{};
	for (int i: fillable_full_inverter) {
		if (inverter_control_values[i].power_max == 0)
			inverter_control_values[i].power_max = 10000;
		float share = weight_sum > 0 ? needed_power * share_weight[i] / weight_sum: 0.f;
		if (share >= 0)
			share = std::min(share, max_exp_pow_avail(inverter_powers[i], inverter_control_values[i]));
		else
			share = std::max(share, -max_imp_pow_avail(inverter_powers[i], inverter_control_values[i]));
		inverter_control_values[i].requested_power = share;
		covered_power += share;
	}
	float remaining_power = needed_power - covered_power;

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
	// distributing the rest of a surplus (negative needed power) to inverters which can still take up power
	for (int i: fillable_full_inverter) {
		if (remaining_power >= 0)
			break;
		float take_avail = max_imp_pow_avail(inverter_powers[i], inverter_control_values[i]) + inverter_control_values[i].requested_power;
		take_avail = std::min(-remaining_power, take_avail);
		if (take_avail > 0) {
			remaining_power += take_avail;
			inverter_control_values[i].requested_power -= take_avail;
		}
	}
	// distributing overpower from full inverters (simply settings the export to max pow of inverter with a power ramp from 98 to 99 soc)
	// a surplus that could not be taken up already goes to the grid and uses up the allowed export
	float remaining_export = std::max(s.max_export + std::min(remaining_power, 0.f), 0.f);
	// the capacity of the fillable inverters which is not yet used by the distribution above
	// (an inverter that already takes up a surplus has less room left, one that exports has more)
	static std::array<float, MAX_INVERTERS> fill_capacity{};
	inverter_fillable_power = 0;
	for (int j: fillable_inverter) {
		fill_capacity[j] = std::max(max_imp_pow_avail(inverter_powers[j], inverter_control_values[j]) + inverter_control_values[j].requested_power, 0.f);
		inverter_fillable_power += fill_capacity[j];
	}
	const float fillable_power_total = inverter_fillable_power;
	for (int i: full_inverter) {
		float max = inverter_control_values[i].power_max;
		// the inverter can only deliver up to its limits, part of which is already requested for the home
		float headroom = std::max(max_exp_pow_avail(inverter_powers[i], inverter_control_values[i]) - inverter_control_values[i].requested_power, 0.f);
		float export_power = std::min(std::clamp(std::lerp(0.f, max, inverter_powers[i].bat_soc - 98.f), 0.f, max), headroom);
		// distribute the available power accross the other importable inverters according to how much they can take up
		if (export_power > 0 && inverter_fillable_power > 0) {
			float p = std::min(export_power, inverter_fillable_power);
			for (int j: fillable_inverter)
				inverter_control_values[j].requested_power -= p * fill_capacity[j] / fillable_power_total;
			export_power -= p;
			inverter_fillable_power -= p; // the capacity is used up for the next full inverter
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

