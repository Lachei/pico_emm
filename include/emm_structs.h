#pragma once

#include "static_types.h"

struct ModbusTcpAddr {
    uint32_t ip;
    uint16_t port;
    uint8_t modbus_id;

    bool operator<=>(const ModbusTcpAddr &o) const = default;
};

struct PowerInfo {
	int device_id;
	float imp_w;
	float exp_w;
};

struct InverterGroup {
	PowerInfo inverter, pv, battery;
};

struct ControlPowerInfo {
    float soc;		    // soc of the battery
    float max_power;	    // maximum power the inverter can give 
    float requested_power;  // requested power by the emm
    int fill_priroty;	    // the higher the more urgent it is to fill the battery of this inverter
    int drain_priority;	    // the higher the more urgent it is to drain the battery of this inverter
};

