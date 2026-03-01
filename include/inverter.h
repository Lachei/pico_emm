#pragma once

#include <span>

#include "emm_structs.h"

constexpr std::string_view CONNECTING{"Vebinde..."}; // default name given to requested discovers which can be used to identify not yet connected inverters
// used to retrieve and set power information for all inverters
struct inverter_infos {
    // general inverter information
    static_vector<ModbusTcpAddr, 32> connected_addrs{};
    static_vector<static_string<32>, 32> connected_names{};

    static_vector<InverterGroup, 32> read_power;    // reported current power values
    static_vector<ControlPowerInfo, 32> control_infos;   // except soc of course, which is also a read quantity

    void initiate_discover_inverters(std::span<ModbusTcpAddr> addresses);
    void initiate_retrieve_infos_all();
    void initiate_send_power_requests_all();
    // can be used to wait for initiated requests for both, retrieving and sending
    void wait_all(uint32_t timeout_ms);
};

inline inverter_infos& inverters() {
    static inverter_infos i{};
    return i;
}

