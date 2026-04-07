#pragma once

#include <iostream>

#include "log_storage.h"
#include "settings.h"
#include "measurements.h"
#include "wifi_storage.h"
#include "access_point.h"
#include "inverter.h"
#include "meter.h"

// handle exactly one command from the input stream at a time (should be called in an endless loop)
static constexpr inline void handle_usb_command(std::istream &in = std::cin, std::ostream &out = std::cout) {
	const auto print_logs = [&out]{
		for (const auto& log: log_storage::Default().logs) {
			switch(log.severity) {
			case log_severity::Info   : out << "[Info   ]: "; break;
			case log_severity::Warning: out << "[Warning]: "; break;
			case log_severity::Error  : out << "[Error  ]: "; break;
			case log_severity::Fatal  : out << "[Fatal  ]: "; break;
			}
			out << log.message.sv() << '\n';
		}
	};

	std::string command;
	in >> command;
	if (command.empty() || command == "h" || command == "-h" || command == "--help" || command == "help") {
		out << "Device controlling the powerstages for a dc-dc converter\n";
		out << "The following commands are available to edit the state of the device:\n\n";
		out << "  h|-h|--help|help\n";
		out << "    Prints This help menu\n\n";
		out << "  status\n";
		out << "    Prints the status of the iot device, including measurement values, setting values, error state, wifi status\n\n";
		out << "  set ${variable} ${value}\n";
		out << "    Set the value of a variable. Available variables are:\n";
		out << "      Variable1\n";
		out << "      Variable2\n";
		out << "      Variable3\n\n";
		out << "  enable_wifi|ew\n";
		out << "    Activate wifi on the device\n\n";
		out << "  disable_wifi|dw\n";
		out << "    Disable wifi on the device\n\n";
		out << "  enable_ap|ea\n";
		out << "    Activate the acces point on the device (also activates wifi if disabled)\n\n";
		out << "  disable_ap|da\n";
		out << "    Disable the acces point on the device (leafs the other wifi untouched)\n\n";
		out << "  connect_wifi ${ssid} ${password}|cw\n";
		out << "    Store the wifi credentials for a certain ssid and connect if its available\n\n";
		out << "  set_log_level (info|warning|error|fatal)|sll\n";
		out << "    Set the log level to the specified value\n\n";;
		out << "  log|l\n";
		out << "    Print the log storage to the console\n\n";
		out << "  logs|ls\n";
		out << "    Print the log storage with a separator line to the console\n\n";
		out << "  s\n";
		out << "    Print a separator line with dashes\n\n";
	} else if (command == "status") {
		out << "-------------\n";
		out << "measurements:\n";
		out << measurements::Default();
		out << "-------------\n";
		out << "settings:\n";
		out << settings::Default();
		out << "-------------\n";
		out << "Realtime data:\n";
		out << "Meter " << g::meter().name.sv() << ": " << g::meter().power_info.imp_w - g::meter().power_info.exp_w << "W\n";
		for (int i: range(g::inverters().read_power.size())) {
			const InverterGroup &ig = g::inverters().read_power[i];
			out << g::inverters().connected_names[i].sv() << ": Inverter(" << -ig.inverter.imp_w + ig.inverter.exp_w << "), PV(" << ig.pv.exp_w << "), Battery(" << -ig.battery.imp_w + ig.battery.exp_w << ", Soc " << ig.bat_soc << ")\n";
		}
		out << "-------------\n";
		out << "wifi:\n";
		out << wifi_storage::Default();
		out << "Access point active: " << (access_point::Default().active ? "true": "false") << '\n';
	} else if (command == "set") {
		in >> settings::Default(); // sets fail bit on error
		if (!in)
			out << "Error at setting the value\n";
		in.clear();
	} else if (command == "enable_wifi" || command == "ew") {
		cyw43_arch_enable_sta_mode();
	} else if (command == "disable_wifi" || command == "dw") {
		cyw43_arch_disable_sta_mode();
	} else if (command == "enable_ap" || command == "ea") {
		access_point::Default().init();
	} else if (command == "disable_ap" || command == "da") {
		access_point::Default().deinit();
	} else if (command == "connect_wifi" || command == "cw") {
		std::string ssid, pwd;
		in >> ssid >> pwd;
		wifi_storage::Default().ssid_wifi.fill(ssid);
		wifi_storage::Default().pwd_wifi.fill(pwd);
		wifi_storage::Default().wifi_connected = false;
		wifi_storage::Default().wifi_changed = true;
		if (PICO_OK != persistent_storage_t::Default().write(
			wifi_storage::Default().ssid_wifi, &persistent_storage_layout::ssid_wifi))
			LogError("Failed to store ssid_wifi");
		if (PICO_OK != persistent_storage_t::Default().write(
			wifi_storage::Default().pwd_wifi, &persistent_storage_layout::pwd_wifi))
			LogError("Failed to store pwd_wifi");
	} else if (command == "set_log_level" || command == "sll") {
		std::string level;
		in >> level;
		if      (level == "info") log_storage::Default().cur_severity = log_severity::Info;
		else if (level == "warning") log_storage::Default().cur_severity = log_severity::Warning;
		else if (level == "error") log_storage::Default().cur_severity = log_severity::Error;
		else if (level == "fatal") log_storage::Default().cur_severity = log_severity::Fatal;
		else out << "[ERROR] severity " << level << " not allowed. Allowed values are: info|warning|error|fatal\n";
	} else if (command == "log" || command == "l") {
		print_logs();
	} else if (command == "logs" || command == "ls") {
		out << "--------------------------------------\n";
		print_logs();
	} else if (command == "s") {
		out << "--------------------------------------\n";
	} else {
		out << "[ERROR] Command " << command << " unknown. Run command 'help' for a list of all available commands\n";
	}
}

