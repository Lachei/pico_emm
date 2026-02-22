#include "draw.h"
#include "log_storage.h"

#define RECT(x, y, width, height) Line{{x,y}, {x + width, y}}, \
				  Line{{x + width,y}, {x + width, y + height + 4}}, \
				  Line{{x + width,y + height}, {x, y + height}}, \
				  Line{{x,y + height}, {x, y}}
#define TRAY(x, y, width, height) Line{{x + width,y}, {x + width, y + height + 4}}, \
				  Line{{x + width,y + height}, {x, y + height}}, \
				  Line{{x,y + height}, {x, y}}

// mainly made because the pico_vector circle was tiling when clipping was set
constexpr void draw_hq_circle(Draw &draw, Point center, int height) {
	draw.circle(center, height / 2); // core
	// shade
	height += 2;
	Point prev_b{};
	for (float i = 0; i < 2 * std::numbers::pi; i += std::numbers::pi / height / 2) {
		float x = std::sin(i) * height / 2 + center.x;
		float y = std::cos(i) * height / 2 + center.y;
		if (x < 0 || x > 240 || y < 0 || y > 240)
			continue;
		Point b{int(x), int(y)};
		if (x < center.x) b.x += 1;
		if (y < center.y) b.y += 1;
		if (b == prev_b) continue;
		prev_b = b;
		float x_d = x - b.x;
		float y_d = y - b.y;
		float alpha = std::sqrt(x_d * x_d + y_d * y_d);
		int a = int(std::clamp(alpha, .0f, 1.f) * 255);
		if (draw.clip.contains(b)) // set_pixel_alpha ignores clip
			draw.set_pixel_alpha(b, a);
	}
}
template<unsigned int N>
constexpr void draw_lines(Draw &draw, Point center, int height, DrawSettings draw_settings, const std::array<Line, N> &lines) {
	if (draw_settings.draw_background) {
		draw.set_pen(draw_settings.background_col.r, draw_settings.background_col.g, draw_settings.background_col.b);
		draw_hq_circle(draw, center, height);
	}
	draw.set_pen(draw_settings.col.r, draw_settings.col.g, draw_settings.col.b);
	for (const Line& line: lines)
		draw.line(center + line.start * height * .01, center + line.end * height * .01);
}

void draw_home(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 6> lines{
		TRAY(-25, -10, 50, 40),
		Line{{-40, 7}, {04, -35}},
		Line{{40, 5}, {00, -35}},
	};
	draw_lines(draw, center, height, draw_settings, lines);
}
void draw_electric_pole(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 7> lines{
		Line{{-20, 30}, {00, -40}},
		Line{{20, 30}, {00, -40}},
		Line{{-20, 30}, {10, 00}},
		Line{{20, 30}, {-10, 00}},
		Line{{-10, 00}, {10, 00}},
		Line{{-30, -15}, {30, -15}},
		Line{{-20, -30}, {20, -30}},
	};
	draw_lines(draw, center, height, draw_settings, lines);
}
void draw_smart_meter(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 8> lines{
		RECT(-30, -30, 60, 60),
		RECT(-20, -10, 40, 20),
	};
	draw_lines(draw, center, height, draw_settings, lines);
}
void draw_inverter(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 7> lines{
		RECT(-25, -30, 50, 60),
		Line{{-10, 20}, {10, -20}},
		Line{{-15, -15}, {-05, -15}},
		Line{{-15, -10}, {-05, -10}},
		//ppp_arc({1, 3, .r = 1.5, .s = s, .f = 225, .t = 115}),
		//ppp_arc({2, 1, .r = 1.5, .s = s, .f = 435, .t = 315}),
	};
	draw_lines(draw, center, height, draw_settings, lines);
}
void draw_pv(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 6> lines{
		Line{{-20, -20}, {20, -20}},
		Line{{-40, 20},  {40, 20}},
		Line{{-20, -20}, {-40, 20}},
		Line{{20, -20},  {40, 20}},
		Line{{-30, 00}, {30, 0}},
		Line{{0, -20},  {0, 20}},
	};
	draw_lines(draw, center, height, draw_settings, lines);
}
void draw_battery(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 5> lines{
		RECT(-15,  -30, 30, 60),
		Line{{-10, -35}, {10, -35}}
	};
	draw_lines(draw, center, height, draw_settings, lines);
}

