#pragma once

#include <span>

#include "libraries/pico_vector/pico_vector.hpp"
#include "static_types.h"

using Point = pimoroni::Point;
using Rect = pimoroni::Rect;
using Draw = pimoroni::PicoGraphics_PenRGB565;
using DrawVec = pimoroni::PicoVector;
using Mat = pp_mat3_t;
using RGB = pimoroni::RGB;

inline Point operator*(const Point &p, float s) {return {int(p.x * s), int(p.y * s)};}
struct Line {
	Point start;
	Point end;
};
inline Rect operator+(const Rect &r, const Point &p) {return {r.x + p.x, r.y + p.y, r.w, r.h};}
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

struct TouchInfo {
    Point touch_start;
    std::optional<Point> last_touch;
    std::optional<Point> cur_touch;
    constexpr bool touch_started() const {return !last_touch;}
    constexpr bool touch_ended() const {return !cur_touch;}
};
enum struct ButtonState {NONE, HOVERED, PRESSED};
enum struct ButtonStyle {DEFAULT, BORDER};
struct Button {
	Rect pos;
	std::string_view text;
	ButtonStyle style{ButtonStyle::DEFAULT};
	ButtonState state{ButtonState::NONE};
	
	RGB col{0xff, 0xff, 0xff};
	RGB back_col{0x04, 0xAA, 0x6D};
	RGB hover_col{0x04, 0x8A, 0x2D};
	RGB border_col{0x00, 0x00, 0x00};
	
	bool operator()(Draw &draw, int x_offset);
	// sets the button state, press is can then be checke with next Button() call
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
};
struct TimeInfo {
	uint32_t ms;
	uint32_t delta_ms;
};
struct PowerInfo{
	float imp_w;
	float exp_w;
};
struct InverterGroup {
	PowerInfo inverter, pv, battery;
};
enum struct SelectedHistory {DAY = 0, MONTH, YEAR, COUNT};
struct OverviewPage {
	float base_offset{};
	float target_y_offset{};
	float ig_view_height{};
	bool drag_ig_view{};
	float y_offset{};
	float inverter_scroll{};
	static_vector<EnergyBlobInfo, 32> energy_blobs{};

	void draw(Draw &display, TimeInfo time_info, float x_offset, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter);
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
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

	SelectedHistory selected_history{SelectedHistory::DAY};
	Button day_button{{20, 50, 40, 15}, "Tag", ButtonStyle::BORDER};
	Button month_button{{65, 50, 40, 15}, "Monat"};
	Button year_button{{110, 50, 40, 15}, "Jahr"};
	std::array<Button*, int(SelectedHistory::COUNT)> buttons{&day_button, &month_button, &year_button};

	void draw(Draw &display, TimeInfo time_info, float x_offset, std::span<CurveInfo> curve_infos);
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
};

struct settings;
struct SettingsPage {
	float base_offset{480};
	void draw(Draw &display, TimeInfo time_info, float x_offset, settings& settings);
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
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

