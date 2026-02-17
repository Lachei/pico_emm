#pragma once

#include <span>

#include "libraries/pico_vector/pico_vector.hpp"
#include "static_types.h"

using DisplayHQ = pimoroni::PicoVector;
using Display = pimoroni::PicoGraphics_PenRGB565;
using Mat = pp_mat3_t;
using RGB = pimoroni::RGB;

struct DrawSettings {
	bool draw_background{true};
	RGB col{0, 0, 0};
	RGB background_col{255, 255, 255};
};
enum SourceSinkType{ HOUSE, GRID, SMART_METER, INVERTER, PV, BATTERY };
struct EnergyBlobInfo {
	float energy;
	float x, y;
	SourceSinkType start_type;
	SourceSinkType end_type;
	uint32_t end_type_id;
};

// simple drawing functions
void draw_home(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});
void draw_electric_pole(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});
void draw_smart_meter(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});
void draw_inverter(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});
void draw_pv(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});
void draw_battery(DisplayHQ &display, Mat &transform, DrawSettings draw_settings = {});

// full page drawing structures, structures as some context is needed
struct TouchInfo;
struct TimeInfo {
	uint32_t ms;
	uint32_t delta_ms;
};
struct PowerInfo{
	float imp;
	float exp;
};
struct InverterGroup {
	PowerInfo inverter, pv, battery;
};
struct OverviewPage {
	float base_offset{};
	float inverter_scroll{};
	static_vector<EnergyBlobInfo, 32> energy_blobs{};

	void draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter);
	bool handle_touch_input(TouchInfo &touch_info);
};

enum HistoryPageType { DAILY, WEEKLY, MONTHLY, YEARLY, HISTORY_PAGE_COUNT };
enum VisType { POWER_PER_COMPONENT, POWER_BALANCE };
struct CurveInfo {
	std::string_view name;
	std::string_view unit_name;
	static_vector<uint16_t, 512> data_finfos[HISTORY_PAGE_COUNT];
};
struct CurveScale {
	std::string_view unit_name;
	float min_value{};
	float max_value{};
};
struct HistoryPage {
	float base_offset{240};
	HistoryPageType history_page{DAILY};
	VisType vis_type{POWER_PER_COMPONENT};
	static_vector<CurveScale, 12> curve_scales{};

	void draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, std::span<CurveInfo> curve_infos);
	bool handle_touch_input(TouchInfo &touch_info);
};

struct settings;
struct SettingsPage {
	float base_offset{480};
	void draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, settings& settings);
	bool handle_touch_input(TouchInfo &touch_info);
};

// singletons for main drawing (safe reduction of stack allocation)

inline OverviewPage& overview_page() {
	static OverviewPage page{};
	return page;
}

inline HistoryPage& history_page() {
	static HistoryPage page{};
	return page;
}

inline SettingsPage& settings_page() {
	static SettingsPage page{};
	return page;
}

