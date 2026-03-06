#include "inverter.h"
#include "modbus-register.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "inverter_sunspec.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/pbuf.h>
#include <lwip/tcp.h>

#include <bitset>

#define CHECK_INVERTER_CONFIGURED if (!configured_inverters) {LogError("Configured Inverters not set"); return;}
#define ASSERT_OK_RETURN(status) {std::string_view s = status; if ((s) != OK) {LogError(s); return;}}

using namespace libmodbus_static;

enum class pcb_state {IDLE, CONNECTING, CHECK_SUNS, FIND_COMMON_HDR, GET_COMMON_INFOS, FIND_DATA_HDR, GET_DATA_INFOS, WAIT_RESPONSE};
enum class request_type {NONE, SUNS, SUNS_HEADER, IMP_POWER, EXP_POWER, SOC, P_SET};
using sunspec_header = model_start;
template<typename T>
constexpr inline int suns_sizeof(const T& = {}) { return sizeof(T) / 2; }
template<typename T>
constexpr inline int suns_offsetof(T member) { return (int(*(uintptr_t*)(&member))) / 2; }
constexpr int SUNSPEC_HDR_SIZE = suns_sizeof(sunspec_header{});
static_assert(SUNSPEC_HDR_SIZE == 2);
struct suns_hdr{
	uint16_t id;
	uint16_t length;
};
struct generic_halfs_registers {
	constexpr static int OFFSET = 40000;
	std::array<uint16_t, sizeof(inverter_layout) + 100> data;
};
struct generic_modbus_layout {
	generic_halfs_registers halfs_registers{};

	template <typename T>
	const T* get_addr_as(int addr) const {
		if (addr < generic_halfs_registers::OFFSET || addr + suns_sizeof(T{}) > generic_halfs_registers::OFFSET + halfs_registers.data.size())
			return {};
		return (const T*)(halfs_registers.data.data() + (addr - generic_halfs_registers::OFFSET));
	};
};
// internal tcp context
struct context_t {
	struct tcp_pcb* pcb{};
	bool connected{};
	bool request_close{}; // used to request close after next received package
	pcb_state state{pcb_state::IDLE};
	int next_hdr_addr{-1};
	int common_addr{-1};
	int inverter_addr{-1};
	int status_addr{-1};
	int controls_addr{-1};
	int storage_addr{-1};
	uint16_t tcp_frame{1};
	modbus_register<generic_modbus_layout> modbus{.addr = 0}; // client always has addr 0
	int last_modbus_addr{-1};
};
static static_vector<context_t, 32> contexts{};

void init_pcb(context_t &context);
void tcp_err_cb(void *arg, err_t err);
err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
err_t tcp_pcb_close(tcp_pcb *pcb);

void inverter_infos::initiate_discover_inverters(static_vector<ModbusTcpAddr, 32> *ivs) {
	configured_inverters = ivs;
	CHECK_INVERTER_CONFIGURED;
	connected_names.resize(configured_inverters->size());
	for(int i: range(connected_names.size())) {
		if (connected_names[i].size())
			continue;
		if (!contexts[i].pcb)
			init_pcb(contexts[i]);
		if (!contexts[i].pcb)
			continue; // error if failed to crate will be already printed by init_pcb
		ip_addr_t ip{.addr = PP_HTONL(configured_inverters[0][i].ip)};
		if (!contexts[i].connected)
			tcp_connect(contexts[i].pcb, &ip, configured_inverters[0][i].port, tcp_connect_cb);
	}
}
void inverter_infos::initiate_retrieve_infos_all() {
	CHECK_INVERTER_CONFIGURED;
}
void inverter_infos::initiate_send_power_requests_all() {
	CHECK_INVERTER_CONFIGURED;
}
void inverter_infos::wait_all(uint32_t timeout_ms) {
	CHECK_INVERTER_CONFIGURED;
	// TODO: wait for all notifications
	for (int i: range(contexts.size()))
		if (contexts[i].request_close)
			tcp_pcb_close(contexts[i].pcb); // only close the pcb, dont reorder
}

// private implementations

