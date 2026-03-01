#include "meter.h"
#include "log_storage.h"

void* modbus_registers(void *self) {
    static void* s{self};
    if (self != s)
        LogWarning("Multiple meters have been created, it should only be used as a singleton!");
    return nullptr;
}

void meter_info::initiate_discover(ModbusTcpAddr address) {

}
void meter_info::initiate_retrieve_infos() {

}
void meter_info::wait_requests(uint32_t timeout_ms) {

}
