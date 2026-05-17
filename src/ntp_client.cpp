#include "pico/time.h"

#include "ntp_client.h"
#include "log_storage.h"

static void ntp_result(ntp_client* state, int status, time_t *result) {
    if (status == 0 && result) {
        struct tm *utc = gmtime(result);
        LogInfo("got ntp response: {}/{}/{} {}:{}:{}", utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
               utc->tm_hour, utc->tm_min, utc->tm_sec);
    }
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data);

static void ntp_request(ntp_client *state) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *) p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b;
    udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
    pbuf_free(p);
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data)
{
    ntp_client* state = (ntp_client*)user_data;
    LogError("ntp request failed");
    ntp_result(state, -1, NULL);
    return 0;
}

static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    ntp_client *state = (ntp_client*)arg;
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
        mode == 0x4 && stratum != 0) {
        state->local_time = time_us_64() / 1000000u;
        uint8_t seconds_buf[4] = {0};
        pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
        time_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
        if (seconds_since_1900 < NTP_DELTA)
            seconds_since_1900 += (time_t(1) << 32);
        time_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
        state->ntp_time = seconds_since_1970;
        ntp_result(state, 0, &state->ntp_time);
    } else {
        LogError("Invalid ntp response");
        ntp_result(state, -1, NULL);
    }
    pbuf_free(p);
}

static void ntp_init(ntp_client &client) {
    client.ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!client.ntp_pcb) {
        LogError("Failed to allocate ip address");
        return;
    }
    udp_recv(client.ntp_pcb, ntp_recv, &client);
}

static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    ntp_client *state = (ntp_client*)arg;
    if (ipaddr) {
        state->ntp_server_address = *ipaddr;
        LogInfo("Ntp address {}", ipaddr_ntoa(ipaddr));
        ntp_request(state);
    } else {
        LogInfo("Ntp dns request failed");
        ntp_result(state, -1, NULL);
    }
}

void ntp_client::update_time() {
    if (!ntp_pcb)
        ntp_init(*this);
    if (!ntp_pcb)
        return;

    int err = dns_gethostbyname(NTP_SERVER, &ntp_server_address, ntp_dns_found, this);
    if (err != ERR_OK) {
        LogError("Failed to resolve ntp hostname");
        return;
    }
}

time_t ntp_client::get_time_since_epoch() const {
    return ntp_time + (time_us_64() / 1000000u) - local_time;
}

uint32_t ntp_client::get_days_since_epoch() const {
    time_t secs = get_time_since_epoch();
    return secs / (60 * 60 * 24);
}

void ntp_client::set_time_since_epoch(time_t t) {
    local_time = time_us_64() / 1000000u;
    ntp_time = t;
}

