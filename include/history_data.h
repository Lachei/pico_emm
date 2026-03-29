#pragma once

#include "AppConfig.h"
#include "static_types.h"
#include "psram.h"
#include "mutex.h"

namespace t {
using daily = static_ring_buffer<static_vector<int16_t, 3600>, 24>;
using monthly = static_ring_buffer<static_vector<int16_t, 1024>, 31>;
using yearly = static_ring_buffer<static_vector<int16_t, 1024>, 48>; // 48 months of data max

struct device_data {
	uint32_t daily_start;	// start given in days since 1970
	t::daily daily;
	uint32_t monthly_start;	// start given in days since 1970
	t::monthly monthly;
	uint32_t yearly_start;	// start given in days since 1970
	t::yearly yearly;
};
}

namespace g {
t::device_data meter_data PSRAM;
static_vector<t::device_data, MAX_INVERTERS> inverter_data PSRAM;
static_vector<t::device_data, 32> any_data PSRAM;

mutex meter_mutex;
mutex inverter_mutex;
mutex any_mutex;
}

namespace hd {
void init();
void write_meter_data(int16_t value);
void write_inverter_data(int i, int16_t value);
void write_any_data(int i, int16_t value);
scoped_lock lock_meter_daata();
scoped_lock lock_inverter_data();
scoped_lock lock_any_data();
}

