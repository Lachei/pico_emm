#include "FreeRTOS.h"
#include "draw.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "ntp_client.h"
#include "settings.h"
#include "wifi_storage.h"
#include "history_data.h"

#define RECTE(x, y, width, height, e) Line{{x,y}, {x + width, y}}, \
				  Line{{x + width,y}, {x + width, y + height + e}}, \
				  Line{{x + width,y + height}, {x, y + height}}, \
				  Line{{x,y + height}, {x, y}}
#define RECT(x, y, width, height) RECTE(x, y, width, height, 4)
#define TRAY(x, y, width, height) Line{{x + width,y}, {x + width, y + height + 4}}, \
				  Line{{x + width,y + height}, {x, y + height}}, \
				  Line{{x,y + height}, {x, y}}

// mainly made because the pico_vector circle was tiling when clipping was set
constexpr void draw_hq_circle(Draw &draw, Point center, int height) {
	draw.circle(center, height / 2); // core
	if (height <= 2)
		return;
	// shade
	height += height > 4 ? 2: 1;
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
		draw.set_pen(draw_settings.background_col);
		draw_hq_circle(draw, center, height);
	}
	draw.set_pen(draw_settings.col);
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
void draw_battery(Draw &draw, Point center, int height, float soc, DrawSettings draw_settings) {
	static std::array<Line, 5> lines{
		RECT(-15,  -30, 30, 60),
		Line{{-10, -40}, {10, -40}}
	};
	draw_lines(draw, center, height, draw_settings, lines);
	float m = height * .01;
	draw.rectangle(Rect{int(center.x - 5 * m), int(center.y - (22 - 56 * (100 - soc) / 100) * m), int(18 * m), int(54 * soc / 100 * m) });
}

