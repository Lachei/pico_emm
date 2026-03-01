#pragma once

struct EMM {
    float power_home{}; // this is the value that is approximated with a PI integration
    // integrate a new smart meter value into the power_home
    void update_power(const PowerInfo &smart_meter_power, std::span<InverterGroup> inverter_powers, std::span<ControlPowerInfo> inverter_control_values, uint32_t delta_ms);
};

inline EMM& emm() {
    static EMM e{};
    return e;
}
