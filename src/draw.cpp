#include "draw.h"
#include "log_storage.h"
#include "ranges_util.h"
#include "ntp_client.h"

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
void draw_battery(Draw &draw, Point center, int height, DrawSettings draw_settings) {
	static std::array<Line, 5> lines{
		RECT(-15,  -30, 30, 60),
		Line{{-10, -40}, {10, -40}}
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
		EnergyInfo *ei = energy_infos | find{[id = pi.device_id](EnergyInfo& ei) {return id == ei.device_id;}};
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
						energy_blobs.push({.energy = cur_e, .x = x + 1, .y = y, .end_device_id = cur->device_id, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					float cur_e = std::min(rest, home_energy_info.imp_ws);
					if (cur_e > 5) {
						rest -= cur_e;
						home_energy_info.imp_ws -= cur_e;
						energy_blobs.push({.energy = cur_e, .x = x + 1, .y = y, .end_device_id = HOME_ID, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					cur_e = std::min(rest, meter_energy_info.imp_ws);
					if (cur_e > 5) {
						rest -= cur_e;
						meter_energy_info.imp_ws -= cur_e;
						energy_blobs.push({.energy = cur_e, .x = x + 1, .y = y, .end_device_id = METER_ID, .col = COL_INVERTER, .dir = Direction::RIGHT});
					}
					if (rest > 100)
						LogWarning("Still got some juice: {}", rest);
				}
			}
			y_base_f -= IG_HEIGHT;
		}
	}

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
				} else if (goal_x > X_INV_CONN && crossed(blob.y + y_offset, prev_y + y_offset, Y_BUS)) {
					blob.x += std::abs(blob.y + y_offset - Y_BUS);
					blob.y = Y_BUS;
					blob.dir = Direction::RIGHT;
				} else if ((blob.x != X_INV_CONN || goal_x <= X_INV_CONN) && crossed(blob.y, prev_y, goal_y)) {
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
					blob.x = Y_BUS;
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
	for (int i = energy_blobs.rev_start_idx(); i >= 0; --i)
		if (energy_blobs[i].end_device_id == -1)
			energy_blobs[i] = *energy_blobs.pop();

	int y_offset = static_cast<int>(this->y_offset);
	int x_offset = static_cast<int>(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

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
	for (const EnergyBlobInfo &blob: energy_blobs) {
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
        draw_electric_pole(draw, pole_pos, ICON_HEIGHT, {.col = 0xffff, .background_col = COL_POLE});
        draw_smart_meter(draw, meter_pos, ICON_HEIGHT, {.background_col = COL_METER});
	if (inverter_groups.size()) {
		float y_base_f = Y_BUS + inverter_groups.size() * IG_HEIGHT / 2.;
		draw.set_clip(IG_VIEW_BOX + Point{x_offset, 0});
		for (const InverterGroup &ig: inverter_groups) {
			int y_base = static_cast<int>(y_base_f);
			Point p{X_PV + x_offset, y_base + Y_PV_OFF + y_offset};
			draw_pv(draw, p, ICON_HEIGHT, {.background_col = COL_PV});
			p = {X_INVERTER + x_offset, y_base + Y_INVERTER_OFF + y_offset};
			draw_inverter(draw, p, ICON_HEIGHT, {.background_col = COL_INVERTER});
			p = {X_BATTERY + x_offset, y_base + Y_BATTERY_OFF + y_offset};
			draw_battery(draw, p, ICON_HEIGHT, {.background_col = COL_BATTERY});
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
struct GraphInfo {
	MinMax x{};
	static_vector<UnitInfo, 16> units{};
};
static const Rect PLOT_RECT{10, 60, 220, 170};
void HistoryPage::draw(Draw &draw, TimeInfo time_info, float x_off, std::span<CurveInfo> curve_infos) {
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	// the buttons are only the selectors. The data is set in the main loop according to this->selected_history
	if (day_button(draw, x_offset)) {
		selected_history = SelectedHistory::DAY;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		day_button.style = ButtonStyle::BORDER;
	}
	if (month_button(draw, x_offset)) {
		selected_history = SelectedHistory::MONTH;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		month_button.style = ButtonStyle::BORDER;
	}
	if (year_button(draw, x_offset)) {
		selected_history = SelectedHistory::YEAR;
		for (Button *b: time_buttons)
			b->style = ButtonStyle::DEFAULT;
		year_button.style = ButtonStyle::BORDER;
	}
	if (net_power_button(draw, x_offset)) {
		net_power_button.style = net_power_button.is_selected() ? ButtonStyle::DEFAULT: ButtonStyle::BORDER;
	}

	// analyzing the graphs
	static GraphInfo graph_info{};
	// switch (selected_history) {
	// 	case SelectedHistory::DAY: graph_info.x.min = 0; graph_info.x.max = 24;break;
	// 	case SelectedHistory::MONTH: graph_info.x.min = ;break;
	// 	case SelectedHistory::YEAR: ;break;
	// }
	graph_info.units.clear();
	const auto get_or_insert_unit = [] (std::string_view name, GraphInfo &graph_info) -> UnitInfo* {
		UnitInfo *y_unit = graph_info.units | find{[&](const UnitInfo &unit) { return unit.name == name;}};
		if (!y_unit) {
			if (!graph_info.units.push({.name = name}))
				return nullptr;
			y_unit = graph_info.units.back();
		}
		return y_unit;
	};
	for (CurveInfo &ci: curve_infos) {
		UnitInfo *y_unit = get_or_insert_unit(ci.unit_name, graph_info);
		if (!y_unit) {
			LogError("Failed to get and create unit {}", ci.unit_name);
			continue;
		}
		y_unit->bounds.min = std::min(ci.min_val, y_unit->bounds.min);
		y_unit->bounds.max = std::max(ci.max_val, y_unit->bounds.max);
	}

	// checking for derived curve
	if (net_power_button.is_selected()) {
		CurveInfo *producing = curve_infos | find{[](const CurveInfo &ci){ return ci.name == "Erzeugung"; }};
		CurveInfo *consuming = curve_infos | find{[](const CurveInfo &ci){ return ci.name == "Verbrauch"; }};
		if (producing && consuming) {
			int s = std::max(producing->data.size(), producing->data.size());
			derived_curve.data.resize(s);
			derived_curve.min_val = derived_curve.max_val = 0;
			derived_curve.unit_name = "W";
			for (int i: range(s)) {
				int16_t prod = i < producing->data.size() ? producing->data[i]: 0;
				int16_t con = i < consuming->data.size() ? consuming->data[i]: 0;
				derived_curve.data[i] = prod - con;
				derived_curve.min_val = std::min<float>(derived_curve.min_val, derived_curve.data[i]);
				derived_curve.max_val = std::max<float>(derived_curve.max_val, derived_curve.data[i]);
			}
			UnitInfo *y_unit = get_or_insert_unit("W", graph_info);
			if (y_unit) {
				y_unit->bounds.min = std::min(y_unit->bounds.min, derived_curve.min_val);
				y_unit->bounds.max = std::max(y_unit->bounds.max, derived_curve.max_val);
			}
			derived_curve.color = RGB{180, 180, 180}.to_rgb565();
		} else 
			LogError("Didnt find necessary curves");
	}

	// aligning 0 line for all min-max values: Get highest 0 line, adopt all max min vals to get the same 0 line
	float zero_line = 0;
	for (const UnitInfo &ui: graph_info.units)
		zero_line = std::max(zero_line, (-ui.bounds.min) / (ui.bounds.max - ui.bounds.min));
	for (UnitInfo &ui: graph_info.units)
		ui.bounds.min = zero_line * ui.bounds.max / (zero_line - 1);
	int zero_line_y = int((.8 * (1. - zero_line) + .1) * PLOT_RECT.h + PLOT_RECT.y);

	// drawing graphs (always are drawn with common 0)
	Point offset{x_offset, 0};
	draw.set_pen(0);
	for (const Line& l: {RECTE(PLOT_RECT.x, PLOT_RECT.y, PLOT_RECT.w, PLOT_RECT.h, 1)})
		draw.line(l.start + offset, l.end + offset);
	const auto draw_curve = [&](const CurveInfo &ci, GraphInfo &graph_info) {
		UnitInfo *unit = get_or_insert_unit(ci.unit_name, graph_info);
		if (!unit) {
			LogError("Cant draw unit {}", ci.unit_name);
			return;
		}
		MinMax m = unit->bounds;
		float d = m.max - m.min;
		m.min -= d * .125;
		m.max += d * .125;
		d = m.max - m.min;
		draw.set_pen(ci.color);
		for (int i: range(ci.data.size() - 1)) {
			int x_start = int((float(i) / (ci.data.size() - 1)) * PLOT_RECT.w + PLOT_RECT.x);
			int x_end = int((float(i + 1) / (ci.data.size() - 1)) * PLOT_RECT.w + PLOT_RECT.x) + 1;
			int y_start = int((1. - (ci.data[i] - m.min) / d) * PLOT_RECT.h + PLOT_RECT.y);
			int y_end = int((1. - (ci.data[i + 1] - m.min) / d) * PLOT_RECT.h + PLOT_RECT.y);
			draw.line({x_start + x_offset, y_start}, {x_end + x_offset, y_end});
		}
	};
	for (CurveInfo &ci: curve_infos)
		draw_curve(ci, graph_info);
	if (net_power_button.is_selected())
		draw_curve(derived_curve, graph_info);
	draw.set_pen(RGB{180, 0, 0}.to_rgb565());
	draw.line({PLOT_RECT.x + x_offset, zero_line_y}, {PLOT_RECT.x + PLOT_RECT.w + x_offset, zero_line_y});
}
bool HistoryPage::handle_touch_input(TouchInfo &touch_info, int x_offset) {
	for (Button *b: time_buttons) {
		if (b->handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	}
	if (net_power_button.handle_touch_input(touch_info, base_offset + x_offset))
		return true;
	return false;
}

void SettingsPage::draw(Draw &draw, TimeInfo time_info, float x_off, settings& settings) {
	int x_offset = int(x_off + base_offset);
	if (x_offset >= 239 ||
	    x_offset + 239 <= 0)
		return;

	draw.set_pen(0);
	draw.text("Verbundene Geräte:", {10 + x_offset, 30}, 100, 1);
	// TODO: readout and draw connected devices
	
	draw.text("Konfigurierte Geräte:", {10 + x_offset, 60}, 100, 1);
	// TODO: readout and draw configured devices that are not connected

	draw.text("Neues Gerät konfigurieren:", {10 + x_offset, 160}, 140, 1);
	
	for (IpButton *ip_button: ip_buttons) {
		if (!ip_button->button(draw, x_offset)) 
			continue;

		for (IpButton *b: ip_buttons)
			b->button.style = ButtonStyle::DEFAULT;
		ip_button->button.style = ButtonStyle::BORDER;
		selected_ip = ip_button;
	}
	if (configure_button(draw, x_offset)) {
		// TODO: add to stored configurations
	}
	const auto le = [] (char c, int i) { if (c == '0') return true; return (c - '1') <= i - 1; };
	for (Button *button: number_inputs) {
		if (!button->show(draw, x_offset))
			continue;
		if (button == &ix) {
			selected_ip->str.pop();
			selected_ip->button.text = selected_ip->str.sv();
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
	if (configure_button.handle_touch_input(touch_info, base_offset + x_offset))
		return true;
	for (Button *b: number_inputs)
		if (b->handle_touch_input(touch_info, base_offset + x_offset))
			return true;
	return false;
}