void sort_energy(const static_vector<EnergyBlobInfo, 256> &energy_blobs, static_vector<uint8_t, 256> &blobs_sorted) {
	static std::array<uint8_t, 256> histogram;
	histogram = {};
	blobs_sorted.resize(energy_blobs.size());
	const auto to_bucket = [] (const EnergyBlobInfo &e) { return 255 - uint8_t(std::min(e.energy / 15000 * 255, 255.f)); };
	for (const EnergyBlobInfo &b: energy_blobs)
		histogram[to_bucket(b)] += 1;
	// exclusive scan
	int cur{};
	for (uint8_t &h: histogram) {
		int pre = cur;
		cur += h;
		h = pre;
	}
	// scatter
	for (int i: range(energy_blobs.size()))
		blobs_sorted[histogram[to_bucket(energy_blobs[i])]++] = i;
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

bool Button::is_selected() const { return style == ButtonStyle::BORDER; }

constexpr uint16_t COL_HOME = RGB{255,210,100}.to_rgb565();
constexpr uint16_t COL_POLE = RGB{100,100,255}.to_rgb565();
constexpr uint16_t COL_METER = RGB{200,200,200}.to_rgb565();
constexpr uint16_t COL_PV = RGB{255,255,100}.to_rgb565();
constexpr uint16_t COL_INVERTER = RGB{255,100,100}.to_rgb565();
constexpr uint16_t COL_BATTERY = RGB{50,255,50}.to_rgb565();
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
constexpr int BUBBLE_SPAWN_MS = 1000;
static const Rect IG_VIEW_BOX = {0, 40, X_INV_CONN + 2, 200};
static const Rect BUBBLE_VIEW_BOX = {0, 40, 240, 200};
void OverviewPage::draw(Draw &draw, TimeInfo time_info, float x_off, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter) {
	y_offset = .8 * y_offset + .2 * target_y_offset;

	// integrate power and generate new dots
	d_last_spawn_ms += time_info.delta_ms;
	bool spawn_bubbles = d_last_spawn_ms > BUBBLE_SPAWN_MS;
	if (spawn_bubbles)
		d_last_spawn_ms -= BUBBLE_SPAWN_MS;
	float ds = time_info.delta_ms / 1000.f;


	tot_imp_wh += meter.imp_w * ds / 60 / 60;
	tot_exp_wh += meter.exp_w * ds / 60 / 60;
	home_energy_info.imp_ws += home.imp_w * ds;
	home_energy_info.exp_ws += home.exp_w * ds;
	meter_energy_info.imp_ws += meter.exp_w * ds; // is swapped on purpose
	meter_energy_info.exp_ws += meter.imp_w * ds;
	if (spawn_bubbles && meter_energy_info.imp_ws > 5) {
		energy_blobs.push({.energy = meter_energy_info.imp_ws, .x = X_METER, .y = Y_METER, .end_device_id = GRID_ID, .col = COL_METER, .dir = Direction::DOWN});
	}
	if (spawn_bubbles && meter_energy_info.exp_ws > 5) {
		energy_blobs.push({.energy = meter_energy_info.exp_ws, .x = X_GRID, .y = Y_GRID, .end_device_id = METER_ID, .col = COL_POLE, .dir = Direction::UP});
	}
	const auto generate_bubbles = [&] (EnergyInfo &ei, float x_start, float y_start, uint16_t col) {
		// first distribute to meter and home, then to a inverter
		float rest = ei.exp_ws;
		ei.exp_ws = 0;
		float cur_e = std::min(rest, home_energy_info.imp_ws);
		if (cur_e > 5) {
			rest -= cur_e;
			home_energy_info.imp_ws -= cur_e;
			energy_blobs.push({.energy = cur_e, .x = x_start - 1, .y = y_start, .end_device_id = HOME_ID, .col = col, .dir = Direction::LEFT});
		}
		cur_e = std::min(rest, meter_energy_info.imp_ws);
		if (cur_e > 5) {
			rest -= cur_e;
			meter_energy_info.imp_ws -= cur_e;
			energy_blobs.push({.energy = cur_e, .x = x_start - 1, .y = y_start, .end_device_id = METER_ID, .col = col, .dir = Direction::LEFT});
		}
		for (EnergyInfo &ei: energy_infos) {
			if (!ei.is_inverter)
				continue;
			if (rest <= 5)
				return;
			cur_e = std::min(rest, ei.imp_ws);
			if (cur_e <= 5)
				continue;
			rest -= cur_e;
			ei.imp_ws -= cur_e;
			energy_blobs.push({.energy = cur_e, .x = x_start - 1, .y = y_start, .end_device_id = ei.device_id, .col = col, .dir = Direction::LEFT});
		}
	};
	if (spawn_bubbles) {
		generate_bubbles(meter_energy_info, X_METER, Y_METER, COL_METER);
		generate_bubbles(home_energy_info, X_HOME, Y_HOME, COL_HOME);
	}

	const auto integrate_power = [&] (const PowerInfo &pi, bool is_inverter = false) -> EnergyInfo* {
		if (pi.device_id == -1)
			return {};
		EnergyInfo *ei = energy_infos | find{&EnergyInfo::device_id, pi.device_id};
		if (!ei) {
			ei = energy_infos.push();
			if (!ei) {
				LogError("Could not allocate new energy info");
				return {};
			}
			*ei = {.device_id = pi.device_id, .is_inverter = is_inverter};
		}
		ei->imp_ws += pi.imp_w * ds;
		ei->exp_ws += pi.exp_w * ds;
		return ei;
	};
	{
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		int bubble_off = 1;
		for (InverterGroup &ig: inverter_groups) {
			EnergyInfo *e_inverter = integrate_power(ig.inverter, true);
			EnergyInfo *e_pv = integrate_power(ig.pv);
			EnergyInfo *e_battery = integrate_power(ig.battery);
			if (spawn_bubbles) {
				if (e_pv && e_pv->exp_ws > 5) {
					energy_blobs.push({.energy = e_pv->exp_ws, .x = X_PV + 1, .y = y_base_f + Y_PV_OFF, .end_device_id = ig.inverter.device_id, .col = COL_PV, .dir = Direction::RIGHT});
					e_pv->exp_ws = e_pv->imp_ws = 0;
				}
				if (e_battery && e_battery->exp_ws > 5) {
					energy_blobs.push({.energy = e_battery->exp_ws, .x = X_BATTERY + 1, .y = y_base_f + Y_BATTERY_OFF, .end_device_id = ig.inverter.device_id, .col = COL_BATTERY, .dir = Direction::RIGHT});
					e_battery->exp_ws = 0;
				}
				if (e_battery && e_battery->imp_ws > 5) {
					energy_blobs.push({.energy = e_battery->imp_ws, .x = X_INVERTER, .y = y_base_f + Y_INVERTER_OFF + 1, .end_device_id = ig.battery.device_id, .col = COL_INVERTER, .dir = Direction::DOWN});
					e_battery->imp_ws = 0;
				}
				// inverter has to match for another sink in the main loop
				// 1. Search for other inverter which has imported stuff
				// 2. Search home for import, search smart meter for export
				if (e_inverter && e_inverter->exp_ws > 5) {
					float x = X_INVERTER;
					float y = y_base_f + Y_INVERTER_OFF;
					float rest = e_inverter->exp_ws;
					e_inverter->exp_ws = 0;
					for (auto cur = energy_infos.begin(); rest > 5 && cur < energy_infos.end(); ++cur) {
						if (!cur->is_inverter)
							continue;
						if (cur->imp_ws <= 5)
							continue;
						float cur_e = std::min(rest, cur->imp_ws);
						rest -= cur_e;
						cur->imp_ws -= cur_e;
						energy_blobs.push({.energy = cur_e, .x = x + bubble_off, .y = y, .end_device_id = cur->device_id, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					float cur_e = std::min(rest, home_energy_info.imp_ws);
					if (cur_e > 5) {
						rest -= cur_e;
						home_energy_info.imp_ws -= cur_e;
						energy_blobs.push({.energy = cur_e, .x = x + bubble_off, .y = y, .end_device_id = HOME_ID, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					cur_e = std::min(rest, meter_energy_info.imp_ws);
					if (cur_e > 5) {
						rest -= cur_e;
						meter_energy_info.imp_ws -= cur_e;
						energy_blobs.push({.energy = cur_e, .x = x + bubble_off, .y = y, .end_device_id = METER_ID, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					if (rest > 100)
						LogWarning("Still got some juice: {}", rest);
					bubble_off += 2;
				}
			}
			y_base_f -= IG_HEIGHT;
		}
	}
	if (spawn_bubbles)
		meter_energy_info.imp_ws = 0;

	// advancing energy dots
	constexpr float SPEED = .03; // in pixels per miliseconds
	const float dist = SPEED * time_info.delta_ms;
	for (EnergyBlobInfo &blob: energy_blobs) {
		if (blob.x > 300 || blob.x < -100 || blob.y > 300 || blob.y < -100) {
			LogError("Removing stray dot");
			blob.end_device_id = -1;
			continue;
		}
		float goal_x = INFINITY, goal_y = INFINITY;
		if (blob.end_device_id == METER_ID) {goal_x = X_METER; goal_y = Y_METER;}
		if (blob.end_device_id == HOME_ID) {goal_x = X_HOME; goal_y = Y_HOME;}
		if (blob.end_device_id == GRID_ID) {goal_x = X_GRID; goal_y = Y_GRID;}
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		for (const InverterGroup &ig: inverter_groups) {
			if (!std::isinf(goal_x))
				break;
			if (ig.inverter.device_id == blob.end_device_id) 
				{goal_x = X_INVERTER; goal_y = y_base_f + Y_INVERTER_OFF;}
			if (ig.pv.device_id == blob.end_device_id) 
				{goal_x = X_PV; goal_y = y_base_f + Y_PV_OFF;}
			if (ig.battery.device_id == blob.end_device_id) 
				{goal_x = X_BATTERY; goal_y = y_base_f + Y_BATTERY_OFF;}
			y_base_f -= IG_HEIGHT;
		}
		if (std::isinf(goal_x)) {
			LogError("Did not find goal device");
			blob.end_device_id = -1; // marks for removal
			continue;
		}
		float prev_x = blob.x;
		float prev_y = blob.y;
		if (std::abs(goal_x - blob.x) + std::abs(goal_y - blob.y) < 3) {
			blob.end_device_id = -1; // marks for removal
			continue;
		}
		const auto crossed = [](float cur, float prev, float val) {return (cur < val && prev >= val) || (cur >= val && prev < val);};
		if (blob.x == X_INV_CONN && goal_x > X_INV_CONN) // correct goal value for blobs on the inverter bus
			blob.dir = blob.y + y_offset < Y_BUS ? Direction::DOWN: Direction::UP;
		switch(blob.dir) {
			case Direction::UP:
				blob.y -= dist; 
				if (goal_x > blob.x && blob.x == X_INV_CONN && 
					crossed(blob.y + y_offset, prev_y + y_offset, Y_BUS)) { // go onto bus
					blob.x += std::abs(blob.y + y_offset - Y_BUS);
					blob.y = Y_BUS;
					blob.dir = Direction::RIGHT;
				} else if ((blob.x != X_INV_CONN || goal_x <= X_INV_CONN) && prev_y > goal_y && blob.y <= goal_y) { // reached target branch
					int x_mult = blob.x < goal_x ? 1: -1;
					blob.x += x_mult * std::abs(blob.y - goal_y);
					blob.y = goal_y;
					blob.dir = blob.x < goal_x ? Direction::RIGHT: Direction::LEFT;
				}
				break;
			case Direction::RIGHT:	
				blob.x += dist;
				if (crossed(blob.x, prev_x, goal_x)) {
					int y_mult = blob.y < goal_y ? 1: -1;
					blob.y += y_mult * std::abs(blob.x - goal_x);
					blob.x = goal_x;
					blob.dir = blob.y < goal_y ? Direction::DOWN: Direction::UP;
				} else if (blob.end_device_id == HOME_ID && crossed(blob.x, prev_x, X_HOME_CONN)) {
					blob.y -= std::abs(blob.x - X_HOME_CONN);
					blob.x = X_HOME_CONN;
					blob.dir = Direction::UP;
				} else if (crossed(blob.x, prev_x, X_INV_CONN)) {
					if (goal_x < X_INV_CONN) { // stay in scroll region
						int y_mult = blob.y < goal_y ? 1: -1;
						blob.y += y_mult * std::abs(blob.x - X_INV_CONN);
						blob.x = X_INV_CONN;
						blob.dir = blob.y < goal_y ? Direction::DOWN: Direction::UP;
					} else {
						float g_y = blob.y + y_offset;
						int y_mult = g_y < goal_y ? 1: -1;
						blob.y += y_mult * std::abs(blob.x - X_INV_CONN);
						blob.x = X_INV_CONN;
						blob.dir = g_y < goal_y ? Direction::DOWN: Direction::UP;
					}
				}
				break;
			case Direction::DOWN:
				blob.y += dist;
				if (blob.x == X_HOME_CONN && crossed(blob.y, prev_y, Y_BUS)) {
					int x_mult = blob.x < goal_x ? 1: -1;
					blob.x += x_mult * std::abs(blob.y - Y_BUS);
					blob.y = Y_BUS;
					blob.dir = blob.x < goal_x ? Direction::RIGHT: Direction::LEFT;
				} else if (blob.end_device_id != GRID_ID && goal_x > X_INV_CONN && crossed(blob.y + y_offset, prev_y + y_offset, Y_BUS)) {
					blob.x += std::abs(blob.y + y_offset - Y_BUS);
					blob.y = Y_BUS;
					blob.dir = Direction::RIGHT;
				} else if (blob.x != X_HOME_CONN && (blob.x != X_INV_CONN || goal_x <= X_INV_CONN) && crossed(blob.y, prev_y, goal_y)) {
					int x_mult = blob.x < goal_x ? 1: -1;
					blob.x += x_mult + std::abs(blob.y - goal_y);
					blob.y = goal_y;
					blob.dir = blob.x < goal_x ? Direction::RIGHT: Direction::LEFT;
				}
				break;
			case Direction::LEFT:
				blob.x -= dist; 
				if (blob.y == Y_HOME && crossed(blob.x, prev_x, X_HOME_CONN)) {
					blob.y += std::abs(blob.x - X_HOME_CONN);
					blob.x = X_HOME_CONN;
					blob.dir = Direction::DOWN;
				} else if (blob.end_device_id == HOME_ID && crossed(blob.x, prev_x, X_HOME_CONN)) {
					blob.y -= std::abs(blob.x - X_HOME_CONN);
					blob.x = X_HOME_CONN;
					blob.dir = Direction::UP;
				} else if (crossed(blob.x, prev_x, X_INV_CONN)) {
					int y_mult = goal_y + y_offset > Y_BUS ? 1: -1;
					blob.y = Y_BUS - y_offset + y_mult * std::abs(blob.x - X_INV_CONN);
					blob.x = X_INV_CONN;
					blob.dir = blob.y < goal_y ? Direction::DOWN: Direction::UP;
				} else if (blob.end_device_id != HOME_ID && crossed(blob.x, prev_x, goal_x)) {
					int y_mult = blob.y < goal_y ? 1: -1;
					blob.y += y_mult * std::abs(blob.x - goal_x);
					blob.x = goal_x;
					blob.dir = blob.y < goal_y ? Direction::DOWN: Direction::UP;
				}
				break;
		}
	}
	// remove stale points
	int prev_size = energy_blobs.size();
	for (int i = energy_blobs.rev_start_idx(); i >= 0; --i)
		if (energy_blobs[i].end_device_id == -1)
			energy_blobs[i] = *energy_blobs.pop();

	// resort points for smallest last (shall be drawn in the front)
	if (spawn_bubbles || prev_size != energy_blobs.size())
		sort_energy(energy_blobs, blobs_sorted);

	int y_offset = static_cast<int>(this->y_offset);
	int x_offset = static_cast<int>(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	// energy counts
	std::string_view power = static_format<64>("Importzähler: {:.1f}Wh", tot_imp_wh);
	draw.text(power.data(), {100 + x_offset, 40}, 80, 1);
	power = static_format<64>("Exportzähler: {:.1f}Wh", tot_exp_wh);
	draw.text(power.data(), {170 + x_offset, 40}, 80, 1);

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

	// draw energy blobs
	draw.set_clip(BUBBLE_VIEW_BOX + Point{x_offset, 0});
	for (uint8_t blob_i: blobs_sorted) {
      		const EnergyBlobInfo &blob = energy_blobs[blob_i];
		float y = blob.y;
		if (blob.x <= X_INV_CONN) {
			y += y_offset;
		}
		draw.set_pen(blob.col);
		draw_hq_circle(draw, {int(blob.x + x_offset), int(y)}, .2 * std::sqrt(blob.energy));
	}
	draw.remove_clip();
	draw.set_pen(0);

	// draw icons
        Point pole_pos{X_GRID + x_offset, Y_GRID};
        Point meter_pos{X_METER + x_offset, Y_METER};
        Point home_pos{X_HOME + x_offset, Y_HOME};
        draw_home(draw, home_pos, ICON_HEIGHT, {.background_col = COL_HOME});
	power = static_format<64>("{}W", int(home.imp_w + home.exp_w));
	draw.text(power.data(), {home_pos.x - 10, home_pos.y + 21}, 40, 1);
        draw_electric_pole(draw, pole_pos, ICON_HEIGHT, {.col = 0xffff, .background_col = COL_POLE});
        draw_smart_meter(draw, meter_pos, ICON_HEIGHT, {.background_col = COL_METER});
	power = static_format<64>("{}W", int(meter.imp_w + meter.exp_w));
	draw.text(power.data(), {meter_pos.x - 40, meter_pos.y + 22}, 40, 1);
	if (inverter_groups.size()) {
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		draw.set_clip(IG_VIEW_BOX + Point{x_offset, 0});
		for (const InverterGroup &ig: inverter_groups) {
			int y_base = static_cast<int>(y_base_f);
			Point p{X_PV + x_offset, y_base + Y_PV_OFF + y_offset};
			draw_pv(draw, p, ICON_HEIGHT, {.background_col = COL_PV});
			power = static_format<64>("{}W", int(ig.pv.imp_w + ig.pv.exp_w));
			draw.text(power.data(), {p.x + 15, p.y - 9}, 40, 1);
			p = {X_INVERTER + x_offset, y_base + Y_INVERTER_OFF + y_offset};
			draw_inverter(draw, p, ICON_HEIGHT, {.background_col = COL_INVERTER});
			power = static_format<64>("{}W", int(ig.inverter.imp_w + ig.inverter.exp_w));
			draw.text(power.data(), {p.x + 2, p.y + 18}, 40, 1);
			p = {X_BATTERY + x_offset, y_base + Y_BATTERY_OFF + y_offset};
			draw_battery(draw, p, ICON_HEIGHT, ig.bat_soc, {.background_col = COL_BATTERY});
			power = static_format<64>("{}W", int(ig.battery.imp_w + ig.battery.exp_w));
			draw.text(power.data(), {p.x + 15, p.y + 4}, 40, 1);
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


struct MinMax { float min, max; };
struct UnitInfo {std::string_view name; MinMax bounds;};
static const Rect PLOT_RECT{10, 60, 220, 170};
template <int N>
void draw_data(Draw &draw, const static_ring_buffer<t::data_time, N> &data, MinMax m, float x_offset, int x_page_offset) {
	float d = m.max - m.min;
	m.min -= d * .125;
	m.max += d * .125;
	d = m.max - m.min;
	std::optional<float> prev{};
	for (int i: range(PLOT_RECT.w)) {
		int j = x_offset - (i + 1);
		if (j >= 0)
			continue;
		t::data_time cur = data[j];
		if (prev) {
			int x_start = -i + PLOT_RECT.w + PLOT_RECT.x;
			int x_end = x_start - 1;
			int y_start = int((1. - (*prev - m.min) / d) * PLOT_RECT.h + PLOT_RECT.y);
			int y_end = int((1. - (cur.data - m.min) / d) * PLOT_RECT.h + PLOT_RECT.y);
			draw.line({x_start + x_page_offset, y_start}, {x_end + x_page_offset, y_end});
		}
		prev = cur.data;
	}
};
void HistoryPage::draw(Draw &draw, TimeInfo time_info, float x_off) {
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	if (!drag_history_view)
 		x_history_offseŧ = .65 * x_history_offseŧ; // converges to 0

	// the buttons are only the selectors. The data is set in the main loop according to this->selected_history
	if (second_button(draw, x_offset)) {
		selected_history = SelectedHistory::SECOND;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		second_button.style = ButtonStyle::BORDER;
	}
	if (minute_button(draw, x_offset)) {
		selected_history = SelectedHistory::MINUTE;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		minute_button.style = ButtonStyle::BORDER;
	}
	if (hour_button(draw, x_offset)) {
		selected_history = SelectedHistory::HOUR;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		hour_button.style = ButtonStyle::BORDER;
	}
	if (net_power_button(draw, x_offset)) {
		net_power_button.style = net_power_button.is_selected() ? ButtonStyle::DEFAULT: ButtonStyle::BORDER;
	}

	static MinMax pow_bounds{-10000, 10000};
	MinMax cur_pow_bounds{-10000, 10000};

	
	// drawing outline
	Point offset{x_offset, 0};
	draw.set_pen(0);
	for (const Line& l: {RECTE(PLOT_RECT.x, PLOT_RECT.y, PLOT_RECT.w, PLOT_RECT.h, 1)})
		draw.line(l.start + offset, l.end + offset);

	{ // drawing curves
		t::locked_data<t::device_data> meter = g::meter_data.access();
		t::locked_data<std::array<t::id_data, MAX_INVERTERS * 2>> inverter = g::inverter_data.access();
		t::locked_data<std::array<t::id_data, MAX_INVERTERS>> soc = g::soc_data.access();
		uint8_t r{100}, g{200}, b{};
		const auto draw_per_x = [&](auto member) {
			draw.set_pen(COL_METER);
			draw_data(draw, meter.data.*member, pow_bounds, x_history_offseŧ, x_offset);
			for (const t::id_data &id_data: inverter.data) {
				if (id_data.device_id < 0)
					continue;
				draw.set_pen(r, g, b);
				draw_data(draw, id_data.data.*member, pow_bounds, x_history_offseŧ, x_offset);
				r += 30; g += 56; b += 111;
			}
			for (const t::id_data &id_data: soc.data) {
				if (id_data.device_id < 0)
					continue;
				draw.set_pen(r, g, b);
				draw_data(draw, id_data.data.*member, pow_bounds, x_history_offseŧ, x_offset);
				r += 30; g += 56; b += 111;
			}
		};
		switch (selected_history) {
			case SelectedHistory::SECOND:
				draw_per_x(&t::device_data::per_second);
				break;
			case SelectedHistory::MINUTE:
				draw_per_x(&t::device_data::per_minute);
				break;
			case SelectedHistory::HOUR:
				draw_per_x(&t::device_data::per_hour);
				break;
		}
	}

	pow_bounds = cur_pow_bounds;
}
bool HistoryPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	x_offset += base_offset;
	if (touch_info.touch_ended() && drag_history_view) {
		drag_history_view = false;
		return true;
	}
	if (drag_history_view && touch_info.cur_touch && touch_info.last_touch) {
		x_history_offseŧ -= touch_info.cur_touch->x - touch_info.last_touch->x;
		return true;
	}
	for (Button *b: time_buttons) {
		if (b->handle_touch_input(touch_info, x_offset))
			return true;
	}
	if (net_power_button.handle_touch_input(touch_info, x_offset))
		return true;
	Rect history_r = PLOT_RECT + Point{x_offset, 0};
	if (touch_info.touch_started() && history_r.contains(*touch_info.cur_touch))
		return drag_history_view = true;
	return false;
}

void table_background(Draw &draw, int x_offset, int cur_y, int row, int x_start = 15) {
	draw.set_pen(RGB{200, 200, 255}.to_rgb565());
	if (row & 1)
		draw.rectangle({x_start + x_offset, cur_y - 1, 240 - x_start - 10, 15});
	draw.set_pen(0);
};
void SettingsPage::draw(Draw &draw, TimeInfo time_info, float x_off, settings &s, runtime_state &r) {
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	// local utility functions
	const auto remove_addr = [&request_settings_store](ModbusTcpAddr a, settings &s, runtime_state &r) {
		ModbusTcpAddr *s_addr = s.configured_inverters | find{a};
		if (!s_addr)
			return;
		request_settings_store = true;
		*s_addr = *s.configured_inverters.pop();
		AddrName *r_ip = r.found_ips | find{[&a](AddrName ipn){return a == ipn.addr;}};
		if (!r_ip)
			return;
		*r_ip = *r.found_ips.pop();
	};

	draw.set_pen(0);

	// configure smart meter
	ModbusTcpAddr &meter = s.configured_meter;
	draw.text("Smart meter:", {10 + x_offset, 34}, 100, 1);
	if (ip1_m.button.text == "..." && meter.ip != 0) {
		ip1_m.str.fill_formatted("{}", meter.ip >> 24);
		ip2_m.str.fill_formatted("{}", (meter.ip >> 16) & 0xff);
		ip3_m.str.fill_formatted("{}", (meter.ip >> 8) & 0xff);
		ip4_m.str.fill_formatted("{}", meter.ip & 0xff);
		port_m.str.fill_formatted("{}", meter.port);
		addr_m.str.fill_formatted("{}", (int)meter.modbus_id);
		ip1_m.button.text = ip1_m.str.sv();
		ip2_m.button.text = ip2_m.str.sv();
		ip3_m.button.text = ip3_m.str.sv();
		ip4_m.button.text = ip4_m.str.sv();
		port_m.button.text = port_m.str.sv();
		addr_m.button.text = addr_m.str.sv();
	}

	// configure inverter
	int cur_y = 50;
	delete_buttons.resize(s.configured_inverters.size());
	draw.text("Verbundene Wechselrichter:", {10 + x_offset, cur_y}, 150, 1);
	cur_y += 15;
	int row{};
	for (const auto &[a, name]: r.found_ips) {
		table_background(draw, x_offset, cur_y, ++row);
		std::string_view line = static_format<64>("{}.{}.{}.{}:{}|{} {}", a.ip >> 24, (a.ip >> 16) & 0xff, (a.ip >> 8) & 0xff, a.ip & 0xff, a.port, (int)a.modbus_id, name.sv());
		draw.text(line, {20 + x_offset, cur_y + 2}, 200, 1);
		Button &del= delete_buttons[row - 1];
		del.pos = {200, cur_y, 13, 13};
		del.text = "X";
		if (del(draw, x_offset))
			remove_addr(a, s, r);
		cur_y += 15;
	}
	cur_y += 5;
	draw.text("Konfigurierte Geräte (nicht verbunden):", {10 + x_offset, cur_y}, 200, 1);
	cur_y += 15;
	int db_off = row;
	row = 0;
	for (ModbusTcpAddr a: s.configured_inverters) {
		if (r.found_ips | find{[a](const AddrName &an){return an.addr == a;}})
			continue;
		table_background(draw, x_offset, cur_y, ++row);
		std::string_view line = static_format<64>("{}.{}.{}.{}:{}|{}", a.ip >> 24, (a.ip >> 16) & 0xff, (a.ip >> 8) & 0xff, a.ip & 0xff, a.port, (int)a.modbus_id);
		draw.text(line, {20 + x_offset, cur_y + 2}, 200, 1);
		Button &del= delete_buttons[db_off + row - 1];
		del.pos = {200, cur_y, 13, 13};
		del.text = "X";
		if (del(draw, x_offset))
			remove_addr(a, s, r);
		cur_y += 15;
	}
	draw.set_pen(0);
	draw.text("Neues Gerät konfigurieren:", {10 + x_offset, 160}, 140, 1);
	
	draw.text(".", {112 + x_offset, 34}, 5, 1);
	draw.text(".", {137 + x_offset, 34}, 5, 1);
	draw.text(".", {162 + x_offset, 34}, 5, 1);
	draw.text(":", {187 + x_offset, 34}, 5, 1);
	draw.text("|", {212 + x_offset, 34}, 5, 1);
	draw.text(".", {32 + x_offset, 184}, 5, 1);
	draw.text(".", {57 + x_offset, 184}, 5, 1);
	draw.text(".", {82 + x_offset, 184}, 5, 1);
	draw.text(":", {107 + x_offset, 184}, 5, 1);
	draw.text("|", {132 + x_offset, 184}, 5, 1);
	for (IpButton *ip_button: ip_buttons) {
		if (!ip_button->button(draw, x_offset)) 
			continue;

		for (IpButton *b: ip_buttons)
			b->button.style = ButtonStyle::DEFAULT;
		ip_button->button.style = ButtonStyle::BORDER;
		selected_ip = ip_button;
	}
	if (configure_button(draw, x_offset)) {
		ModbusTcpAddr a{};
		if (selected_ip == &ip1 || selected_ip == &ip2 || selected_ip == &ip3 || selected_ip == &ip4 || selected_ip == &port || selected_ip == &addr) {
			a.ip |= std::clamp(to_int(ip1.button.text).value_or(0), 0, 255) << 24;
			a.ip |= std::clamp(to_int(ip2.button.text).value_or(0), 0, 255) << 16;
			a.ip |= std::clamp(to_int(ip3.button.text).value_or(0), 0, 255) << 8;
			a.ip |= std::clamp(to_int(ip4.button.text).value_or(0), 0, 255);
			a.port = to_int(port.button.text).value_or(0);
			a.modbus_id = std::clamp(to_int(addr.button.text).value_or(0), 0, 255);
			bool ip_exists = s.configured_inverters | find{a};
			if (!ip_exists && s.configured_inverters.push(a))
				request_settings_store = true;
			else if (ip_exists)
				LogWarning("Addr {}.{}.{}.{}:{}|{} already exists", ip1.button.text, ip2.button.text, ip3.button.text, ip4.button.text, port.button.text, addr.button.text);
			else
				LogError("Failed to add new ip");
		} else {
			a.ip |= std::clamp(to_int(ip1_m.button.text).value_or(0), 0, 255) << 24;
			a.ip |= std::clamp(to_int(ip2_m.button.text).value_or(0), 0, 255) << 16;
			a.ip |= std::clamp(to_int(ip3_m.button.text).value_or(0), 0, 255) << 8;
			a.ip |= std::clamp(to_int(ip4_m.button.text).value_or(0), 0, 255);
			a.port = to_int(port_m.button.text).value_or(0);
			a.modbus_id = std::clamp(to_int(addr_m.button.text).value_or(0), 0, 255);
			request_settings_store = a != s.configured_meter;
			s.configured_meter = a;
		}
	}
	const auto le = [] (char c, int i) { if (c == '0') return true; return (c - '1') <= i - 1; };
	for (Button *button: number_inputs) {
		if (!button->show(draw, x_offset))
			continue;
		if (button == &ix) {
			selected_ip->str.pop();
			selected_ip->button.text = selected_ip->str.sv();
		} else if (selected_ip == &port || selected_ip == &port_m) {
			uint32_t p = to_int(port.button.text).value_or(0);
			bool can_append = 10 * p + to_int(button->text).value_or(0) <= 0xffff;
			if (can_append) {
				selected_ip->str.append(button->text);
				selected_ip->button.text = selected_ip->str.sv();
			}
		} else {
			// safety checks for valid ipv4 part
			int s = selected_ip->str.size();
			std::string_view t = selected_ip->str.sv();
			bool can_append = s < 2 ||
					(s == 2 && le(t[0], 2) && (le(t[0], 1) || 
						(le(t[1], 5) && (le(t[1], 4) || le(button->text[0], 5)))));
			if (can_append) {
				selected_ip->str.append(button->text);
				selected_ip->button.text = selected_ip->str.sv();
			}
		}
	}
}
bool SettingsPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	for (IpButton *b: ip_buttons)
		if (b->button.handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	for (Button &b: delete_buttons)
		if (b.handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	if (configure_button.handle_touch_input(touch_info, base_offset + x_offset))
		return true;
	for (Button *b: number_inputs)
		if (b->handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	return false;
}

constexpr std::string_view LOWER_CHARS{"abcdefghijklmnopqrstuvwxyz"};
void WifiPage::init(bool shift) {
	int cur_x{10};
	int cur_y{130};
	for (int i: range(keyboard_buttons.size())) {
		keyboard_buttons[i].pos = {cur_x, cur_y, 15, 15};
		std::string_view c = KEYBOARD_CHARS.substr(i, 1);
		if (!shift && c[0] >= 'A' && c[0] <= 'Z')
			c = LOWER_CHARS.substr(c[0] - 'A', 1);
		keyboard_buttons[i].text = c;
		cur_x += 17;
		if (cur_x > 220) {
			cur_y += 17;
			cur_x = 10;
		}
	}
}

const static Rect WIFI_RECT{5, 30, 230, 90};
constexpr std::string_view STARS{"***********************************************"};
void WifiPage::draw(Draw &draw, TimeInfo time_info, float x_off, wifi_storage &w) {
	y_offset = .8 * y_offset + .2 * target_y_offset;
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	// drawing the found wifis
	draw.set_pen(RGB{200, 200, 200}.to_rgb565());
	Point offset{x_offset, 0};
	for (const Line& l: {RECTE(WIFI_RECT.x, WIFI_RECT.y, WIFI_RECT.w, WIFI_RECT.h, 1)})
		draw.line(l.start + offset, l.end + offset);
	draw.set_clip(WIFI_RECT + Point{x_offset, 0});
	wifi_pwds.resize(w.wifis.size());
	int cur_y{30 + int(y_offset)};
	for (int i: range(w.wifis.size())) {
		table_background(draw, x_offset, cur_y, i + 1, 5);
		std::string_view cur_ssid = w.wifis[i].ssid.sv();
		draw.set_pen(0);
		draw.text(cur_ssid, {10 + x_offset, cur_y + 2}, 100, 1);
		if (w.ssid_wifi.sv() == cur_ssid && !w.wifi_connected) {
			draw.set_pen(RGB{150, 150, 0}.to_rgb565());
			draw.text("Verbinden...", {180 + x_offset, cur_y + 2}, 100, 1);
		} else if (w.ssid_wifi.sv() == cur_ssid && w.wifi_connected) {
			draw.set_pen(RGB{0, 150, 0}.to_rgb565());
			draw.text("Verbunden", {180 + x_offset, cur_y + 2}, 100, 1);
		}
		cur_y += 15;
		table_background(draw, x_offset, cur_y, i + 1, 5);
		draw.set_pen(0);
		draw.text("Passwort:", {30 + x_offset, cur_y}, 50, 1);
		wifi_pwds[i].button.pos = {80, cur_y - 2, 140, 15};
		if (show_pwd.is_selected())
			wifi_pwds[i].button.text = wifi_pwds[i].pwd.sv();
		else
			wifi_pwds[i].button.text = STARS.substr(0, wifi_pwds[i].pwd.size());
		if (wifi_pwds[i].button(draw, x_offset)) {
			for (PwdButton &b: wifi_pwds)
				b.button.style = ButtonStyle::DEFAULT;
			selected_pwd = &wifi_pwds[i];
			selected_pwd->button.style = ButtonStyle::BORDER;
		}
		cur_y += 20;
	}
	draw.remove_clip();

	if (shift(draw, x_offset)) {
		shift.style = shift.is_selected() ? ButtonStyle::DEFAULT: ButtonStyle::BORDER;
		init(shift.is_selected());
	}

	if (del(draw, x_offset) && selected_pwd)
		selected_pwd->pwd.pop();

	if (show_pwd(draw, x_offset))
		show_pwd.style = show_pwd.is_selected() ? ButtonStyle::DEFAULT: ButtonStyle::BORDER;
	
	if (connect(draw, x_offset) && selected_pwd) {
		int wifi_idx = selected_pwd - wifi_pwds.begin();
		w.ssid_wifi.fill(w.wifis[wifi_idx].ssid.sv());
		w.ssid_wifi.make_c_str_safe();
		w.pwd_wifi.fill(selected_pwd->pwd.sv());
		w.pwd_wifi.make_c_str_safe();
		w.wifi_connected = false;
		w.wifi_changed = true;
		request_store_wifi = true;
	}

	for (Button &b: keyboard_buttons) {
		if (!b(draw, x_offset) || !selected_pwd)
			continue;
		selected_pwd->pwd.append(b.text);
		selected_pwd->button.text = selected_pwd->pwd.sv();
	}
}

bool WifiPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	x_offset += base_offset;
	if (touch_info.touch_ended()) {
		target_y_offset = std::clamp(target_y_offset, std::min(-wifi_storage::Default().wifis.size() * 35.f + WIFI_RECT.h, .0f), .0f);
		if (drag_wifi_view) {
			drag_wifi_view = false;
			return true; // avoid any button being pressed
		}
	}
	if (drag_wifi_view && touch_info.cur_touch && touch_info.last_touch) {
		target_y_offset += touch_info.cur_touch->y - touch_info.last_touch->y;
		return true;
	}
	if (shift.handle_touch_input(touch_info, x_offset) ||
		del.handle_touch_input(touch_info, x_offset) ||
		show_pwd.handle_touch_input(touch_info, x_offset) ||
		connect.handle_touch_input(touch_info, x_offset))
		return true;
	Rect wifi_r = WIFI_RECT + Point{x_offset, 0};
	for (PwdButton &b: wifi_pwds)
		if (wifi_r.contains(touch_info.last_touch.value_or(Point{})) && b.button.handle_touch_input(touch_info, x_offset))
			return true;
	for (Button &b: keyboard_buttons)
		if (b.handle_touch_input(touch_info, x_offset))
			return true;
	if (touch_info.cur_touch && touch_info.last_touch && 
		wifi_r.contains(*touch_info.cur_touch) &&
		std::abs(touch_info.cur_touch->y - touch_info.last_touch->y) > 3)
		return drag_wifi_view = true;
	return false;
}

