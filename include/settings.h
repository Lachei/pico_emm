#pragma once

#include <iostream>

#include "AppConfig.h"
#include "static_types.h"
#include "ranges_util.h"
#include "string_util.h"
#include "emm_structs.h"

template<int N>
void print_ip(static_string<N>& s, uint32_t ip, uint16_t port, uint8_t m_id) {
	s.append_formatted(R"("{}.{}.{}.{}:{}|{}")", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port, (int)m_id);
}

inline bool request_settings_store{};
inline bool request_settings_load{};
struct settings {
	bool enable_emm{};
	float max_export{}; // can be used to set maximum export limit of plant
	static_vector<int, MAX_INVERTERS> inverter_bat_prio{}; // high prio means that the battery should be kept full
	ModbusTcpAddr configured_meter{};
	static_vector<ModbusTcpAddr, MAX_INVERTERS> configured_inverters{};

	static settings& Default() {
		static settings s{};
		return s;
	}
	/** @brief writes the settings struct as json to the static strig s */
	template<int N>
	constexpr void dump_to_json(static_string<N> &s) const {
		s.append(R"({"configured_ips":[)");
		for (int i: range(configured_inverters.size())) {
			ModbusTcpAddr &a = configured_inverters[i];
			if (i != 0)
				s.append(',');
			print_ip(s, a.ip, a.port, a.modbus_id);
		}
		s.append(R"(],"configured_meter":)");
		print_ip(s, configured_meter);
		s.append("}");
	}

	constexpr void sanitize() { 
		configured_inverters.sanitize(); 
		inverter_bat_prio.sanitize();
		if (inverter_bat_prio.size() < configured_inverters.size())
			for (int i: range(inverter_bat_prio.size(), configured_inverters.size()))
				inverter_bat_prio[i] = 1;
		inverter_bat_prio.resize(configured_inverters.size());
	}
};

/** @brief prints formatted for monospace output, eg. usb */
inline std::ostream& operator<<(std::ostream &os, const settings &s) {
	const auto ip_to_stream = [](std::ostream &os, const ModbusTcpAddr &a) {
		os << (a.ip >> 24) << '.' << ((a.ip >> 16) & 0xff) << '.' << ((a.ip >> 8) & 0xff) << '.' << (a.ip & 0xff) << ':' << a.port << '|' << (int)a.modbus_id;
	};
	os << "configured_inverters [" << s.configured_inverters.size() << "]:\n";
	for (int i: range(s.configured_inverters.size())) {
		os << "  ";
		ip_to_stream(os, s.configured_inverters[i]);
		os << '\n';
	}
	os << "configured_meter: ";
	ip_to_stream(os, s.configured_meter);
	return os << '\n';
}

/** @brief parses a single key, value pair from the istream */
inline std::istream& operator>>(std::istream &is, settings &s) {
	const auto parse_ip = [](std::string_view ip, ModbusTcpAddr &conf_ip) {
		std::string_view cur;
		for (int shift = 24; extract_word(ip, cur, '.') && shift >= 0; shift -= 8)
			conf_ip.ip |= to_int(cur).value_or(0) << shift;
		// cur is now port|modbus_id
		conf_ip.port = to_int(extract_word(cur, '|')).value_or(0);
		conf_ip.modbus_id = to_int(cur).value_or(0);
	};
	std::string key;
	std::string ip;
	is >> key;
	if (key == "configure_inverter") {
		is >> ip;
		ModbusTcpAddr *conf_ip = s.configured_inverters.push();
		if (!conf_ip)
			is.fail();
		else {
			*conf_ip = {};
			parse_ip(ip, *conf_ip);
		}
	} else if (key == "configure_meter") {
		is >> ip;
		parse_ip(ip, s.configured_meter);

	} else
		is.fail();
	return is;
}

struct AddrName {
	ModbusTcpAddr addr{};
	static_string<32> name{"hello"};
};
struct runtime_state {
	static runtime_state& Default() {
		static runtime_state r{};
		return r;
	}
	static_vector<AddrName, MAX_INVERTERS> found_ips{};
};

