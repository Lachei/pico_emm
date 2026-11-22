#pragma once

#include <iostream>
#include <cmath>

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
	int power_offset{}; // default power added to the requested power befor sending (ignores all emm logik)
	uint32_t version{0x454d4d02}; // on change resetes config on start
	bool enable_emm{}; // power control of the inverters active
	bool invert_home{}; // invert the sign of the measured home power
	float filter_alpha{.1f}; // smoothing of the home power, see EMM::filter_alpha
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
			const ModbusTcpAddr &a = configured_inverters[i];
			if (i != 0)
				s.append(',');
			print_ip(s, a.ip, a.port, a.modbus_id);
		}
		s.append(R"(],"configured_meter":)");
		print_ip(s, configured_meter.ip, configured_meter.port, configured_meter.modbus_id);
		s.append("}");
	}

	constexpr void sanitize() { 
		if (version != settings{}.version) {
			*this = settings{};
			return;
		}
		if (!(max_export >= 0) || std::isinf(max_export)) // also catches nan from uninitialized flash
			max_export = 0;
		if (!(filter_alpha >= 0 && filter_alpha < 1))
			filter_alpha = .1f;
		configured_inverters.sanitize(); 
		inverter_bat_prio.sanitize();
		if (inverter_bat_prio.size() < configured_inverters.size())
			for (int i: range(inverter_bat_prio.size(), configured_inverters.size()))
				inverter_bat_prio[i] = 1;
		inverter_bat_prio.resize(configured_inverters.size());
		for (int &p: inverter_bat_prio)
			p = std::clamp(p, 1, 1000);
		power_offset = std::clamp(power_offset, -1000, 1000);
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
		os << " battery priority " << (i < s.inverter_bat_prio.size() ? s.inverter_bat_prio[i]: 1) << '\n';
	}
	os << "configured_meter: ";
	ip_to_stream(os, s.configured_meter);
	os << "\npower_offset: " << s.power_offset;
	os << ", emm control enabled: " << (s.enable_emm ? "true": "false") << ", invert home: " << (s.invert_home ? "true": "false");
	os << ", filter alpha: " << s.filter_alpha << ", max export: " << s.max_export << "W";
	return os << '\n';
}

/** @brief parses a single key, value pair from the istream */
inline std::istream& operator>>(std::istream &is, settings &s) {
	// expected format: a.b.c.d:port|modbus_id
	const auto parse_ip = [](std::string_view ip, ModbusTcpAddr &conf_ip) {
		conf_ip = {};
		std::string_view host = extract_word(ip, ':'); // ip is now port|modbus_id
		std::string_view cur;
		for (int shift = 24; shift >= 0 && extract_word(host, cur, '.'); shift -= 8)
			conf_ip.ip |= uint32_t(std::clamp(to_int(cur).value_or(0), 0, 255)) << shift;
		conf_ip.port = std::clamp(to_int(extract_word(ip, '|')).value_or(0), 0, 0xffff);
		conf_ip.modbus_id = std::clamp(to_int(ip).value_or(0), 0, 255);
	};
	std::string key;
	std::string ip;
	is >> key;
	if (key == "configure_inverter") {
		is >> ip;
		ModbusTcpAddr *conf_ip = s.configured_inverters.push();
		if (!conf_ip)
			is.setstate(std::ios_base::failbit);
		else
			parse_ip(ip, *conf_ip);
	} else if (key == "configure_meter") {
		is >> ip;
		parse_ip(ip, s.configured_meter);

	} else if (key == "inverter_prio") {
		// expected format: inverter_index priority (index as listed by status, priority >= 1)
		int idx{-1}, prio{};
		is >> idx >> prio;
		if (!is || idx < 0 || idx >= s.configured_inverters.size() || prio < 1) {
			is.setstate(std::ios_base::failbit);
		} else {
			s.sanitize(); // makes sure the priorities have the same size as the inverters
			s.inverter_bat_prio[idx] = prio;
		}
	} else if (key == "power_offset") {
		is >> s.power_offset;
		s.sanitize();
	} else
		is.setstate(std::ios_base::failbit);
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

