#include "history_data.h"
#include "ranges_util.h"

namespace history_data {

static void add_data(t::device_data &locked_data, float value, time_t epoch_time_s);

void init() {
	{ g::meter_data.access().data = {}; }
	{ g::inverter_data.access().data = {}; }
	{ t::locked_data<std::array<t::id_data, MAX_INVERTERS * 2>> inverter = g::inverter_data.access(); 
	  for (t::id_data &i: inverter.data)
		i.device_id = -1;
	}
	{ t::locked_data<std::array<t::id_data, MAX_INVERTERS>> soc = g::soc_data.access(); 
	  for (t::id_data &i: soc.data)
		i.device_id = -1;
	}
	{ g::any_data.access().data = {}; }
}

void write_meter_data(float value, time_t epoch_time_s) {
	t::locked_data<t::device_data> locked_data = g::meter_data.access();
	add_data(locked_data.data, value, epoch_time_s);
}
void write_inverter_data(int id, float value, time_t epoch_time_s) {
	t::locked_data<std::array<t::id_data, MAX_INVERTERS * 2>> locked_data = g::inverter_data.access();
	t::id_data *inverter = locked_data.data | find{&t::id_data::device_id, id};
	if (!inverter) {
		inverter = locked_data.data | find{&t::id_data::device_id, -1};
		if (!inverter) // everything full
			return;
		inverter->device_id = id;
		inverter->data = {};
	}
	add_data(inverter->data, value, epoch_time_s);
}
void write_soc_data(int id, float value, time_t epoch_time_s) {
	t::locked_data<std::array<t::id_data, MAX_INVERTERS>> locked_data = g::soc_data.access();
	t::id_data *soc = locked_data.data | find{&t::id_data::device_id, id};
	if (!soc) {
		soc = locked_data.data | find{&t::id_data::device_id, -1};
		if (!soc) // everything full
			return;
		soc->device_id = id;
		soc->data = {};
	}
	add_data(soc->data, value, epoch_time_s);
}
void write_any_data(int i, float value, time_t epoch_time_s) {
	t::locked_data<static_vector<t::device_data, 4>> locked_data = g::any_data.access();
	add_data(locked_data.data[i], value, epoch_time_s);
}

// -------------------------------------------------------------------------------------------
// Internal implementation functions
// -------------------------------------------------------------------------------------------

static void add_data(t::device_data &locked_data, float value, time_t epoch_time_s) {
	bool has_data = locked_data.per_second.size();
	uint32_t prev_minute = locked_data.per_second[-1].time / 60;
	std::optional<t::data_time> new_per_minute{};
	if (has_data && prev_minute != epoch_time_s / 60) { // new minute started
		// calculate minute average of previous minute
		float sum{};
		int n{};
		for (int i: range(60)) {
			t::data_time dt = locked_data.per_second[-60 + i];
			if (dt.time / 60 == prev_minute) {
				sum += dt.data;
				n += 1;
			}
		}
		new_per_minute = {sum / n, prev_minute * 60};
	}

	has_data = locked_data.per_minute.size();
	uint32_t prev_hour = locked_data.per_second[-1].time / 3600;
	std::optional<t::data_time> new_per_hour{};
	if (has_data && prev_hour != epoch_time_s / 3600) {
		float sum{};
		int n{};
		for (int i: range(60)) {
			t::data_time dt = locked_data.per_minute[-60 + i];
			if (dt.time / 3600 == prev_hour) {
				sum += dt.data;
				n += 1;
			}
		}
		new_per_hour = {sum / n, prev_hour * 3600};
	}

	locked_data.per_second.push({value, epoch_time_s});
	if (new_per_minute)
		locked_data.per_minute.push(new_per_minute.value());
	if (new_per_hour)
		locked_data.per_hour.push(new_per_hour.value());
}

} // namespace hd

