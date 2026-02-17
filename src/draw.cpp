#include "draw.h"
#include "libraries/pico_vector/pretty-poly.h"
#include "libraries/pico_vector/pretty-poly-primitives.h"

static pp_poly_t* circle() {
	static  pp_poly_t *c = ppp_regular({.x = 0, .y = 0, .r = 7, .e = 20, .s = 0});
	return c;
}

template<unsigned int N>
constexpr void draw(DisplayHQ &display, Mat &transform, DrawSettings draw_settings, const std::array<pp_poly_t*, N> &polies) {
	pimoroni::PicoGraphics &d = *std::decay_t<decltype(display)>::graphics;
	if (draw_settings.draw_background) {
		d.set_pen(draw_settings.background_col.r, draw_settings.background_col.g, draw_settings.background_col.b);
		display.draw(circle(), &transform);
	}
	d.set_pen(draw_settings.col.r, draw_settings.col.g, draw_settings.col.b);
	for (pp_poly_t *p: polies)
		display.draw(p, &transform);
}

void draw_home(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 3> polies{
		ppp_rect({.x = -4, .y = -2, .w = 8, .h = 7, .s = s, .r1 = 0, .r2 = 0, .r3 = 2, .r4 = 2}),
		ppp_line({.x1 = -5, .y1 = -1, .x2 = 0, .y2 = -6, .s = s}),
		ppp_line({.x1 = 5, .y1 = -1, .x2 = 0, .y2 = -6, .s = s}),
	};
	draw(display, transform, draw_settings, polies);
}
void draw_electric_pole(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 7> polies{
		ppp_line({.x1 = -4, .y1 = 5, .x2 = 0, .y2 = -6, .s = s}),
		ppp_line({.x1 = 4, .y1 = 5, .x2 = 0, .y2 = -6, .s = s}),
		ppp_line({.x1 = -4, .y1 = 5, .x2 = 2, .y2 = 0, .s = s}),
		ppp_line({.x1 = 4, .y1 = 5, .x2 = -2, .y2 = 0, .s = s}),
		ppp_line({.x1 = -2, .y1 = -.5, .x2 = 2, .y2 = -.5, .s = s}),
		ppp_line({.x1 = -5, .y1 = -2, .x2 = 5, .y2 = -2, .s = s}),
		ppp_line({.x1 = -3, .y1 = -4, .x2 = 3, .y2 = -4, .s = s}),
	};
	draw(display, transform, draw_settings, polies);
}
void draw_smart_meter(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 2> polies{
		ppp_rect({.x = -4, .y = -5, .w = 8, .h = 10, .s = s, .r1 = 1, .r2 = 1, .r3 = 1, .r4 = 1}),
		ppp_rect({.x = -3, .y = -2, .w = 6, .h = 4, .s = s, .r1 = 0, .r2 = 0, .r3 = 0, .r4 = 0}),
	};
	draw(display, transform, draw_settings, polies);
}
void draw_inverter(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 6> polies{
		ppp_rect({.x = -4, .y = -5, .w = 8, .h = 10, .s = s, .r1 = 2, .r2 = 2, .r3 = 2, .r4 = 2}),
		ppp_line({.x1 = -2, .y1 = 3, .x2 = 2, .y2 = -3, .s = s}),
		ppp_line({.x1 = -3, .y1 = -3, .x2 = -2, .y2 = -3, .s = s}),
		ppp_line({.x1 = -3, .y1 = -2, .x2 = -2, .y2 = -2, .s = s}),
		ppp_arc({.x = 1, .y = 3, .r = 1.5, .s = s, .f = 225, .t = 115}),
		ppp_arc({.x = 2, .y = 1, .r = 1.5, .s = s, .f = 435, .t = 315}),
	};
	draw(display, transform, draw_settings, polies);
}
void draw_pv(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 6> polies{
		ppp_line({.x1 = -3, .y1 = -3, .x2 = 3, .y2 = -3, .s = s}),
		ppp_line({.x1 = -5, .y1 = 3, .x2 = 5, .y2 = 3, .s = s}),
		ppp_line({.x1 = -3, .y1 = -3, .x2 = -5, .y2 = 3, .s = s}),
		ppp_line({.x1 = 3, .y1 = -3, .x2 = 5, .y2 = 3, .s = s}),
		ppp_line({.x1 = -4, .y1 = -1, .x2 = 4, .y2 = -1, .s = s}),
		ppp_line({.x1 = 0, .y1 = -3, .x2 = 01, .y2 = 3, .s = s}),
	};
	draw(display, transform, draw_settings, polies);
}
void draw_battery(DisplayHQ &display, Mat &transform, DrawSettings draw_settings) {
	constexpr float s=.5;
	static std::array<pp_poly_t*, 2> polies{
		ppp_rect({.x = -2, .y = -4, .w = 4, .h = 8, .s = s, .r1 = 1, .r2 = 1, .r3 = 1, .r4 = 1}),
		ppp_rect({.x = -1, .y = -5, .w = 2, .h = 1, .s = s, .r1 = 0, .r2 = 0, .r3 = 0, .r4 = 0}),
	};
	draw(display, transform, draw_settings, polies);
}


void OverviewPage::draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, 
	   std::span<InverterGroup> inverter_groups, PowerInfo home, PowerInfo meter) {
	if (base_offset + x_offset > 240 ||
	    base_offset + x_offset + 240 < 0)
		return;
        float x_off = std::sin(time_info.ms * .003) * 10 + x_offset;
        float y_off = std::cos(time_info.ms * .003) * 10;
        pp_mat3_t pole_pos = pp_mat3_identity();
        pp_mat3_translate(&pole_pos, 50 + x_off, 150 + y_off);
        pp_mat3_scale(&pole_pos, 3, 3);
        pp_mat3_t meter_pos = pp_mat3_identity();
        pp_mat3_translate(&meter_pos, 50 + x_off, 200 + y_off);
        pp_mat3_scale(&meter_pos, 3, 3);
        pp_mat3_t inverter_pos = pp_mat3_identity();
        pp_mat3_translate(&inverter_pos, 100 + x_off, 200 + y_off);
        pp_mat3_scale(&inverter_pos, 3, 3);
        pp_mat3_t home_pos = pp_mat3_identity();
        pp_mat3_translate(&home_pos, 100 + x_off, 150 + y_off);
        pp_mat3_scale(&home_pos, 3, 3);
        pp_mat3_t pv_pos = pp_mat3_identity();
        pp_mat3_translate(&pv_pos, 150 + x_off, 150 + y_off);
        pp_mat3_scale(&pv_pos, 3, 3);
        pp_mat3_t battery_pos = pp_mat3_identity();
        pp_mat3_translate(&battery_pos, 150 + x_off, 200 + y_off);
        pp_mat3_scale(&battery_pos, 3, 3);

        draw_home(display_hq, home_pos, {.background_col = {255,210,100}});
        draw_electric_pole(display_hq, pole_pos, {.col = {255, 255, 255}, .background_col = {100,100,255}});
        draw_smart_meter(display_hq, meter_pos, {.background_col = {200,200,200}});
        draw_inverter(display_hq, inverter_pos, {.background_col = {255,200,200}});
        draw_pv(display_hq, pv_pos, {.background_col = {255,255,200}});
        draw_battery(display_hq, battery_pos, {.background_col= {100, 255, 100}});
}

void HistoryPage::draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, std::span<CurveInfo> curve_infos) {

}

void SettingsPage::draw(Display &display, DisplayHQ &display_hq, TimeInfo time_info, float x_offset, settings& settings) {

}

