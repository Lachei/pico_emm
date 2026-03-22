#pragma once

#include <iostream>

#include "AppConfig.h"
#include "static_types.h"
#include "ranges_util.h"
#include "string_util.h"
#include "emm_structs.h"

inline bool request_settings_store{};
inline bool request_settings_load{};
struct settings {
	static_vector<ModbusTcpAddr, MAX_INVERTERS> configured_inverters{};

	static settings& Default() {
		static settings s{};
		return s;
	}
	/** @brief writes the settings struct as json to the static strig s */
	template<int N>
	constexpr void dump_to_json(static_string<N> &s) const {
		s.append(R"({{"configured_ips":[)");
		for (int i: range(configured_inverters.size())) {
			ModbusTcpAddr &a = configured_inverters[i];
			if (i != 0)
				s.append(',');
			s.append_formatted(R"("{}.{}.{}.{}:{}|{}")", a.ip >> 24, (a.ip >> 16) & 0xff, (a.ip >> 8) & 0xff, a.ip & 0xff, a.port, (int)a.modbus_id);
		}
		s.append("]}");
	}

	constexpr void sanitize() { configured_inverters.sanitize(); }
};

/** @brief prints formatted for monospace output, eg. usb */
inline std::ostream& operator<<(std::ostream &os, const settings &s) {
	const auto ip_to_stream = [](std::ostream &os, const ModbusTcpAddr &a) {
		os << (a.ip >> 24) << '.' << ((a.ip >> 16) & 0xff) << '.' << ((a.ip >> 8) & 0xff) << '.' << (a.ip & 0xff) << ':' << a.port << '|' << (int)a.modbus_id;
	};
	os << "configured_inverters: ";
	for (int i: range(s.configured_inverters.size())) {
		if (i != 0)
			os << "\n                      ";
		ip_to_stream(os, s.configured_inverters[i]);
	}
	return os << '\n';
}

/** @brief parses a single key, value pair from the istream */
inline std::istream& operator>>(std::istream &is, settings &s) {
	std::string key;
	std::string ip;
	is >> key;
	if (key == "configure_inverter") {
		is >> ip;
		std::string_view ip_view = ip;
		ModbusTcpAddr *conf_ip = s.configured_inverters.push();
		if (!conf_ip)
			is.fail();
		else {
			*conf_ip = {};
			std::string_view cur;
			for (int shift = 24; extract_word(ip_view, cur, '.') && shift >= 0; shift -= 8)
				conf_ip->ip |= to_int(cur).value_or(0) << shift;
			// cur is now port|modbus_id
			conf_ip->port = to_int(extract_word(cur, '|')).value_or(0);
			conf_ip->modbus_id = to_int(cur).value_or(0);
		}
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

