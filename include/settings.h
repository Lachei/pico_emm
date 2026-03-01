#pragma once

#include <iostream>

#include "static_types.h"
#include "ranges_util.h"
#include "string_util.h"

inline bool request_settings_store{};
inline bool request_settings_load{};
struct settings {
	static_vector<uint32_t, 32> configured_ips{};

	static settings& Default() {
		static settings s{};
		return s;
	}
	/** @brief writes the settings struct as json to the static strig s */
	template<int N>
	constexpr void dump_to_json(static_string<N> &s) const {
		s.append(R"({{"configured_ips":[)");
		for (int i: range(configured_ips.size())) {
			if (i != 0)
				s.append(',');
			s.append_formatted(R"("{}.{}.{}.{}")", configured_ips[i] >> 24, (configured_ips[i] >> 16) & 0xff, (configured_ips[i] >> 8) & 0xff, configured_ips[i] & 0xff);
		}
		s.append("]}");
	}

	constexpr void sanitize() { configured_ips.sanitize(); }
};

/** @brief prints formatted for monospace output, eg. usb */
inline std::ostream& operator<<(std::ostream &os, const settings &s) {
	const auto ip_to_stream = [](std::ostream &os, uint32_t ip) {
		os << (ip >> 24) << '.' << ((ip >> 16) & 0xff) << '.' << ((ip >> 8) & 0xff) << '.' << (ip & 0xff);
	};
	os << "configured_ips:   ";
	for (int i: range(s.configured_ips.size())) {
		if (i != 0)
			os << "\n                  ";
		ip_to_stream(os, s.configured_ips[i]);
	}
	return os << '\n';
}

/** @brief parses a single key, value pair from the istream */
inline std::istream& operator>>(std::istream &is, settings &s) {
	std::string key;
	std::string ip;
	is >> key;
	if (key == "configure_ip") {
		is >> ip;
		std::string_view ip_view = ip;
		uint32_t *conf_ip = s.configured_ips.push();
		if (!conf_ip)
			is.fail();
		else {
			*conf_ip = 0;
			std::string_view cur;
			for (int shift = 24; extract_word(ip_view, cur, ',') && shift >= 0; shift -= 8)
				*conf_ip |= to_int(cur).value_or(0) << shift;
		}
	} else
		is.fail();
	return is;
}

struct IpName {
	uint32_t ip{};
	static_string<32> name{"hello"};
};
struct runtime_state {
	static runtime_state& Default() {
		static runtime_state r{};
		return r;
	}
	static_vector<IpName, 32> found_ips{};
};