// modbus logic functions ------------------------------------------------------------------------------
void request_modbus_registers(context_t &context, int offset, int registers);
void parse_modbus_frame(context_t &context, struct pbuf *p);
void advance_context_state(context_t &context, struct pbuf *p = {}) {
	int i = &context - contexts.begin();
	switch(context.state) {
		case pcb_state::IDLE:
			break;
		case pcb_state::CONNECTING:
			LogInfo("Trying to connect");
			context.connected = true;
			// check sunspec header
			request_modbus_registers(context, generic_halfs_registers::OFFSET, suns_sizeof(sunspec_header{}));
			context.state = pcb_state::CHECK_SUNS;
			break;
		case pcb_state::CHECK_SUNS: {
			LogInfo("Checking sunspec id");
			parse_modbus_frame(context, p);
			const string<4> *hdr = context.modbus.storage.get_addr_as<string<4>>(context.last_modbus_addr);
			if (!hdr || to_sv(*hdr) != "SunS") {
				LogError("Invalid sunspec inverter, removing");
				context.request_close = true;
				break;
			}
			// find common header
			context.state = pcb_state::FIND_COMMON_HDR;
			request_modbus_registers(context, context.last_modbus_addr + SUNSPEC_HDR_SIZE, SUNSPEC_HDR_SIZE);
			break;
		}
		case pcb_state::FIND_COMMON_HDR: {
			LogInfo("Searching common header");
			parse_modbus_frame(context, p);
			const suns_hdr *hdr = context.modbus.storage.get_addr_as<suns_hdr>(context.last_modbus_addr);
			if (!hdr) {
				LogError("Could not get header, overflow");
				context.request_close = true;
				break;
			}
			context.next_hdr_addr = context.last_modbus_addr + SUNSPEC_HDR_SIZE + hdr->length;
			if (hdr->id == model_common::ID) {
				context.common_addr = context.last_modbus_addr;
				request_modbus_registers(context, context.last_modbus_addr + suns_offsetof(&model_common::device_model), suns_sizeof<decltype(model_common::device_model)>());
				context.state = pcb_state::GET_COMMON_INFOS;
			} else 
				request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
			break;
		}
		case pcb_state::GET_COMMON_INFOS: {
			LogInfo("Reading common info");
			parse_modbus_frame(context, p);
			const model_common *common = context.modbus.storage.get_addr_as<model_common>(context.last_modbus_addr);
			if (!common) {
				LogError("Could not get model common registers");
				context.request_close = true;
				break;
			}
			inverters().connected_names[i].fill(to_sv(common->device_model));
			request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
			context.state = pcb_state::FIND_DATA_HDR;
			break;
		}
		case pcb_state::FIND_DATA_HDR: {
			LogInfo("Trying to find data headers");
			parse_modbus_frame(context, p);
			const suns_hdr *hdr = context.modbus.storage.get_addr_as<suns_hdr>(context.last_modbus_addr);
			if (!hdr) {
				LogError("Could not get header, overflow");
				context.request_close = true;
				break;
			}
			switch (hdr->id) {
				case model_inverter::ID: 	context.inverter_addr = context.last_modbus_addr; break;
				case model_status::ID:		context.status_addr = context.last_modbus_addr; break;
				case model_controls::ID:	context.controls_addr = context.last_modbus_addr; break;
				case model_storage::ID:		context.storage_addr = context.last_modbus_addr; break;
				case model_end::ID: 		context.state = pcb_state::IDLE;
				default:;
			}
			if (context.state != pcb_state::IDLE) { // continue searching
				context.next_hdr_addr = context.last_modbus_addr + SUNSPEC_HDR_SIZE + hdr->length;
				request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
			}
			break;
		}
		case pcb_state::WAIT_RESPONSE:
			break;
	}
	// get any missing information if there are
	
}
void request_modbus_registers(context_t &context, int offset, int register_count) {
	context.modbus.switch_to_request();
	context.last_modbus_addr= offset;
	ASSERT_OK_RETURN(context.modbus.start_tcp_frame(context.tcp_frame++, context.modbus.addr));
	auto [res, err] = context.modbus.get_frame_read(libmodbus_static::register_t::HALFS, offset, register_count);
	ASSERT_OK_RETURN(err);
	err_t error = tcp_write(context.pcb, res.data(), res.size(), TCP_WRITE_FLAG_COPY);
	if (error != ERR_OK)
		LogError("Error sending modbus frame {}", error);
}
void parse_modbus_frame(context_t &context, struct pbuf *p) {
	if (!p) {
		LogError("Cant parse modbus frame because its empty");
	}
	if (p->tot_len > 0) {
		context.modbus.switch_to_response();
		for (int i: range(p->tot_len)) {
			std::string_view r = context.modbus.process_tcp(pbuf_get_at(p, i)).err;
			if (r == IN_PROGRESS)
				continue;
			if (r != OK) {
				LogError("Modbus parsing failed with {}", r);
				continue;
			}
		}
	}
	pbuf_free(p);
}

// pcb handle functions --------------------------------------------------------------------------------
void init_pcb(context_t &context) {
	struct tcp_pcb* &pcb = context.pcb;
	pcb = tcp_new();
	if (!pcb) {
		LogError("Failed to create client_pcb");
		return;
	}

	tcp_arg(pcb, &context);
	tcp_err(pcb, tcp_err_cb);
	tcp_recv(pcb, tcp_recv_cb);
	tcp_sent(pcb, tcp_sent_cb);
}
err_t tcp_pcb_close(tcp_pcb *pcb) { 
	err_t err = ERR_OK;
	if (pcb) {
		tcp_arg(pcb, NULL);
		tcp_poll(pcb, NULL, 0);
		tcp_sent(pcb, NULL);
		tcp_recv(pcb, NULL);
		tcp_err(pcb, NULL);
		if (tcp_close(pcb) != ERR_OK) {
			LogError("Close failed on pcb, calling abort");
			tcp_abort(pcb);
			err = ERR_ABRT;
		}
	}; 
	return err;
}
err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
	context_t &self = (*(context_t*)arg);
	advance_context_state(self);
	return ERR_OK;
}
err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	context_t &self = (*(context_t*)arg);
	advance_context_state(self, p);
	return ERR_OK;
}
err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	return ERR_OK;
}
void tcp_err_cb(void *arg, err_t err) {
	context_t &self = (*(context_t*)arg);
	self.pcb = {};
	self.connected = false;
	self.state = pcb_state::IDLE;
}
