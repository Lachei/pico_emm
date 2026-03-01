#pragma once
#include <static_types.h>

struct InverterPowerInfo {
    float pv_power;
    float bat_power;
    float exp_power;
    float imp_power;
    float soc;
};
struct InverterControl {
    // checks for sunspec id and connection possibility;
    bool inverter_available(uint32_t ip);
    void get_inverter_name(uint32_t ip, static_string<32> &name);
    void get_inverter_power(uint32_t ip, InverterPowerInfo &info);
    void set_inverter_power(uint32_t ip, const InverterPowerInfo &info);
};

inline InverterControl& inverter_control() {
    static InverterControl ic{};
    return ic;
}
