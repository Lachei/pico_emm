#pragma once

#include "AppConfig.h"
#include "static_types.h"
#include "psram.h"
#include "mutex.h"

namespace t {
struct data_time {float data; uint32_t time;}; // note that time is alwasys in seconds
using per_second = static_ring_buffer<data_time, 3600 * 2>; // last 2 hours are available as per_second
using per_minute = static_ring_buffer<data_time, 60 * 24 * 7>; // last 2 days are available as per minute
using per_hour = static_ring_buffer<data_time, 24 * 366 * 2>; // last 4 years are available as per hour;

template<typename T>
struct locked_data {
	T& data;
	scoped_lock lock;
};
template<typename T>
struct thread_safe {
	T& data;
	mutex m{};
	locked_data<T> access() { return locked_data<T>{data, scoped_lock{m}}; }
};

struct device_data {
	t::per_second per_second;
	t::per_minute per_minute;
	t::per_hour per_hour;
};
struct id_data { 
	int device_id;
	t::device_data data;
};

}

namespace g {
/*
 * @brief Global history data, all located in psram
 *
 * to accesss the data (both reading or writing) simply choose your field and do eg.
 * int16_t value = meter_data.access().data.daily[-1][-1]; // this is essentially the last written value
 */
// storage
inline t::device_data meter_data_psram PSRAM;
inline std::array<t::id_data, MAX_INVERTERS * 2> inverter_data_psram PSRAM;
inline std::array<t::id_data, MAX_INVERTERS> soc_data_psram PSRAM;
inline static_vector<t::device_data, 4> any_data_psram PSRAM;
// access
inline t::thread_safe<t::device_data> meter_data{meter_data_psram};
inline t::thread_safe<std::array<t::id_data, MAX_INVERTERS * 2>> inverter_data{inverter_data_psram};
inline t::thread_safe<std::array<t::id_data, MAX_INVERTERS>> soc_data{soc_data_psram};
inline t::thread_safe<static_vector<t::device_data, 4>> any_data{any_data_psram};
}

namespace history_data {
void init();
void write_meter_data(float value, time_t epoch_hours);
void write_inverter_data(int id, float value, time_t epoch_hours);
void write_soc_data(int id, float value, time_t epoch_hours);
void write_any_data(int i, float value, time_t epoch_hours);
}
namespace hd = history_data;

