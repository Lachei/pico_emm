#pragma once

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"


#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_TEST_TIME (30 * 1000)
#define NTP_RESEND_TIME (10 * 1000)

struct ntp_client {
	static ntp_client& Default() { // avoids any stack allocations which means stack can stay as small as possible
		static ntp_client client{};
		return client;
	}

	ip_addr_t ntp_server_address{};
	struct udp_pcb *ntp_pcb{};
	absolute_time_t ntp_test_time{};
	alarm_id_t ntp_resend_alarm{};
	time_t ntp_time{};
	time_t local_time{};

	bool synched() const { return ntp_time; }
	void update_time();
	time_t get_time_since_epoch();
	void set_time_since_epoch(time_t t);
};
