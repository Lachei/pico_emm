#include "inverter.h"
#include "modbus-register.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "inverter_sunspec.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

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
	GET_CONTROLS_INFOS, GET_MPPT_INFOS, GET_STORAGE_INFOS, ENABLE_INVERTER_CONTROL, ENABLE_STORAGE_CONTROL, 
	WAIT_DATA_RESPONSE, SET_POWER_INVERTER, SET_POWER_STORAGE, SET_MIN_SOC_STORAGE};
enum class request_type {NONE, SUNS, SUNS_HEADER, IMP_POWER, EXP_POWER, SOC, P_SET};
struct generic_halfs_registers {
	constexpr static int OFFSET = 40000;
	std::array<uint16_t, suns_sizeof(inverter_layout{}) + 100> data;
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
	bool wait_receive{};
	bool request_close{}; // used to request close after next received package
	pcb_state state{pcb_state::IDLE};
	int wait_count{};
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
	int mppt_addr{-1}; // mptt has variable length, so length is required
	int mppt_length{-1};
	int controls_addr{-1};
	uint32_t controls_fetched_s{}; // refetched only once, afterwards is only written
	int storage_addr{-1};
	uint32_t storage_fetched_s{}; // refetched all 30 s for up to date 
	uint16_t tcp_frame{1};
	modbus_register<generic_modbus_layout> modbus{.addr = 0}; // client always has addr 1
	int last_modbus_addr{-1};
};
static static_vector<context_t, MAX_INVERTERS> contexts{};
static TaskHandle_t parent_task{};

static void init_pcb(context_t &context);
static void tcp_err_cb(void *arg, err_t err);
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t tcp_pcb_close(tcp_pcb *pcb);
static void advance_context_state(context_t &context, struct pbuf *p = {});

