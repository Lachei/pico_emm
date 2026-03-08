#include "inverter.h"
#include "modbus-register.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "inverter_sunspec.h"

#include "pico/stdlib.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/pbuf.h>
#include <lwip/tcp.h>

#include <bitset>

#define CHECK_INVERTER_CONFIGURED if (!configured_inverters) {LogError("Configured Inverters not set"); return;} parent_task = xTaskGetCurrentTaskHandle()
#define ASSERT_OK_RETURN(status) {std::string_view s = status; if ((s) != OK) {LogError(s); return;}}
#define ASSERT_BREAK_CONTEXT(cond, message) if (!(cond)) {LogError(message); context.state = pcb_state::IDLE; context.request_close = true; break;}

using namespace libmodbus_static;
constexpr uint32_t time_ms() { return time_us_64() / 1000; }
constexpr uint32_t time_s() { return time_us_64() / 1000000; }

enum class pcb_state {
	IDLE, CONNECTING, CHECK_SUNS, FIND_COMMON_HDR, GET_COMMON_INFOS, FIND_DATA_HDR, START_DATA_FETCH,
	GET_INVERTER_INFOS, GET_NAMEPLATE_INFOS, GET_SETTINGS_INFOS, GET_STATUS_INFOS,
	GET_CONTROLS_INFOS, GET_STORAGE_INFOS, ENABLE_INVERTER_CONTROL, ENABLE_STORAGE_CONTROL, WAIT_DATA_RESPONSE, SET_POWER};
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
	T* get_addr_as(int addr) {
		if (addr < generic_halfs_registers::OFFSET || addr + suns_sizeof(T{}) > generic_halfs_registers::OFFSET + halfs_registers.data.size())
			return {};
		return (T*)(halfs_registers.data.data() + (addr - generic_halfs_registers::OFFSET));
	};
};
constexpr static int NAMEPLATE_REFETCH_S{10 * 60};
constexpr static int SETTINGS_REFETCH_S{5 * 60};
constexpr static int STATUS_REFETCH_S{1 * 60};
constexpr static int STORAGE_REFETCH_S{30};
// internal tcp context
struct context_t {
	struct tcp_pcb* pcb{};
	bool connected{};
	bool request_close{}; // used to request close after next received package
	pcb_state state{pcb_state::IDLE};
	int next_hdr_addr{-1};
	int common_addr{-1}; // only fetched once after new discovery, for reread reboot
	uint32_t common_fetched_s{};
	int inverter_addr{-1}; // always fetch, needed for current power value (should be every second)
	int nameplate_addr{-1};
	uint32_t nameplate_fetched_s{}; // refetched only once every 10 minutes, should never change
	int settings_addr{-1};
	uint32_t settings_fetched_s{}; // refetched only once every 5 minutes, stays mostly the same
	int status_addr{-1};
	uint32_t status_fetched_s{}; // refetched only once every minute, stays mostly the same
	int controls_addr{-1};
	uint32_t controls_fetched_s{}; // refetched only once, afterwards is only written
	int storage_addr{-1};
	uint32_t storage_fetched_s{}; // refetched all 30 s for up to date 
	uint16_t tcp_frame{1};
	modbus_register<generic_modbus_layout> modbus{.addr = 0}; // client always has addr 0
	int last_modbus_addr{-1};
};
static static_vector<context_t, 32> contexts{};
static TaskHandle_t parent_task{};

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
		if (!contexts[i].connected) {
			contexts[i].state = pcb_state::CONNECTING;
			tcp_connect(contexts[i].pcb, &ip, configured_inverters[0][i].port, tcp_connect_cb);
		}
	}
}
void inverter_infos::initiate_retrieve_infos_all() {
	CHECK_INVERTER_CONFIGURED;
	for (int i: range(configured_inverters->size())) {
		if (connected_names[i].empty() || !contexts[i].connected)
			continue;
		if (contexts[i].state != pcb_state::IDLE) {
			LogError("Could not start info retrieval for {}: {}", connected_names[i].sv(), int(contexts[i].state));
			continue;
		}
		contexts[i].state = pcb_state::START_DATA_FETCH;
	}
}
void inverter_infos::initiate_send_power_requests_all() {
	CHECK_INVERTER_CONFIGURED;
	for (int i: range(configured_inverters->size())) {
		if (connected_names[i].empty() || !contexts[i].connected)
			continue;
		if (contexts[i].state != pcb_state::IDLE) {
			LogError("Could not send power for {}: {}", connected_names[i].sv(), int(contexts[i].state));
			continue;
		}
		// request_modbus_registers_write(contexts[i], , int register_count);
		// contexts[i].state = pcb_state::SET_POWER;
	}
}
void inverter_infos::wait_all(uint32_t timeout_ms) {
	CHECK_INVERTER_CONFIGURED;
	uint32_t end_ms = time_ms() + timeout_ms;
	for (int i: range(contexts.size()))
		if (contexts[i].connected)
			ulTaskNotifyTakeIndexed(i, pdTRUE, 
			  pdMS_TO_TICKS(std::max(0, int(end_ms) - int(time_ms()))));
	for (int i: range(contexts.size())) {
		if (contexts[i].request_close) {
			tcp_pcb_close(contexts[i].pcb); // only close the pcb, dont reorder
			contexts[i].connected = false;
			connected_names[i] = {}; // resetting name signals disconnected inverter
		}
	}
}