bool Button::operator()(Draw &draw, int x_offset) {
	// drawing stuff
	Rect pos = this->pos + Point{x_offset, 0};
	if (state == ButtonState::HOVERED)
		draw.set_pen(hover_col.r, hover_col.g, hover_col.b);
	else
		draw.set_pen(back_col.r, back_col.g, back_col.b);
	draw.rectangle(pos);
	if (style == ButtonStyle::BORDER) {
		draw.set_pen(border_col.r, border_col.g, border_col.b);
		draw.line({pos.x, pos.y}, {pos.x + pos.w, pos.y});
		draw.line({pos.x + pos.w, pos.y}, {pos.x + pos.w, pos.y + pos.h + 1});
		draw.line({pos.x + pos.w, pos.y + pos.h}, {pos.x, pos.y + pos.h});
		draw.line({pos.x, pos.y + pos.h}, {pos.x, pos.y});
	}
	int text_width = draw.measure_text(text, 1);
	constexpr int TEXT_HEIGHT = 10;
	draw.set_pen(0xffffffff);
	draw.text(text, {pos.x + pos.w / 2 - text_width / 2, pos.y + pos.h / 2 - TEXT_HEIGHT / 2 + 1}, pos.w, 1);
	// do press handling
	if (state == ButtonState::PRESSED) {
		state = ButtonState::NONE;
		return true;
	}
	return false;
}

bool Button::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	Rect pos = this->pos + Point{x_offset, 0};
	bool in_button = touch_info.cur_touch ? pos.contains(*touch_info.cur_touch): 
						pos.contains(touch_info.last_touch.value_or(Point{-1, -1}));
	if (!in_button) {
		state = ButtonState::NONE;
		return false;
	}
	if (touch_info.touch_ended()) {
		state = ButtonState::PRESSED;
		return true;
	}
	if (touch_info.cur_touch)
		state = ButtonState::HOVERED;
	return false;
}

