/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/malloc.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/xip.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "lwip/ip4_addr.h"
#include "lwip/apps/mdns.h"

#include "FreeRTOS.h"
#include "task.h"

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"

#include "log_storage.h"
#include "access_point.h"
#include "wifi_storage.h"
#include "webserver.h"
#include "usb_interface.h"
#include "settings.h"
#include "measurements.h"
#include "crypto_storage.h"
#include "ntp_client.h"
#include "draw.h"
#include "FT6236.h"
#include "psram.h"

#define TEST_TASK_PRIORITY ( tskIDLE_PRIORITY + 1UL )
#define PSRAM __attribute__((section (".psram")))

constexpr UBaseType_t STANDARD_TASK_PRIORITY = tskIDLE_PRIORITY + 1ul;
constexpr UBaseType_t CONTROL_TASK_PRIORITY = tskIDLE_PRIORITY + 10ul;

constexpr uint32_t FRAME_WIDTH {240};
constexpr uint32_t FRAME_HEIGHT {240};
constexpr uint32_t BACKLIGHT = 45;
constexpr uint32_t LCD_CLK = 26;
constexpr uint32_t LCD_CS = 28;
constexpr uint32_t LCD_DAT = 27;
constexpr uint32_t LCD_DC = -1;
constexpr uint32_t LCD_D0 = 1;
using Screen = pimoroni::ST7701;
using Touchscreen = FT6236;
constexpr int FRAME_BUFFERS = 2;

constexpr uint32_t time_ms() { return time_us_64() / 1000; }
constexpr uint32_t time_s() { return time_us_64() / 1000000; }

void __no_inline_not_in_flash_func(init_psram)() {
    gpio_set_function(47, GPIO_FUNC_XIP_CS1); // CS for PSRAM
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;
}

struct DrawContext {
    int cur_index{};
    uint16_t frame_buffers[FRAME_BUFFERS][FRAME_WIDTH * FRAME_HEIGHT];
    Draw draw{FRAME_WIDTH, FRAME_HEIGHT, frame_buffers[cur_index]};
    // DrawVec draw_vec{&draw};
    uint16_t* frame_buffer() {return frame_buffers[cur_index];}
    uint16_t* frame_buffer_end() {return frame_buffers[cur_index] + FRAME_WIDTH * FRAME_HEIGHT;}
    void swap() { cur_index = (cur_index + 1) % FRAME_BUFFERS; draw.frame_buffer = frame_buffers[cur_index]; }
};

static DrawContext& draw_ctx() {
    static DrawContext ctx{};
    return ctx;
}

static Screen& screen() {
    static Screen screen{FRAME_WIDTH, FRAME_HEIGHT, pimoroni::ROTATE_0, pimoroni::SPIPins{spi1, LCD_CS, LCD_CLK, LCD_DAT, pimoroni::PIN_UNUSED, LCD_DC, BACKLIGHT}, draw_ctx().frame_buffer()};
    return screen;
}

static Touchscreen& touchscreen() {
    static Touchscreen t{};
    return t;
}

static std::atomic<float> page_offset;

static std::array<InverterGroup, 3> test_groups{
    InverterGroup {
        .inverter = {.imp_w = 0, .exp_w = 1000},
        .pv = {.imp_w = 0, .exp_w = 1100},
        .battery = {.imp_w = 100, .exp_w = 0}
    },
    InverterGroup {
        .inverter = {.imp_w = 0, .exp_w = 1000},
        .pv = {.imp_w = 0, .exp_w = 1100},
        .battery = {.imp_w = 100, .exp_w = 0}
    },
    InverterGroup {
        .inverter = {.imp_w = 0, .exp_w = 1000},
        .pv = {.imp_w = 0, .exp_w = 1100},
        .battery = {.imp_w = 100, .exp_w = 0}
    },
};

std::array<std::string_view, 4> texts{"Hello darkness", "my old friend,", "shall peace and glory", "thy remove"};
void display_task(void *) {
    LogInfo("Display task started");
    constexpr uint32_t FPS_CAP{16};
    // init all globals
    screen().init();
    draw_ctx().draw.set_font("bitmap8");
    // draw_ctx().draw_vec.set_antialiasing(PP_AA_X16);
    // draw_ctx().draw_vec.set_font("Roboto", 15);

    LogInfo("Psram size: {}", ps_size);
    uint32_t last_ms = time_ms();
    float dms{};
    float cur_page_offset = page_offset;
    float fps{};
    for (;;) {
        uint32_t ms = time_ms();
        uint32_t delta_ms = ms - last_ms;
        last_ms = ms;
        dms = .9 * dms + .1 * delta_ms;
        fps = 1000. / std::max(dms, 1.f);
        std::string_view fps_string = static_format<20>("FPS: {}", int(fps + .5));
        float page_a = 0.2;
        cur_page_offset = (1 - page_a) * cur_page_offset + page_a * page_offset;

        float x_off = std::sin(ms * .003) * 10;
        float y_off = std::cos(ms * .003) * 10;
        pp_mat3_t text_pos = pp_mat3_identity();
        pp_mat3_translate(&text_pos, 50 + x_off, 100 + y_off);

        std::fill(draw_ctx().frame_buffer(), draw_ctx().frame_buffer_end(), 0xffff);
        draw_ctx().draw.set_pen(0);
        draw_ctx().draw.text("Minifuzi EMM", {50, 5}, 150);
        draw_ctx().draw.set_pen(0x4f, 0x9f, 0x98);
        draw_ctx().draw.line({45, 22}, {174, 22});
        // draw_ctx().draw.text(texts[ms / 3000 % texts.size()], {30 + x_off, 30 + y_off}, 240);
        draw_ctx().draw.set_pen(0);
        draw_ctx().draw.text(fps_string, {210, 1}, 40, 1);
        overview_page().draw(draw_ctx().draw, {ms, delta_ms}, cur_page_offset, test_groups, {}, {});
        history_page().draw(draw_ctx().draw, {ms, delta_ms}, cur_page_offset, {});
        settings_page().draw(draw_ctx().draw, {ms, delta_ms}, cur_page_offset, settings::Default());
        screen().set_framebuffer(draw_ctx().frame_buffer());
        draw_ctx().swap();
        screen().wait_for_vsync();
        // taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(FPS_CAP - std::min(time_ms() - ms, uint32_t(FPS_CAP)))); // creates a stable 60fps 
        //vTaskDelay(pdMS_TO_TICKS(1000)); // creates a stable 60fps 
    }
}

