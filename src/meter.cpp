#include "meter.h"
#include "modbus-register.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "meter_sunspec.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/pbuf.h>
#include <lwip/tcp.h>

using namespace libmodbus_static;
using namespace g;

#define ASSERT_OK_FAIL(status) {std::string_view s = status; if ((s) != OK) {LogError(s); context.request_failed = true; return;}}
#define ASSERT_OK_FAIL_BREAK(status) {std::string_view s = status; if ((s) != OK) {LogError(s); context.request_failed = true; break;}}

namespace e {
enum class state {IDLE, CONNECTING, CHECK_SUNS, FIND_COMMON_HDR, FETCH_DATA, WAIT_DATA_RESPONSE};
}

namespace t{
struct context {
	struct tcp_pcb *pcb{};
	bool connected{};
	bool wait_receive{};
	bool request_close{};
	bool request_failed{}; // set if a request could not be sent, the state machine then goes back to idle
	int wait_count{};
	int tcp_frame{};
	uint32_t last_update_s{};
	e::state state{e::state::IDLE};
	modbus_register<meter_layout> modbus{.addr = 0}; // client always has addr 1
};
}
static t::context context{};
static TaskHandle_t parent_task{};

constexpr uint32_t time_ms() { return time_us_64() / 1000; }
constexpr uint32_t time_s() { return time_us_64() / 1000000; }
constexpr uint32_t METER_STALE_S{10};

static void init_pcb(t::context &context);
static void tcp_err_cb(void *arg, err_t err);
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t tcp_pcb_close(tcp_pcb *pcb);
static void advance_context_state(t::context &context, bool received = false);

void meter_info::initiate_discover(ModbusTcpAddr address) {
	parent_task = xTaskGetCurrentTaskHandle();
	if (address != addr) { // close previous connection and restart discovery with the new address
		cyw43_arch_lwip_begin();
		tcp_pcb_close(context.pcb);
		cyw43_arch_lwip_end();
		context.pcb = {};
		context.connected = context.wait_receive = context.request_close = false;
		context.state = e::state::IDLE;
		context.wait_count = 0;
		context.request_failed = false;
		name.fill(address == ModbusTcpAddr{} ? NOT_CONNECTED: CONNECTING);
		power_info.imp_w = power_info.exp_w = 0;
	}
	addr = address;
	power_info.device_id = METER_ID;
	if (addr == ModbusTcpAddr{}) // no meter configured (anymore)
		return;
	if (!context.pcb) {
		cyw43_arch_lwip_begin();
		init_pcb(context);
		cyw43_arch_lwip_end();
	}
	if (!context.pcb)
		return;
	ip_addr_t ip{.addr = PP_HTONL(addr.ip)};
	if (!context.connected && context.state != e::state::CONNECTING) {
		LogInfo("Tcp connect meter");
		context.state = e::state::CONNECTING;
		context.wait_receive = true;
		cyw43_arch_lwip_begin();
		err_t err = tcp_connect(context.pcb, &ip, addr.port, tcp_connect_cb);
		cyw43_arch_lwip_end();
		if (err != ERR_OK)
			LogError("Tcp connect meter failed {}", err);
	}
}
void meter_info::initiate_retrieve_infos() {
	ulTaskNotifyTakeIndexed(MAX_INVERTERS, pdTRUE, 0);
	if (!context.connected)
		return;
	if (context.state != e::state::IDLE) {
		LogError("Start info failed meter: state {} retry {}", (int)context.state, context.wait_count);
		return;
	}
	context.wait_receive = true;
	context.state = e::state::FETCH_DATA;
	cyw43_arch_lwip_begin();
	advance_context_state(context);
	cyw43_arch_lwip_end();
}
void meter_info::wait_requests(uint32_t timeout_ms) {
	// do not keep stale values if the meter is not reachable anymore
	if (time_s() - context.last_update_s > METER_STALE_S)
		power_info.imp_w = power_info.exp_w = 0;
	if (context.connected && context.wait_receive)
		ulTaskNotifyTakeIndexed(MAX_INVERTERS, pdTRUE, pdMS_TO_TICKS(timeout_ms));
	if (context.state == e::state::IDLE || !context.pcb) {
		context.wait_count = 0;
		context.wait_receive = false;
	}
	bool timeout = ++context.wait_count > 3;
	if (timeout) {
		context.wait_count = 0;
		LogError("Meter Wait expired, initiate reconnection");
	}
	if (context.request_close || timeout) {
		cyw43_arch_lwip_begin();
		tcp_pcb_close(context.pcb); // only close the pcb, dont reorder
		cyw43_arch_lwip_end();
		context.pcb = {};
		context.connected = false;
		context.request_close = false;
		context.wait_receive = false;
		context.state = e::state::IDLE;
		if (!timeout)
			name.fill(CONNECTING); // resetting name signals disconnected inverter
	}
}

// private implementations

