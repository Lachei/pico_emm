#pragma once

#include <span>

#include "emm_structs.h"

constexpr std::string_view CONNECTING{"Vebinde..."}; // default name given to requested discovers which can be used to identify not yet connected inverters
// used to retrieve and set power information for all inverters
struct inverter_infos {
    // general inverter information
    static_vector<ModbusTcpAddr, 32> *configured_inverters{}; // has to be set on startup, usually are the conffigured inverters from the settings
    static_vector<static_string<32>, 32> connected_names{}; // if empty the inverter is not configured

    static_vector<InverterGroup, 32> read_power;    // reported current power values
    static_vector<ControlPowerInfo, 32> control_infos;   // except soc of course, which is also a read quantity

    // only does discovery of new inverters and checks for sunspec conformity. Inverters getting lost are handled in
    // retrieve_infos
    void initiate_discover_inverters(static_vector<ModbusTcpAddr, 32> *ivs);
    void initiate_retrieve_infos_all();
    void initiate_send_power_requests_all();
    // can be used to wait for initiated requests for both, retrieving and sending
    void wait_all(uint32_t timeout_ms);
};

inline inverter_infos& inverters() {
    static inverter_infos i{};
    return i;
}

