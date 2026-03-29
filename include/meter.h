#pragma once

#include "emm_structs.h"

// represents sunspec meter
// has internally (in compile unit) an additional storage space for full modbus registers to read from meter
struct meter_info {
	ModbusTcpAddr addr{};
	static_string<32> name{NOT_CONNECTED}; // check for equality with NOT_CONNECTED and CONNECTING to get the current status
	PowerInfo power_info{}; 	// used for external processing

	void initiate_discover(ModbusTcpAddr address);
	void initiate_retrieve_infos();	      // will do nothing if meter not yet found, will do minimal read out except every 10th iteration when a full information readout is done
	void wait_requests(uint32_t timeout_ms);  // wait for previously enqueued operations
};

namespace g {
inline meter_info& meter() {
	static meter_info m{};
	return m;
}
}