void inverter_infos::initiate_discover_inverters(static_vector<ModbusTcpAddr, MAX_INVERTERS> *ivs) {
	configured_inverters = ivs;
	CHECK_INVERTER_CONFIGURED;
	connected_names.resize(configured_inverters->size());
	read_power.resize(configured_inverters->size());
	control_infos.resize(configured_inverters->size());
	contexts.resize(configured_inverters->size());
	for(int i: range(connected_names.size())) {
		if (read_power[i].inverter.device_id == 0)
			read_power[i].inverter.device_id = get_next_device_id();
		if (!contexts[i].pcb) {
			cyw43_arch_lwip_begin();
			init_pcb(contexts[i]);
			cyw43_arch_lwip_end();
		}
		if (!contexts[i].pcb)
			continue; // error if failed to crate will be already printed by init_pcb
		ip_addr_t ip{.addr = PP_HTONL(configured_inverters[0][i].ip)};
		if (!contexts[i].connected) {
			LogInfo("Tcp connect inverter");
			contexts[i].state = pcb_state::CONNECTING;
			contexts[i].wait_receive = true;
			cyw43_arch_lwip_begin();
			tcp_connect(contexts[i].pcb, &ip, configured_inverters[0][i].port, tcp_connect_cb);
			cyw43_arch_lwip_end();
		}
	}
}
void inverter_infos::initiate_retrieve_infos_all() {
	CHECK_INVERTER_CONFIGURED;
	// resetting all wait infos
	for (int i: range(contexts.size()))
		ulTaskNotifyTakeIndexed(i, pdTRUE, 0);
	for (int i: range(configured_inverters->size())) {
		if (connected_names[i].empty() || !contexts[i].connected)
			continue;
		if (contexts[i].state != pcb_state::IDLE) {
			LogError("Start info failed {}: {}, retry {}", connected_names[i].sv(), int(contexts[i].state), contexts[i].wait_count);
			continue;
		}
		contexts[i].wait_receive = true;
		contexts[i].state = pcb_state::START_DATA_FETCH;
		cyw43_arch_lwip_begin();
		advance_context_state(contexts[i]);
		cyw43_arch_lwip_end();
	}
}
void inverter_infos::initiate_send_power_requests_all() {
	CHECK_INVERTER_CONFIGURED;
	for (int i: range(configured_inverters->size())) {
		if (connected_names[i].empty() || !contexts[i].connected)
			continue;
		if (contexts[i].state != pcb_state::IDLE) {
			LogError("Could not send power for {}: {}, retry {}", connected_names[i].sv(), int(contexts[i].state), contexts[i].wait_count);
			continue;
		}
		model_controls *control = contexts[i].modbus.storage.get_addr_as<model_controls>(contexts[i].controls_addr);
		model_storage *storage = contexts[i].modbus.storage.get_addr_as<model_storage>(contexts[i].storage_addr);
		// convert requested power to relative values
		float inv_power_r = std::max(.0f, control_infos[i].requested_power) / control_infos[i].power_max;
		control->WMaxLimPct = modbus_swap(from_float(inv_power_r, modbus_swap_i16(control->WMaxLimPct_SF)));
		bool charge = control_infos[i].requested_power < 0;
		float bat_min_soc = charge ? 100: 0;
		float bat_cha_r = charge ? 
					(-control_infos[i].requested_power + read_power[i].pv.exp_w) / control_infos[i].power_max_cha:
					100;
		// battery discharge rate always stays at 100%, discharging is controlled via max inverter power
		storage->MinRsvPct = modbus_swap(from_float(bat_min_soc, modbus_swap_i16(storage->MinRsvPct_SF)));
		storage->WChaMax = modbus_swap(from_float(bat_cha_r, modbus_swap_i16(storage->WChaMax_SF)));

		contexts[i].wait_receive = true;
		contexts[i].state = pcb_state::SET_POWER_INVERTER;
		cyw43_arch_lwip_begin();
		advance_context_state(contexts[i]);
		cyw43_arch_lwip_end();
	}
}
void inverter_infos::wait_all(uint32_t timeout_ms) {
	CHECK_INVERTER_CONFIGURED;
	uint32_t end_ms = time_ms() + timeout_ms;
	for (int i: range(contexts.size()))
		if (contexts[i].connected && contexts[i].wait_receive)
			ulTaskNotifyTakeIndexed(i, pdTRUE, pdMS_TO_TICKS(std::max(0, int(end_ms) - int(time_ms()))));
	for (int i: range(contexts.size())) {
		if (contexts[i].state == pcb_state::IDLE) {
			contexts[i].wait_count = 0;
			contexts[i].wait_receive = false;
		}
		bool timeout = ++contexts[i].wait_count > 3;
		if (timeout) {
			contexts[i].wait_count = 0;
			LogInfo("Wait expired, initiate reconnection");
		}
		if (contexts[i].request_close || timeout) {
			cyw43_arch_lwip_begin();
			tcp_pcb_close(contexts[i].pcb); // only close the pcb, dont reorder
			cyw43_arch_lwip_end();
			contexts[i].pcb = {};
			contexts[i].connected = false;
			contexts[i].request_close = false;
			if (!timeout)
				connected_names[i] = {}; // resetting name signals disconnected inverter
		}
	}
}

// private implementations