// private implementations

// modbus logic functions ------------------------------------------------------------------------------
void request_modbus_registers(context_t &context, int offset, int registers);
void request_modbus_registers_write(context_t &context, int offset, int registers);
void parse_modbus_frame(context_t &context, struct pbuf *&p);
void advance_context_state(context_t &context, struct pbuf *p = {}) {
	int i = &context - contexts.begin();
	pcb_state prev_state = context.state;
	uint32_t s = time_s();
	switch(context.state) {
		case pcb_state::IDLE:
			break;
		// Connection setup cases ---------------------------------------------------------
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
				case model_nameplate::ID:	context.nameplate_addr = context.last_modbus_addr; break;
				case model_settings::ID:	context.settings_addr = context.last_modbus_addr; break;
				case model_status::ID:		context.status_addr = context.last_modbus_addr; break;
				case model_controls::ID:	context.controls_addr = context.last_modbus_addr; break;
				case model_storage::ID:		context.storage_addr = context.last_modbus_addr; break;
				case model_end::ID: 		context.state = pcb_state::ENABLE_STORAGE_CONTROL;
				default:;
			}
			if (context.state != pcb_state::ENABLE_STORAGE_CONTROL) { // continue searching
				context.next_hdr_addr = context.last_modbus_addr + SUNSPEC_HDR_SIZE + hdr->length;
				request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
			}
			break;
		}
		case pcb_state::ENABLE_STORAGE_CONTROL: {
			LogInfo("Enable storage control");
			if (context.storage_addr == -1) {
				LogError("Missing storage control header, removing inverter");
				context.request_close = true;
				break;
			}
			model_storage *storage = context.modbus.storage.get_addr_as<model_storage>(context.storage_addr);
			storage->StorCtl_Mod = modbus_swap(1 | 2); //  Bit0 enable charge power override, Bit1 enable discharge override
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::StorCtl_Mod), suns_sizeof<decltype(model_storage::StorCtl_Mod)>());
			context.state = pcb_state::ENABLE_INVERTER_CONTROL;
			break;
		}
		case pcb_state::ENABLE_INVERTER_CONTROL: {
			LogInfo("Enable inverter control");
			if (context.controls_addr == -1) {
				LogError("Missing inverter control header, removing inverter");
				context.request_close = true;
				break;
			}
			model_controls *controls = context.modbus.storage.get_addr_as<model_controls>(context.controls_addr);
			controls->WMaxLim_Ena = modbus_swap(1);
			request_modbus_registers_write(context, context.controls_addr + suns_offsetof(&model_controls::WMaxLim_Ena), suns_sizeof<decltype(model_controls::WMaxLim_Ena)>());
			context.state = pcb_state::WAIT_DATA_RESPONSE;
			break;
		}
		// Data fetching -----------------------------------------------------------------------
		case pcb_state::START_DATA_FETCH:
			LogInfo("Starting to fetch data");
			context.state = pcb_state::GET_INVERTER_INFOS;
			[[fallthrough]]; // directly execute get common
		case pcb_state::GET_INVERTER_INFOS:
			ASSERT_BREAK_CONTEXT(context.inverter_addr == -1, "Inverter address unknown");
			request_modbus_registers(context, context.inverter_addr, suns_sizeof(model_inverter{}));
			context.state = pcb_state::GET_NAMEPLATE_INFOS;
			break;
			// inverter infos have to be read always, so no fallthrough possible
		case pcb_state::GET_NAMEPLATE_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.nameplate_addr == -1, "Nameplate address unknown");
			if (s - context.nameplate_fetched_s >= NAMEPLATE_REFETCH_S) {
				context.nameplate_fetched_s = s;
				request_modbus_registers(context, context.nameplate_addr, suns_sizeof(model_nameplate{}));
				context.state = pcb_state::GET_SETTINGS_INFOS;
				break;
			}
			[[fallthrough]]; // if nameplate is not fetched just continue
		case pcb_state::GET_SETTINGS_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.settings_addr == -1, "Settings address unknown");
			if (s - context.settings_fetched_s >= SETTINGS_REFETCH_S) {
				context.settings_fetched_s = s;
				request_modbus_registers(context, context.settings_addr, suns_sizeof(model_settings{}));
				context.state = pcb_state::GET_STATUS_INFOS;
				break;
			}
			[[fallthrough]]; // if settings is not fetched fetch next
		case pcb_state::GET_STATUS_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.status_addr == -1, "Status address unknown");
			if (s - context.status_fetched_s >= STATUS_REFETCH_S) {
				context.status_fetched_s = s;
				request_modbus_registers(context, context.settings_addr, suns_sizeof(model_settings{}));
				context.state = pcb_state::GET_CONTROLS_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_CONTROLS_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.controls_addr == -1, "Controls address unknown");
			if (context.controls_fetched_s == 0) {
				context.controls_fetched_s = s;
				request_modbus_registers(context, context.controls_addr, suns_sizeof(model_controls{}));
				context.state = pcb_state::GET_STORAGE_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_STORAGE_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.storage_addr == -1, "Storage address unknown");
			if (s - context.storage_fetched_s >= STORAGE_REFETCH_S) {
				context.storage_fetched_s = s;
				request_modbus_registers(context, context.storage_addr, suns_sizeof(model_storage{}));
				context.state = pcb_state::WAIT_DATA_RESPONSE;
				break;
			}
			[[fallthrough]];
		case pcb_state::WAIT_DATA_RESPONSE:
			if (p)
				parse_modbus_frame(context, p);
			context.state = pcb_state::IDLE;
			break;
	}
	// get any missing information if there are
	if (context.state == pcb_state::IDLE && prev_state != pcb_state::IDLE) // wakeup main task
		xTaskNotifyGiveIndexed(parent_task, i);
}
void request_modbus_registers(context_t &context, int offset, int register_count) {
	context.modbus.switch_to_request();
	context.last_modbus_addr = offset;
	ASSERT_OK_RETURN(context.modbus.start_tcp_frame(context.tcp_frame++, context.modbus.addr));
	auto [res, err] = context.modbus.get_frame_read(libmodbus_static::register_t::HALFS, offset, register_count);
	ASSERT_OK_RETURN(err);
	err_t error = tcp_write(context.pcb, res.data(), res.size(), TCP_WRITE_FLAG_COPY);
	if (error != ERR_OK)
		LogError("Error sending modbus read frame {}", error);
}
void request_modbus_registers_write(context_t &context, int offset, int register_count) {
	context.modbus.switch_to_request();
	context.last_modbus_addr = offset;
	uint16_t* data_start = context.modbus.storage.halfs_registers.data.data() + offset - generic_halfs_registers::OFFSET;
	std::span<uint8_t> data{(uint8_t*)data_start, (uint8_t*)(data_start + register_count)};
	ASSERT_OK_RETURN(context.modbus.start_tcp_frame(context.tcp_frame++, context.modbus.addr));
	auto [res, err] = context.modbus.get_frame_write(libmodbus_static::register_t::HALFS, offset, data);
	ASSERT_OK_RETURN(err);
	err_t error = tcp_write(context.pcb, res.data(), res.size(), TCP_WRITE_FLAG_COPY);
	if (error != ERR_OK)
		LogError("Error sending modbus write frame {}", error);
}
void parse_modbus_frame(context_t &context, struct pbuf *&p) {
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
	p = nullptr;
	// updating the inverter informations
	int i = &context - contexts.begin();
	if (context.last_modbus_addr == context.inverter_addr) {
		const model_inverter *inverter = context.modbus.storage.get_addr_as<model_inverter>(context.inverter_addr);
		float w = modbus_swap_f(inverter->W);
		inverters().read_power[i].inverter.imp_w = inverters().read_power[i].inverter.exp_w = 0;
		if (w < 0)
			inverters().read_power[i].inverter.imp_w = -w;
		else
			inverters().read_power[i].inverter.exp_w = w;
	} else if (context.last_modbus_addr == context.nameplate_addr) {
		const model_nameplate *nameplate = context.modbus.storage.get_addr_as<model_nameplate>(context.nameplate_addr);
		float max_pow = to_float(modbus_swap(nameplate->WRtg), modbus_swap_i16(nameplate->WRtg_SF));
		float max_pow_bat_cha = to_float(modbus_swap(nameplate->MaxChaRte), modbus_swap_i16(nameplate->MaxChaRte_SF));
		float max_pow_bat_discha = to_float(modbus_swap(nameplate->MaxDisChaRte), modbus_swap_i16(nameplate->MaxDisChaRte_SF));
		ControlPowerInfo &pi = inverters().control_infos[i];
		pi.power_max = max_pow;
		pi.power_max_cha = max_pow_bat_cha;
		pi.power_max_discha = max_pow_bat_discha;
	} else if (context.last_modbus_addr == context.settings_addr) {
		const model_settings *settings = context.modbus.storage.get_addr_as<model_settings>(context.settings_addr);
		float max_w = to_float(modbus_swap(settings->WMax), modbus_swap_i16(settings->WMax_SF));
		inverters().control_infos[i].power_max = max_w;
	}
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
