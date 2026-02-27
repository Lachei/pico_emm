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
	uint16_t col{0};
	uint16_t background_col{0xffff};
};
enum SourceSinkType{ HOUSE, GRID, SMART_METER, INVERTER, PV, BATTERY };
enum Direction{UP, RIGHT, DOWN, LEFT};
struct EnergyBlobInfo {
	float energy;
	float x, y;
	int end_device_id;
	uint16_t col;
	Direction dir;
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
	bool show(Draw &draw, int x_offset) { return (*this)(draw, x_offset); };
	// sets the button state, press is can then be checke with next Button() call
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
	bool is_selected() const;
};
constexpr int HOME_ID = 0;
constexpr int GRID_ID = 1;
constexpr int METER_ID = 2;
inline int get_next_device_id() {static int cur_id{METER_ID}; return ++cur_id;}
struct TimeInfo {
	uint32_t ms;
	uint32_t delta_ms;
};
struct PowerInfo {
	int device_id;
	float imp_w;
	float exp_w;
};
struct EnergyInfo {
	int device_id;
	bool is_inverter;
	float imp_ws;
	float exp_ws;
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
	uint32_t d_last_spawn_ms{};
	EnergyInfo home_energy_info{.device_id = HOME_ID};
	EnergyInfo meter_energy_info{.device_id = METER_ID};
	static_vector<EnergyInfo, 32> energy_infos{};
	static_vector<EnergyBlobInfo, 128> energy_blobs{};

	void draw(Draw &display, TimeInfo time_info, float x_offset, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter);
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
};

enum HistoryPageType { DAILY, WEEKLY, MONTHLY, YEARLY, HISTORY_PAGE_COUNT };
enum VisType { POWER_PER_COMPONENT, POWER_BALANCE };
struct CurveInfo {
	std::string_view name{};
	std::string_view unit_name{};
	std::string_view time_start{};
	std::string_view time_end{};
	uint16_t color{};
	float min_val{};
	float max_val{};
	static_vector<int16_t, 512> data{};
};
struct CurveScale {
	std::string_view unit_name{};
	float min_value{};
	float max_value{};
};
struct HistoryPage {
	float base_offset{240};
	HistoryPageType history_page{DAILY};
	VisType vis_type{POWER_PER_COMPONENT};
	static_vector<CurveScale, 12> curve_scales{};
	CurveInfo derived_curve{}; // used eg. for net import/export

	SelectedHistory selected_history{SelectedHistory::DAY};
	Button day_button{{20, 30, 40, 15}, "Tag", ButtonStyle::BORDER};
	Button month_button{{65, 30, 40, 15}, "Monat"};
	Button year_button{{110, 30, 40, 15}, "Jahr"};
	std::array<Button*, 3> time_buttons{&day_button, &month_button, &year_button};
	Button net_power_button{{190, 30, 30, 15}, "Net W"};

	void draw(Draw &display, TimeInfo time_info, float x_offset, std::span<CurveInfo> curve_infos);
	bool handle_touch_input(TouchInfo &touch_info, int x_offset);
};

struct IpButton {
	Button button;
	static_string<4> str{};
};
struct settings;
struct SettingsPage {
	float base_offset{480};
	
	IpButton ip1{.button = {{10, 180, 30, 15}, "...", ButtonStyle::BORDER}};
	IpButton ip2{.button = {{50, 180, 30, 15}, "..."}};
	IpButton ip3{.button = {{90, 180, 30, 15}, "..."}};
	IpButton ip4{.button = {{130, 180, 30, 15}, "..."}};
	std::array<IpButton*, 4> ip_buttons{&ip1, &ip2, &ip3, &ip4};
	IpButton *selected_ip{&ip1};
	Button configure_button{{170, 180, 60, 15}, "Hinzufügen"};

	Button i1{{10, 210, 15, 15}, "1"};
	Button i2{{30, 210, 15, 15}, "2"};
	Button i3{{50, 210, 15, 15}, "3"};
	Button i4{{70, 210, 15, 15}, "4"};
	Button i5{{90, 210, 15, 15}, "5"};
	Button i6{{110, 210, 15, 15}, "6"};
	Button i7{{130, 210, 15, 15}, "7"};
	Button i8{{150, 210, 15, 15}, "8"};
	Button i9{{170, 210, 15, 15}, "9"};
	Button i0{{190, 210, 15, 15}, "0"};
	Button ix{{210, 210, 15, 15}, "x"};
	std::array<Button*, 11> number_inputs{&i1, &i2, &i3, &i4, &i5, &i6, &i7, &i8, &i9, &i0, &ix};

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