// modbus logic functions ------------------------------------------------------------------------------
static void request_modbus_registers(context_t &context, int offset, int registers);
static void request_modbus_registers_write(context_t &context, int offset, int registers);
static void parse_modbus_frame(context_t &context, struct pbuf *&p);
static void advance_context_state(context_t &context, struct pbuf *p) {
	int i = &context - contexts.begin();
	pcb_state prev_state = context.state;
	uint32_t s = time_s();
	switch(context.state) {
		case pcb_state::IDLE:
			context.wait_count = 0;
			break;
		// Connection setup cases ---------------------------------------------------------
		case pcb_state::CONNECTING:
			context.connected = true;
			if (inverters().connected_names[i].size()) {
				LogInfo("Connected to inverter, got base info already");
				context.state = pcb_state::IDLE;
				break;
			}
			LogInfo("Connected, requesting Suns register {}ms", time_ms());
			// check sunspec header
			request_modbus_registers(context, generic_halfs_registers::OFFSET, suns_sizeof(sunspec_header{}));
			context.state = pcb_state::CHECK_SUNS;
			break;
		case pcb_state::CHECK_SUNS: {
			LogInfo("Checking sunspec id");
			parse_modbus_frame(context, p);
			const string<4> *hdr = context.modbus.storage.get_addr_as<string<4>>(context.last_modbus_addr);
			LogInfo("Got id: {}, at addr {}", to_sv(*hdr), context.last_modbus_addr);
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
			context.next_hdr_addr = context.last_modbus_addr + SUNSPEC_HDR_SIZE + hdr->length();
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
			const string<32> *common = context.modbus.storage.get_addr_as<string<32>>(context.last_modbus_addr);
			if (!common) {
				LogError("Could not get model common registers");
				context.request_close = true;
				break;
			}
			LogInfo("Model name: {}", to_sv(*common));
			inverters().connected_names[i].fill(to_sv(*common));
			request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
			context.state = pcb_state::FIND_DATA_HDR;
			break;
		}
		case pcb_state::FIND_DATA_HDR: {
			parse_modbus_frame(context, p);
			const suns_hdr *hdr = context.modbus.storage.get_addr_as<suns_hdr>(context.last_modbus_addr);
			if (!hdr) {
				LogError("Could not get header, overflow");
				context.request_close = true;
				break;
			}
			LogInfo("Got header data for id: {}", modbus_swap(hdr->id));
			switch (hdr->id) {
				case model_inverter::ID: 	context.inverter_addr = context.last_modbus_addr; break;
				case model_nameplate::ID:	context.nameplate_addr = context.last_modbus_addr; break;
				case model_settings::ID:	context.settings_addr = context.last_modbus_addr; break;
				case model_status::ID:		context.status_addr = context.last_modbus_addr; break;
				case model_mppt::ID:		context.mppt_addr = context.last_modbus_addr; 
								context.mppt_length = hdr->length(); break;
				case model_controls::ID:	context.controls_addr = context.last_modbus_addr; break;
				case model_storage::ID:		context.storage_addr = context.last_modbus_addr; break;
				case model_end::ID: 		context.state = pcb_state::ENABLE_STORAGE_CONTROL;
				default:;
			}
			if (context.state != pcb_state::ENABLE_STORAGE_CONTROL) { // continue searching
				context.next_hdr_addr = context.last_modbus_addr + SUNSPEC_HDR_SIZE + hdr->length();
				request_modbus_registers(context, context.next_hdr_addr, SUNSPEC_HDR_SIZE);
				break;
			} 
			[[fallthrough]];
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
			context.state = pcb_state::ENABLE_INVERTER_CONTROL;
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::StorCtl_Mod), suns_sizeof<decltype(model_storage::StorCtl_Mod)>());
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
			context.state = pcb_state::WAIT_DATA_RESPONSE;
			request_modbus_registers_write(context, context.controls_addr + suns_offsetof(&model_controls::WMaxLim_Ena), suns_sizeof<decltype(model_controls::WMaxLim_Ena)>());
			break;
		}
		// Data fetching -----------------------------------------------------------------------
		case pcb_state::START_DATA_FETCH:
			LogInfo("Starting to fetch data, {}ms", time_ms());
			context.state = pcb_state::GET_INVERTER_INFOS;
			[[fallthrough]]; // directly execute get common
		case pcb_state::GET_INVERTER_INFOS:
			ASSERT_BREAK_CONTEXT(context.inverter_addr != -1, "Inverter address unknown");
			request_modbus_registers(context, context.inverter_addr, suns_sizeof(model_inverter{}));
			context.state = pcb_state::GET_NAMEPLATE_INFOS;
			break;
			// inverter infos have to be read always, so no fallthrough possible
		case pcb_state::GET_NAMEPLATE_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.nameplate_addr != -1, "Nameplate address unknown");
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
			ASSERT_BREAK_CONTEXT(context.settings_addr != -1, "Settings address unknown");
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
			ASSERT_BREAK_CONTEXT(context.status_addr != -1, "Status address unknown");
			if (context.status_fetched_s == 0 || s - context.status_fetched_s >= STATUS_REFETCH_S) {
				context.status_fetched_s = s;
				request_modbus_registers(context, context.status_addr, suns_sizeof(model_status{}));
				context.state = pcb_state::GET_CONTROLS_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_CONTROLS_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.controls_addr != -1, "Controls address unknown");
			if (context.controls_fetched_s == 0) {
				context.controls_fetched_s = s;
				request_modbus_registers(context, context.controls_addr, suns_sizeof(model_controls{}));
				context.state = pcb_state::GET_MPPT_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_MPPT_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.mppt_addr != -1, "Mppt address unknown");
			// always fetch mptt infos
			context.controls_fetched_s = s;
			request_modbus_registers(context, context.mppt_addr, context.mppt_length);
			context.state = pcb_state::GET_STORAGE_INFOS;
			break;
		case pcb_state::GET_STORAGE_INFOS:
			if (p)
				parse_modbus_frame(context, p);
			ASSERT_BREAK_CONTEXT(context.storage_addr != -1, "Storage address unknown");
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
			LogInfo("Back to idle at: {}ms", time_ms());
			context.state = pcb_state::IDLE;
			break;
		// Data writing ---------------------------------------------------------
		case pcb_state::SET_POWER_INVERTER:
			if (p)
				parse_modbus_frame(context, p);
			request_modbus_registers_write(context, context.controls_addr + suns_offsetof(&model_controls::WMaxLimPct), 1);
			context.state = pcb_state::SET_POWER_STORAGE;
			break;
		case pcb_state::SET_POWER_STORAGE:
			if (p)
				parse_modbus_frame(context, p);
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::WChaMax), 1);
			context.state = pcb_state::SET_MIN_SOC_STORAGE;
			break;
		case pcb_state::SET_MIN_SOC_STORAGE:
			if (p)
				parse_modbus_frame(context, p);
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::MinRsvPct), 1);
			context.state = pcb_state::WAIT_DATA_RESPONSE;
			break;
	}
	if (context.state == pcb_state::IDLE)
		context.wait_receive = false;
	if (context.state == pcb_state::IDLE && prev_state != pcb_state::IDLE) // wakeup main task
		xTaskNotifyGiveIndexed(parent_task, i);
}
static void request_modbus_registers(context_t &context, int offset, int register_count) {
	// LogInfo("Getting registers {}-{}", offset, offset + register_count);
	int i = &context - contexts.begin();
	context.modbus.switch_to_request();
	context.last_modbus_addr = offset;
	ASSERT_OK_RETURN(context.modbus.start_tcp_frame(context.tcp_frame++, inverters().configured_inverters[0][i].modbus_id));
	auto [res, err] = context.modbus.get_frame_read(libmodbus_static::register_t::HALFS, offset, register_count);
	ASSERT_OK_RETURN(err);
	err_t error = tcp_write(context.pcb, res.data(), res.size(), 0);
	if (error != ERR_OK) {
		LogError("Error sending modbus read frame {}", error);
		return;
	}
	error = tcp_output(context.pcb);
	if (error != ERR_OK)
		LogError("Error output modbus read frame {}", error);
}
static void request_modbus_registers_write(context_t &context, int offset, int register_count) {
	int i = &context - contexts.begin();
	context.modbus.switch_to_request();
	context.last_modbus_addr = offset;
	uint16_t* data_start = context.modbus.storage.halfs_registers.data.data() + offset - generic_halfs_registers::OFFSET;
	std::span<uint8_t> data{(uint8_t*)data_start, (uint8_t*)(data_start + register_count)};
	ASSERT_OK_RETURN(context.modbus.start_tcp_frame(context.tcp_frame++, inverters().configured_inverters[0][i].modbus_id));
	auto [res, err] = context.modbus.get_frame_write(libmodbus_static::register_t::HALFS_WRITE, offset, data);
	ASSERT_OK_RETURN(err);
	err_t error = tcp_write(context.pcb, res.data(), res.size(), 0);
	if (error != ERR_OK)
		LogError("Error sending modbus write frame {}", error);
}
static void parse_modbus_frame(context_t &context, struct pbuf *&p) {
	if (!p) {
		LogError("Cant parse modbus frame because its empty");
	}
	if (p->tot_len > 0) {
		context.modbus.switch_to_response();
		for (int i: range(p->tot_len)) {
			// LogInfo("Res byte: {:x}", pbuf_get_at(p, i));
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
	} else if (context.last_modbus_addr == context.status_addr) {
		const model_status *status = context.modbus.storage.get_addr_as<model_status>(context.status_addr);
		bitfield16 pv_status = modbus_swap(status->PVConn);
		bitfield16 bat_status = modbus_swap(status->StorConn);
		if (pv_status > 0 && inverters().read_power[i].pv.device_id == 0)
			inverters().read_power[i].pv.device_id = get_next_device_id();
		if (pv_status == 0 && inverters().read_power[i].pv.device_id != 0)
			inverters().read_power[i].pv.device_id = 0;
		if (bat_status > 0 && inverters().read_power[i].battery.device_id == 0)
			inverters().read_power[i].battery.device_id = get_next_device_id();
		if (bat_status == 0 && inverters().read_power[i].battery.device_id != 0)
			inverters().read_power[i].battery.device_id = 0;
	} else if (context.last_modbus_addr == context.mppt_addr) {
		constexpr uint16_t mppt_hdr_size = suns_sizeof(model_mppt{}) - 4 * suns_sizeof(mppt_infos{});
		static_assert(mppt_hdr_size == 10);
		const model_mppt *mppt = context.modbus.storage.get_addr_as<model_mppt>(context.mppt_addr);
		int mppt_count = modbus_swap(mppt->N);
		const mppt_infos *mppts = (const mppt_infos*)(((const uint16_t*)mppt) + mppt_hdr_size);
		// if battery is enabled it is expected to have the last 2 entries of the mppt infos being battery charge and discharge
		int bat_count = inverters().read_power[i].battery.device_id == 0 ? 0: 2;
		inverters().read_power[i].pv.exp_w = 0;
		int pv_count = mppt_count - bat_count;
		int16_t pf = modbus_swap_i16(mppt->DCW_SF);
		for (int i: range(pv_count))
			inverters().read_power[i].pv.exp_w += to_float(modbus_swap(mppts[i].module_DCW), pf);
		if (bat_count > 0) {
			// charging
			inverters().read_power[i].battery.imp_w = to_float(modbus_swap(mppts[mppt_count - 2].module_DCW), pf);
			// discharging
			inverters().read_power[i].battery.exp_w = to_float(modbus_swap(mppts[mppt_count - 1].module_DCW), pf);
		}
	} else if (context.last_modbus_addr == context.storage_addr) {
		const model_storage *storage = context.modbus.storage.get_addr_as<model_storage>(context.storage_addr);
		inverters().control_infos[i].soc = to_float(modbus_swap(storage->ChaState), modbus_swap_i16(storage->ChaState_SF));
		inverters().control_infos[i].power_max_cha = to_float(modbus_swap(storage->WChaMax), modbus_swap_i16(storage->WChaMax_SF));
		inverters().control_infos[i].power_max_discha = inverters().control_infos[i].power_max_cha;
	}
}

// pcb handle functions --------------------------------------------------------------------------------
static void init_pcb(context_t &context) {
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
static err_t tcp_pcb_close(tcp_pcb *pcb) { 
	LogInfo("close tcp_pcb");
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
static err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
	context_t &self = (*(context_t*)arg);
	self.pcb = tpcb;
	self.pcb->so_options |= SOF_KEEPALIVE;
	advance_context_state(self);
	return ERR_OK;
}
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	context_t &self = (*(context_t*)arg);
	advance_context_state(self, p);
	return ERR_OK;
}
static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	return ERR_OK;
}
static void tcp_err_cb(void *arg, err_t err) {
	LogInfo("Error callback: {}", err);
	context_t &self = (*(context_t*)arg);
	self.pcb = {};
	self.connected = false;
	self.state = pcb_state::IDLE;
	xTaskNotifyGiveIndexed(parent_task, &self - contexts.begin());
}
