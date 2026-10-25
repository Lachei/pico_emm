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
using namespace g;
constexpr uint32_t time_ms() { return time_us_64() / 1000; }
constexpr uint32_t time_s() { return time_us_64() / 1000000; }

enum class pcb_state {
	IDLE, CONNECTING, CHECK_SUNS, FIND_COMMON_HDR, GET_COMMON_INFOS, FIND_DATA_HDR, START_DATA_FETCH,
	GET_INVERTER_INFOS, GET_NAMEPLATE_INFOS, GET_SETTINGS_INFOS, GET_STATUS_INFOS,
	GET_CONTROLS_INFOS, GET_MPPT_INFOS, GET_STORAGE_INFOS, ENABLE_INVERTER_CONTROL, ENABLE_STORAGE_CONTROL,
	WAIT_DATA_RESPONSE, SET_POWER_INVERTER, SET_POWER_STORAGE, SET_STORAGE_MODE};
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
constexpr static int MAX_MODBUS_READ_REGISTERS{125};
// storage control (sunspec model 124 as used by fronius gen24):
// StorCtl_Mod bit0 activates the charge limit InWRte, bit1 the discharge limit OutWRte (both in % of WChaMax).
// A negative OutWRte forces charging, a negative InWRte forces discharging, so InWRte = c, OutWRte = -c
// pins the battery to charge with c and InWRte = -d, OutWRte = d pins it to discharge with d.
constexpr static uint16_t STOR_CTL_LIMIT_CHARGE{1};
constexpr static uint16_t STOR_CTL_LIMIT_DISCHARGE{2};
// charging from the ac side has to be allowed, else a battery can not take up the export of other inverters
constexpr static uint16_t CHA_GRI_SET_GRID{1};
// if the emm stops sending (crash, network loss) the inverters fall back to their own control after this time
constexpr static uint16_t CONTROL_REVERT_S{60};
// WMaxLimPct, WMaxLimPct_WinTms, WMaxLimPct_RvrtTms, WMaxLimPct_RmpTms, WMaxLim_Ena
static const int INVERTER_LIMIT_REGISTERS{suns_offsetof(&model_controls::WMaxLim_Ena) - suns_offsetof(&model_controls::WMaxLimPct) + 1};
// OutWRte, InWRte, InOutWRte_WinTms, InOutWRte_RvrtTms, InOutWRte_RmpTms
static const int STORAGE_RATE_REGISTERS{suns_offsetof(&model_storage::InOutWRte_RmpTms) - suns_offsetof(&model_storage::OutWRte) + 1};
static int16_t from_float_i16(float val, sunssf scale) { return int16_t(std::lround(val * std::pow<float>(10, -scale))); }
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
	ModbusTcpAddr addr{}; // address the context belongs to, used to detect configuration changes
};
static static_vector<context_t, MAX_INVERTERS> contexts{};
static TaskHandle_t parent_task{};

static void init_pcb(context_t &context);
static void tcp_err_cb(void *arg, err_t err);
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t tcp_pcb_close(tcp_pcb *pcb);
static void advance_context_state(context_t &context, bool received = false);

// closes the connection and resets all infos of the inverter at index i
static void reset_inverter(int i) {
	context_t &c = contexts[i];
	cyw43_arch_lwip_begin();
	tcp_pcb_close(c.pcb);
	cyw43_arch_lwip_end();
	c.pcb = {};
	c.connected = c.wait_receive = c.request_close = false;
	c.state = pcb_state::IDLE;
	c.wait_count = 0;
	c.next_hdr_addr = c.common_addr = c.inverter_addr = c.nameplate_addr = c.settings_addr = -1;
	c.status_addr = c.mppt_addr = c.mppt_length = c.controls_addr = c.storage_addr = c.last_modbus_addr = -1;
	c.common_fetched_s = c.nameplate_fetched_s = c.settings_fetched_s = c.status_fetched_s = 0;
	c.controls_fetched_s = c.storage_fetched_s = 0;
	c.addr = {};
	inverters().connected_names[i] = {};
	inverters().read_power[i] = {};
	inverters().control_infos[i] = {};
}