constexpr int ICON_HEIGHT = 30;
constexpr int IG_HEIGHT = 90;
constexpr int Y_BUS = 160;
constexpr int Y_METER = Y_BUS;
constexpr int Y_GRID = Y_METER + 50;
constexpr int Y_HOME = Y_BUS - 60;
constexpr int Y_PV_OFF = -70;
constexpr int Y_BATTERY_OFF = -20;
constexpr int Y_INVERTER_OFF = -IG_HEIGHT / 2;
constexpr int X_METER = 200;
constexpr int X_GRID = 200;
constexpr int X_HOME = 200;
constexpr int X_HOME_CONN = X_HOME - 50;
constexpr int X_PV = 25;
constexpr int X_BATTERY = X_PV;
constexpr int X_INVERTER = 50;
constexpr int X_INV_CONN = 80;
static const Rect IG_VIEW_BOX = {0, 40, X_INV_CONN + 2, 200};
void OverviewPage::draw(Draw &draw, TimeInfo time_info, float x_off, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter) {
	y_offset = .8 * y_offset + .2 * target_y_offset;
	int y_offset = static_cast<int>(this->y_offset);
	int x_offset = static_cast<int>(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

        Point pole_pos{X_GRID + x_offset, Y_GRID};
        Point meter_pos{X_METER + x_offset, Y_METER};
        Point home_pos{X_HOME + x_offset, Y_HOME};

	// draw paths
	draw.line({X_INV_CONN + x_offset, Y_BUS}, {X_METER + x_offset, Y_BUS});
	draw.line({X_METER + x_offset, Y_BUS}, {X_METER + x_offset, Y_GRID});
	draw.line({X_HOME_CONN + x_offset, Y_BUS}, {X_HOME_CONN + x_offset, Y_HOME});
	draw.line({X_HOME_CONN + x_offset, Y_HOME}, {X_HOME + x_offset, Y_HOME});
	ig_view_height = 0;
	if (inverter_groups.empty()) {
		draw.text("Wechselrichter mit x, is wohl nix", {10 + x_offset, Y_BUS - 10}, 40, 1);
	} else {
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		int max_y = Y_BUS;
		int min_y = Y_BUS;
		draw.set_clip(IG_VIEW_BOX + Point{x_offset, 0});
		for (const InverterGroup &ig: inverter_groups) {
			int y_base = static_cast<int>(y_base_f);
			int y_conn = y_base + Y_INVERTER_OFF + y_offset;
			draw.line({X_PV + x_offset, y_base + Y_PV_OFF + y_offset}, 
					{X_INVERTER + x_offset, y_base + Y_PV_OFF + y_offset});
			draw.line({X_INVERTER + x_offset, y_base + Y_PV_OFF + y_offset}, 
					{X_INVERTER + x_offset, y_conn});
			draw.line({X_BATTERY + x_offset, y_base + Y_BATTERY_OFF + y_offset}, 
					{X_INVERTER + x_offset, y_base + Y_BATTERY_OFF + y_offset});
			draw.line({X_INVERTER + x_offset, y_base + Y_BATTERY_OFF + y_offset + 1}, 
					{X_INVERTER + x_offset, y_conn});
			draw.line({X_INVERTER + x_offset, y_conn},
					{X_INV_CONN + x_offset, y_conn});
			max_y = std::max(max_y, y_conn);
			min_y = std::min(min_y, y_conn);
			y_base_f -= IG_HEIGHT;
		}
		ig_view_height = (max_y - min_y + IG_HEIGHT) / 2;
		if (min_y != max_y)
			draw.line({X_INV_CONN + x_offset, min_y}, {X_INV_CONN + x_offset, max_y + 1});
		draw.remove_clip();
	}

	// draw icons
        draw_home(draw, home_pos, ICON_HEIGHT, {.background_col = {255,210,100}});
        draw_electric_pole(draw, pole_pos, ICON_HEIGHT, {.col = {255, 255, 255}, .background_col = {100,100,255}});
        draw_smart_meter(draw, meter_pos, ICON_HEIGHT, {.background_col = {200,200,200}});
	if (inverter_groups.size()) {
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		draw.set_clip(IG_VIEW_BOX + Point{x_offset, 0});
		for (const InverterGroup &ig: inverter_groups) {
			int y_base = static_cast<int>(y_base_f);
			Point p{X_PV + x_offset, y_base + Y_PV_OFF + y_offset};
			draw_pv(draw, p, ICON_HEIGHT, {.background_col = {255,255,100}});
			p = {X_INVERTER + x_offset, y_base + Y_INVERTER_OFF + y_offset};
			draw_inverter(draw, p, ICON_HEIGHT, {.background_col = {255,100,100}});
			p = {X_BATTERY + x_offset, y_base + Y_BATTERY_OFF + y_offset};
			draw_battery(draw, p, ICON_HEIGHT, {.background_col = {50,255,50}});
			y_base_f -= IG_HEIGHT;
		}
		draw.remove_clip();
	}
}
bool OverviewPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	if (touch_info.touch_started() && (IG_VIEW_BOX + Point{x_offset, 0}).contains(*touch_info.cur_touch)) {
		drag_ig_view = true;
		return true;
	}
	if (touch_info.touch_ended()) {
		target_y_offset = std::clamp(target_y_offset, -ig_view_height / 2, ig_view_height / 2);
		drag_ig_view = false;
		return false;
	}
	if (drag_ig_view && touch_info.cur_touch && touch_info.last_touch) {
		target_y_offset += touch_info.cur_touch->y - touch_info.last_touch->y;
		return true;
	}
	return false;
}

void HistoryPage::draw(Draw &draw, TimeInfo time_info, float x_off, std::span<CurveInfo> curve_infos) {
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;
	if (day_button(draw, x_offset)) {
		selected_history = SelectedHistory::DAY;
		for (Button *b: buttons)
			b->style = ButtonStyle::DEFAULT;
		day_button.style = ButtonStyle::BORDER;
	}
	if (month_button(draw, x_offset)) {
		selected_history = SelectedHistory::MONTH;
		for (Button *b: buttons)
			b->style = ButtonStyle::DEFAULT;
		month_button.style = ButtonStyle::BORDER;
	}
	if (year_button(draw, x_offset)) {
		selected_history = SelectedHistory::YEAR;
		for (Button *b: buttons)
			b->style = ButtonStyle::DEFAULT;
		year_button.style = ButtonStyle::BORDER;
	}
}
bool HistoryPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	for (Button *b: buttons) {
		if (b->handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	}
	return false;
}

void SettingsPage::draw(Draw &draw, TimeInfo time_info, float x_offset, settings& settings) {

}
bool SettingsPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	return false;
}