// modbus logic functions ------------------------------------------------------------------------------
static void request_modbus_registers(t::context &context, int offset, int registers);
static void request_send(t::context &context, std::span<uint8_t> d);
static bool receive_modbus_bytes(t::context &context, struct pbuf *p);
static void advance_context_state(t::context &context, bool received) {
	e::state prev_state = context.state;
	switch(context.state) {
		case e::state::IDLE:
			context.wait_count = 0;
			break;
		// Connection setup cases ---------------------------------------------------------
		case e::state::CONNECTING:
			context.connected = true;
			context.wait_count = 0;
			if (meter().name.sv() != CONNECTING && meter().name.sv() != NOT_CONNECTED) {
				LogInfo("Connected to meter, got info already");
				context.state = e::state::IDLE;
				break;
			}
			LogInfo("Meter connected, requesting Suns register {}ms", time_ms());
			// check sunspec header
			request_modbus_registers(context, meter_registers::OFFSET, suns_sizeof(model_start{}));
			context.state = e::state::CHECK_SUNS;
			break;
		case e::state::CHECK_SUNS: {
			LogInfo("Checking sunspec id");
			std::string_view id = to_sv(context.modbus.storage.halfs_registers.sid);
			LogInfo("Got id: {}", id);
			if (id != "SunS") {
				LogError("Invalid sunspec meeter, removing");
				context.request_close = true;
				break;
			}
			context.state = e::state::FIND_COMMON_HDR;
			context.modbus.switch_to_request();
			ASSERT_OK_FAIL_BREAK(context.modbus.start_tcp_frame(context.tcp_frame++, meter().addr.modbus_id));
			auto [res, err] = context.modbus.get_frame_read(&meter_registers::id_common, &meter_registers::id_meter);
			ASSERT_OK_FAIL_BREAK(err);
			request_send(context, res);
			break;
		}
		case e::state::FIND_COMMON_HDR: {
			LogInfo("Meter searching common header");
			// dont use read to keep modbus swap for check
			uint16_t id_common = context.modbus.storage.halfs_registers.id_common; 
			uint16_t id_meter = context.modbus.storage.halfs_registers.id_meter;
			if (id_common == model_common::ID && id_meter == model_meter::ID) {
				meter().name.fill(to_sv(context.modbus.storage.halfs_registers.device_model));
				LogInfo("Meter registered with name {}", meter().name.sv());
			} else
				context.request_close = true;
			context.state = e::state::IDLE;
			break;
		}
		// Data fetching --------------------------------------------------------------------
		case e::state::FETCH_DATA: {
			LogInfo("Meter starting to fetch data, {}ms", time_ms());
			context.state = e::state::WAIT_DATA_RESPONSE;
			context.modbus.switch_to_request();
			ASSERT_OK_FAIL_BREAK(context.modbus.start_tcp_frame(context.tcp_frame++, meter().addr.modbus_id));
			auto [res, err] = context.modbus.get_frame_read(&meter_registers::A, &meter_registers::TotWhImpPhC);
			ASSERT_OK_FAIL_BREAK(err);
			request_send(context, res);
			break;
		}
		case e::state::WAIT_DATA_RESPONSE:
			if (!received) // only use the power values when a response came in
				break;
			// parsing modbus infos back to power info
			float w = context.modbus.read(&meter_registers::W);
			meter().power_info.imp_w = std::max(w, .0f);
			meter().power_info.exp_w = -std::min(w, .0f);
			context.last_update_s = time_s();
			LogInfo("Meter back to idle at: {}ms, {}W", time_ms(), w);
			context.state = e::state::IDLE;
			break;
	}
	if (context.request_failed) {
		// no request went out, so no response will come: do not wait for it
		context.request_failed = false;
		if (prev_state == e::state::CONNECTING || prev_state == e::state::CHECK_SUNS)
			context.request_close = true; // incomplete discovery, reconnect
		LogError("Meter request failed in state {}, back to idle", int(prev_state));
		context.state = e::state::IDLE;
	}
	if (context.state == e::state::IDLE)
		context.wait_receive = false;
	if (context.state == e::state::IDLE && prev_state != e::state::IDLE && parent_task) // wakeup main task
		xTaskNotifyGiveIndexed(parent_task, MAX_INVERTERS);
}
static void request_modbus_registers(t::context &context, int offset, int register_count) {
	// LogInfo("Getting registers {}-{}", offset, offset + register_count);
	context.modbus.switch_to_request();
	ASSERT_OK_FAIL(context.modbus.start_tcp_frame(context.tcp_frame++, meter().addr.modbus_id));
	auto [res, err] = context.modbus.get_frame_read(libmodbus_static::register_t::HALFS, offset, register_count);
	ASSERT_OK_FAIL(err);
	request_send(context, res);
}
static void request_send(t::context &context, std::span<uint8_t> d) {
	// copy the data, the frame buffer is reused for the response
	err_t error = tcp_write(context.pcb, d.data(), d.size(), TCP_WRITE_FLAG_COPY);
	context.modbus.switch_to_response();
	if (error != ERR_OK) {
		LogError("Error sending modbus read frame {}", error);
		context.request_failed = true;
		return;
	}
	error = tcp_output(context.pcb);
	if (error != ERR_OK)
		LogError("Error output modbus read frame {}", error);
}
// feeds the received bytes to the modbus parser, returns true if a full response frame was processed
static bool receive_modbus_bytes(t::context &context, struct pbuf *p) {
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

// pcb handle functions --------------------------------------------------------------------------------
static void init_pcb(t::context &context) {
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
	t::context &self = (*(t::context*)arg);
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
	t::context &self = (*(t::context*)arg);
	if (!p) { // remote side closed the connection
		LogWarning("Meter closed the connection");
		err_t close_err = tcp_pcb_close(tpcb);
		self.pcb = {};
		self.connected = false;
		self.wait_receive = false;
		self.state = e::state::IDLE;
		if (parent_task)
			xTaskNotifyGiveIndexed(parent_task, MAX_INVERTERS);
		return close_err;
	}
	tcp_recved(tpcb, p->tot_len);
	// unexpected data (eg. a late answer after a timeout) is dropped
	bool frame_done = self.state != e::state::IDLE && receive_modbus_bytes(self, p);
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
	t::context &self = (*(t::context*)arg);
	self.pcb = {}; // pcb is already freed by lwip
	self.connected = false;
	self.wait_receive = false;
	self.state = e::state::IDLE;
	if (parent_task)
		xTaskNotifyGiveIndexed(parent_task, MAX_INVERTERS);
}