void inverter_infos::initiate_discover_inverters(static_vector<ModbusTcpAddr, MAX_INVERTERS> *ivs) {
	configured_inverters = ivs;
	CHECK_INVERTER_CONFIGURED;
	int inverter_count = configured_inverters->size();
	// inverters removed from the configuration have to be disconnected
	for (int i = inverter_count; i < contexts.size(); ++i)
		reset_inverter(i);
	connected_names.resize(inverter_count);
	read_power.resize(inverter_count);
	control_infos.resize(inverter_count);
	contexts.resize(inverter_count);
	for(int i: range(inverter_count)) {
		// the address at this index changed (eg. an inverter was removed), restart from scratch
		if (contexts[i].addr != configured_inverters[0][i]) {
			reset_inverter(i);
			contexts[i].addr = configured_inverters[0][i];
		}
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
		if (!contexts[i].connected && contexts[i].state != pcb_state::CONNECTING) {
			LogInfo("Tcp connect inverter");
			contexts[i].state = pcb_state::CONNECTING;
			contexts[i].wait_receive = true;
			cyw43_arch_lwip_begin();
			err_t err = tcp_connect(contexts[i].pcb, &ip, configured_inverters[0][i].port, tcp_connect_cb);
			cyw43_arch_lwip_end();
			if (err != ERR_OK)
				LogError("Tcp connect inverter failed {}", err);
		}
	}
}
void inverter_infos::initiate_retrieve_infos_all() {
	CHECK_INVERTER_CONFIGURED;
	// resetting all wait infos
	for (int i: range(contexts.size()))
		ulTaskNotifyTakeIndexed(i, pdTRUE, 0);
	for (int i: range(contexts.size())) {
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
void inverter_infos::initiate_send_power_requests_all(bool release) {
	CHECK_INVERTER_CONFIGURED;
	for (int i: range(contexts.size())) {
		if (connected_names[i].empty() || !contexts[i].connected || !control_infos[i].is_active())
			continue;
		if (contexts[i].state != pcb_state::IDLE) {
			LogError("Could not send power for {}: {}, retry {}", connected_names[i].sv(), int(contexts[i].state), contexts[i].wait_count);
			continue;
		}
		model_controls *control = contexts[i].modbus.storage.get_addr_as<model_controls>(contexts[i].controls_addr);
		model_storage *storage = contexts[i].modbus.storage.get_addr_as<model_storage>(contexts[i].storage_addr);
		// the rates are relative to WChaMax
		float bat_ref_w = storage ? to_float(modbus_swap(storage->WChaMax), modbus_swap_i16(storage->WChaMax_SF)): 0;
		if (!control || !storage || control_infos[i].power_max <= 0 || bat_ref_w <= 0) {
			LogError("Could not send power for {}: missing control infos", connected_names[i].sv());
			continue;
		}
		const ControlPowerInfo &ci = control_infos[i];
		float inv_power_r{100};      // ac power limit in % of WMax
		float charge_w{bat_ref_w};   // charge limit, negative forces discharging
		float discharge_w{bat_ref_w};// discharge limit, negative forces charging
		uint16_t stor_ctl_mod{};
		if (!release) {
			float pv_w = read_power[i].pv.exp_w;
			float cha_max = ci.power_max_cha > 0 ? ci.power_max_cha: bat_ref_w;
			float discha_max = ci.power_max_discha > 0 ? ci.power_max_discha: bat_ref_w;
			inv_power_r = std::clamp(ci.requested_power, .0f, ci.power_max) / ci.power_max * 100.f;
			stor_ctl_mod = STOR_CTL_LIMIT_CHARGE | STOR_CTL_LIMIT_DISCHARGE;
			if (ci.requested_power < 0) {
				// charge with the requested ac import plus everything pv delivers
				float c = std::min(-ci.requested_power + pv_w, cha_max);
				charge_w = c;
				discharge_w = -c;
			} else if (ci.requested_power > pv_w) {
				// pv is not enough, the battery has to deliver the rest
				float d = std::min(ci.requested_power - pv_w, discha_max);
				charge_w = -d;
				discharge_w = d;
			} else {
				// pv covers the request: no discharging, surplus pv (above the ac limit) may charge the battery
				charge_w = cha_max;
				discharge_w = 0;
			}
		}
		sunssf rate_sf = modbus_swap_i16(storage->InOutWRte_SF);
		float in_r = std::clamp(charge_w / bat_ref_w * 100.f, -100.f, 100.f);
		float out_r = std::clamp(discharge_w / bat_ref_w * 100.f, -100.f, 100.f);
		LogInfo("Power req {}: {}% in {}% out {}%", i, inv_power_r, in_r, out_r);

		control->WMaxLimPct = modbus_swap(from_float(inv_power_r, modbus_swap_i16(control->WMaxLimPct_SF)));
		control->WMaxLimPct_WinTms = 0;
		control->WMaxLimPct_RvrtTms = modbus_swap(CONTROL_REVERT_S);
		control->WMaxLimPct_RmpTms = 0;
		control->WMaxLim_Ena = modbus_swap(release ? 0: 1);
		storage->InWRte = modbus_swap_i16(from_float_i16(in_r, rate_sf));
		storage->OutWRte = modbus_swap_i16(from_float_i16(out_r, rate_sf));
		storage->InOutWRte_WinTms = 0;
		storage->InOutWRte_RvrtTms = modbus_swap(CONTROL_REVERT_S);
		storage->InOutWRte_RmpTms = 0;
		storage->StorCtl_Mod = modbus_swap(stor_ctl_mod);

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
		if (!control_infos[i].is_active()) {
			control_infos[i].requested_power = 0;
			read_power[i].inverter.imp_w = read_power[i].inverter.exp_w = 0;
			read_power[i].pv.imp_w = read_power[i].pv.exp_w = 0;
			read_power[i].battery.imp_w = read_power[i].battery.exp_w = 0;
			read_power[i].bat_soc = 0;
		}
		if (contexts[i].state == pcb_state::IDLE || !contexts[i].pcb) {
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
			contexts[i].state = pcb_state::IDLE;
			contexts[i].wait_receive = false;
			if (!timeout)
				connected_names[i] = {}; // resetting name signals disconnected inverter
		}
	}
}

// private implementations

// modbus logic functions ------------------------------------------------------------------------------
static void request_modbus_registers(context_t &context, int offset, int registers);
static void request_modbus_registers_write(context_t &context, int offset, int registers);
static void parse_modbus_frame(context_t &context, bool &received);
static void advance_context_state(context_t &context, bool received) {
	int i = &context - contexts.begin();
	pcb_state prev_state = context.state;
	uint32_t s = std::max(time_s(), uint32_t(1)); // 0 is reserved for never fetched
	switch(context.state) {
		case pcb_state::IDLE:
			context.wait_count = 0;
			break;
		// Connection setup cases ---------------------------------------------------------
		case pcb_state::CONNECTING:
			context.connected = true;
			context.wait_count = 0;
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
			parse_modbus_frame(context, received);
			const string<4> *hdr = context.modbus.storage.get_addr_as<string<4>>(context.last_modbus_addr);
			if (hdr)
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
			parse_modbus_frame(context, received);
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
			parse_modbus_frame(context, received);
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
			parse_modbus_frame(context, received);
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
			// allow charging from the ac side (eg. with the export of another inverter). The limits themselves are only
			// activated with the first power request, until then the inverter keeps running its own control
			model_storage *storage = context.modbus.storage.get_addr_as<model_storage>(context.storage_addr);
			storage->ChaGriSet = modbus_swap(CHA_GRI_SET_GRID);
			context.state = pcb_state::ENABLE_INVERTER_CONTROL;
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::ChaGriSet), suns_sizeof<decltype(model_storage::ChaGriSet)>());
			break;
		}
		case pcb_state::ENABLE_INVERTER_CONTROL: {
			LogInfo("Enable inverter control");
			if (context.controls_addr == -1) {
				LogError("Missing inverter control header, removing inverter");
				context.request_close = true;
				break;
			}
			// the limit is enabled together with the first power request
			context.state = pcb_state::IDLE;
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
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.nameplate_addr != -1, "Nameplate address unknown");
			if (context.nameplate_fetched_s == 0 || s - context.nameplate_fetched_s >= NAMEPLATE_REFETCH_S) {
				context.nameplate_fetched_s = s;
				request_modbus_registers(context, context.nameplate_addr, suns_sizeof(model_nameplate{}));
				context.state = pcb_state::GET_SETTINGS_INFOS;
				break;
			}
			[[fallthrough]]; // if nameplate is not fetched just continue
		case pcb_state::GET_SETTINGS_INFOS:
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.settings_addr != -1, "Settings address unknown");
			if (context.settings_fetched_s == 0 || s - context.settings_fetched_s >= SETTINGS_REFETCH_S) {
				context.settings_fetched_s = s;
				request_modbus_registers(context, context.settings_addr, suns_sizeof(model_settings{}));
				context.state = pcb_state::GET_STATUS_INFOS;
				break;
			}
			[[fallthrough]]; // if settings is not fetched fetch next
		case pcb_state::GET_STATUS_INFOS:
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.status_addr != -1, "Status address unknown");
			if (context.status_fetched_s == 0 || s - context.status_fetched_s >= STATUS_REFETCH_S) {
				context.status_fetched_s = s;
				request_modbus_registers(context, context.status_addr, suns_sizeof(model_status{}));
				context.state = pcb_state::GET_CONTROLS_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_CONTROLS_INFOS:
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.controls_addr != -1, "Controls address unknown");
			if (context.controls_fetched_s == 0) {
				context.controls_fetched_s = s;
				request_modbus_registers(context, context.controls_addr, suns_sizeof(model_controls{}));
				context.state = pcb_state::GET_MPPT_INFOS;
				break;
			}
			[[fallthrough]];
		case pcb_state::GET_MPPT_INFOS:
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.mppt_addr != -1, "Mppt address unknown");
			// always fetch mptt infos
			context.controls_fetched_s = s;
			request_modbus_registers(context, context.mppt_addr, std::min(SUNSPEC_HDR_SIZE + context.mppt_length, MAX_MODBUS_READ_REGISTERS));
			context.state = pcb_state::GET_STORAGE_INFOS;
			break;
		case pcb_state::GET_STORAGE_INFOS:
			parse_modbus_frame(context, received);
			ASSERT_BREAK_CONTEXT(context.storage_addr != -1, "Storage address unknown");
			if (context.storage_fetched_s == 0 || s - context.storage_fetched_s >= STORAGE_REFETCH_S) {
				context.storage_fetched_s = s;
				request_modbus_registers(context, context.storage_addr, suns_sizeof(model_storage{}));
				context.state = pcb_state::WAIT_DATA_RESPONSE;
				break;
			}
			[[fallthrough]];
		case pcb_state::WAIT_DATA_RESPONSE:
			parse_modbus_frame(context, received);
			LogInfo("Back to idle at: {}ms", time_ms());
			context.state = pcb_state::IDLE;
			break;
		// Data writing ---------------------------------------------------------
		case pcb_state::SET_POWER_INVERTER:
			parse_modbus_frame(context, received);
			request_modbus_registers_write(context, context.controls_addr + suns_offsetof(&model_controls::WMaxLimPct), INVERTER_LIMIT_REGISTERS);
			context.state = pcb_state::SET_POWER_STORAGE;
			break;
		case pcb_state::SET_POWER_STORAGE:
			parse_modbus_frame(context, received);
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::OutWRte), STORAGE_RATE_REGISTERS);
			context.state = pcb_state::SET_STORAGE_MODE;
			break;
		case pcb_state::SET_STORAGE_MODE:
			parse_modbus_frame(context, received);
			request_modbus_registers_write(context, context.storage_addr + suns_offsetof(&model_storage::StorCtl_Mod), suns_sizeof<decltype(model_storage::StorCtl_Mod)>());
			context.state = pcb_state::WAIT_DATA_RESPONSE;
			break;
	}
	if (context.state == pcb_state::IDLE)
		context.wait_receive = false;
	if (context.state == pcb_state::IDLE && prev_state != pcb_state::IDLE && parent_task) // wakeup main task
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
	err_t error = tcp_write(context.pcb, res.data(), res.size(), TCP_WRITE_FLAG_COPY);
	context.modbus.switch_to_response();
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
	err_t error = tcp_write(context.pcb, res.data(), res.size(), TCP_WRITE_FLAG_COPY);
	context.modbus.switch_to_response();
	if (error != ERR_OK) {
		LogError("Error sending modbus write frame {}", error);
		return;
	}
	error = tcp_output(context.pcb);
	if (error != ERR_OK)
		LogError("Error output modbus write frame {}", error);
}
// feeds the received bytes to the modbus parser, returns true if a full response frame was processed
static bool receive_modbus_bytes(context_t &context, struct pbuf *p) {
	bool frame_done{};
	for (int i: range(p->tot_len)) {
		std::string_view r = context.modbus.process_tcp(pbuf_get_at(p, i)).err;
		if (r == IN_PROGRESS)
			continue;
		frame_done = true;
		if (r != OK)
			LogError("Modbus parsing failed with {}", r);
	}
	return frame_done;
}
// processes the infos of a received response frame (the register data was already written by receive_modbus_bytes)
static void parse_modbus_frame(context_t &context, bool &received) {
	if (!received)
		return;
	received = false;
	// updating the inverter informations
	int i = &context - contexts.begin();
	inverters().control_infos[i].last_connection_s = time_s();
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
		// 0 means not supported by the inverter, keep the values from the other models then
		if (max_pow > 0)
			pi.power_max = max_pow;
		if (max_pow_bat_cha > 0)
			pi.power_max_cha = max_pow_bat_cha;
		if (max_pow_bat_discha > 0)
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
		// the model length limits the amount of modules that were actually read
		int read_registers = std::min(SUNSPEC_HDR_SIZE + context.mppt_length, MAX_MODBUS_READ_REGISTERS);
		int max_mppt_count = std::max(read_registers - mppt_hdr_size, 0) / suns_sizeof(mppt_infos{});
		int mppt_count = std::clamp<int>(modbus_swap(mppt->N), 0, max_mppt_count);
		const mppt_infos *mppts = (const mppt_infos*)(((const uint16_t*)mppt) + mppt_hdr_size);
		// if battery is enabled it is expected to have the last 2 entries of the mppt infos being battery charge and discharge
		int bat_count = inverters().read_power[i].battery.device_id == 0 || mppt_count < 2 ? 0: 2;
		inverters().read_power[i].pv.exp_w = 0;
		int pv_count = mppt_count - bat_count;
		int16_t pf = modbus_swap_i16(mppt->DCW_SF);
		for (int j: range(pv_count))
			inverters().read_power[i].pv.exp_w += to_float(modbus_swap(mppts[j].module_DCW), pf);
		if (bat_count > 0) {
			// charging
			inverters().read_power[i].battery.imp_w = to_float(modbus_swap(mppts[mppt_count - 2].module_DCW), pf);
			// discharging
			inverters().read_power[i].battery.exp_w = to_float(modbus_swap(mppts[mppt_count - 1].module_DCW), pf);
		}
	} else if (context.last_modbus_addr == context.storage_addr) {
		const model_storage *storage = context.modbus.storage.get_addr_as<model_storage>(context.storage_addr);
		inverters().read_power[i].bat_soc = to_float(modbus_swap(storage->ChaState), modbus_swap_i16(storage->ChaState_SF));
		ControlPowerInfo &pi = inverters().control_infos[i];
		float w_cha_max = to_float(modbus_swap(storage->WChaMax), modbus_swap_i16(storage->WChaMax_SF));
		if (w_cha_max > 0)
			pi.power_max_cha = w_cha_max;
		if (pi.power_max_discha <= 0)
			pi.power_max_discha = pi.power_max_cha;
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
	if (!arg)
		return ERR_OK;
	context_t &self = (*(context_t*)arg);
	self.pcb = tpcb;
	self.pcb->so_options |= SOF_KEEPALIVE;
	advance_context_state(self);
	return ERR_OK;
}
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	if (!arg) {
		if (p)
			pbuf_free(p);
		return ERR_OK;
	}
	context_t &self = (*(context_t*)arg);
	if (!p) { // remote side closed the connection
		LogWarning("Inverter closed the connection");
		err_t close_err = tcp_pcb_close(tpcb);
		self.pcb = {};
		self.connected = false;
		self.wait_receive = false;
		self.state = pcb_state::IDLE;
		if (parent_task)
			xTaskNotifyGiveIndexed(parent_task, &self - contexts.begin());
		return close_err;
	}
	tcp_recved(tpcb, p->tot_len);
	// unexpected data (eg. a late answer after a timeout) is dropped
	bool frame_done = self.state != pcb_state::IDLE && receive_modbus_bytes(self, p);
	pbuf_free(p);
	if (frame_done) // the response might be split accross multiple packets, only advance on a full frame
		advance_context_state(self, true);
	return ERR_OK;
}
static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	return ERR_OK;
}
static void tcp_err_cb(void *arg, err_t err) {
	LogInfo("Error callback: {}", err);
	if (!arg)
		return;
	context_t &self = (*(context_t*)arg);
	self.pcb = {}; // pcb is already freed by lwip
	self.connected = false;
	self.wait_receive = false;
	self.state = pcb_state::IDLE;
	if (parent_task)
		xTaskNotifyGiveIndexed(parent_task, &self - contexts.begin());
}
