#pragma once

#include <span>

#include "AppConfig.h"
#include "emm_structs.h"

// used to retrieve and set power information for all inverters
struct inverter_infos {
    // general inverter information
    static_vector<ModbusTcpAddr, MAX_INVERTERS> *configured_inverters{}; // is set by the first call to initiate_discover_inverters
    static_vector<static_string<MAX_INVERTERS>, MAX_INVERTERS> connected_names{}; // if empty the inverter is not configured

    static_vector<InverterGroup, MAX_INVERTERS> read_power;    // reported current power values
    static_vector<ControlPowerInfo, MAX_INVERTERS> control_infos;   // except soc of course, which is also a read quantity

    // only does discovery of new inverters and checks for sunspec conformity. Inverters getting lost are handled in
    // retrieve_infos
    void initiate_discover_inverters(static_vector<ModbusTcpAddr, MAX_INVERTERS> *ivs);
    void initiate_retrieve_infos_all();
    void initiate_send_power_requests_all();
    // can be used to wait for initiated requests for both, retrieving and sending
    void wait_all(uint32_t timeout_ms);
};

namespace g {
inline inverter_infos& inverters() {
    static inverter_infos i{};
    return i;
}
}

