#pragma once

struct EMM {
    float power_home{}; // this is the value that is approximated with a PI integration
    static_vector<PowerInfo, 32> requested_power_infos;
    // integrate a new smart meter value into the power_home
    void update_power_home(float smart_meter_power);

};

inline EMM& emm() {
    static EMM e{};
    return e;
}