bool handle_touch_pages(TouchInfo &touch_info, int x_offset) {
    return overview_page().handle_touch_input(touch_info, x_offset) ||
           history_page().handle_touch_input(touch_info, x_offset) ||
           settings_page().handle_touch_input(touch_info, x_offset);
}
void touchscreen_task(void *) {
    LogInfo("Touchscreen task started");
    if (!touchscreen().begin())
        LogError("Failed to init touchscreen");

    TouchInfo touch_info;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        touch_info.last_touch = touch_info.cur_touch;
        touch_info.cur_touch = {};
        uint8_t touches = touchscreen().touched();
        if (!touches) {
            if (touch_info.last_touch)
                handle_touch_pages(touch_info, page_offset);
            // snap to closest page
            page_offset = std::clamp(std::round(page_offset / 240), -2.f, .0f) * 240.f;
            continue;
        }
        TS_Point t = touchscreen().getPoint(0);
        touch_info.cur_touch = Point{t.x / 2, t.y / 2};
        bool touch_handled = handle_touch_pages(touch_info, page_offset);
        if (!touch_handled && touch_info.touch_started())
            touch_info.touch_start = *touch_info.cur_touch;
        if (!touch_handled && !touch_info.touch_started()) { // dragging
            float dx = (touch_info.cur_touch->x - touch_info.last_touch->x); // divide by 2 due to diff in screen res and internal pixel
            page_offset += dx;
        }
    }
}

void usb_comm_task(void *) {
    LogInfo("Usb communication task started");
    crypto_storage::Default();

    for (;;) {
	handle_usb_command();
    }
}

void wifi_search_task(void *) {
    LogInfo("Wifi task started");
    if (wifi_storage::Default().ssid_wifi.empty()) // onyl start the access point by default if no normal wifi connection is set
        access_point::Default().init();

    constexpr uint32_t ap_timeout = 10;
    uint32_t cur_time = time_s();
    uint32_t last_conn = cur_time;

    for (;;) {
        cur_time = time_s();
        uint32_t dt = cur_time - last_conn;
        wifi_storage::Default().update_hostname();
        wifi_storage::Default().update_wifi_connection();
        if (wifi_storage::Default().wifi_connected)
            last_conn = cur_time;
        if (dt % 30 == 5) // every 30 seconds enable reconnect try
            wifi_storage::Default().wifi_changed = true;
        if (dt > ap_timeout) {
            access_point::Default().init();
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, cur_time & 1);
        } else {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, wifi_storage::Default().wifi_connected);
        }
        wifi_storage::Default().update_scanned();
        if (wifi_storage::Default().wifi_connected)
            ntp_client::Default().update_time();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// task to initailize everything and only after initialization startin all other threads
// cyw43 init has to be done in freertos task because it utilizes freertos synchronization variables
void startup_task(void *) {
    LogInfo("Starting initialization");
    std::cout << "Starting initialization\n";
    if (cyw43_arch_init()) {
        for (;;) {
            vTaskDelay(1000);
            LogError("failed to initialize\n");
            std::cout << "failed to initialize arch (probably ram problem, increase ram size)\n";
        }
    }
    cyw43_arch_enable_sta_mode();
    wifi_storage::Default().update_hostname();
    Webserver().start();
    LogInfo("Ready, running http at {}", ip4addr_ntoa(netif_ip4_addr(netif_list)));
    LogInfo("Initialization done");
    std::cout << "Initialization done, get all further info via the commands shown in 'help'\n";

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    xTaskCreate(usb_comm_task, "usb_comm", 512, NULL, 1, NULL);
    xTaskCreate(wifi_search_task, "UpdateWifiThread", 512, NULL, 1, NULL);
    xTaskCreate(display_task, "DisplayThread", 512, NULL, 1, NULL);
    xTaskCreate(touchscreen_task, "TouchscreenThread", 512, NULL, 1, NULL);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    vTaskDelete(NULL); // remove this task for efficiency reasions
}

int main( void )
{
    vreg_set_voltage(VREG_VOLTAGE_1_30);  
    sleep_ms(10);
    set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, true);
    stdio_init_all();
    setup_psram();


    LogInfo("Starting FreeRTOS on all cores.");
    std::cout << "Starting FreeRTOS on all cores\n";

    xTaskCreate(startup_task, "StartupThread", 512, NULL, 1, NULL);

    vTaskStartScheduler();
    return 0;
}
