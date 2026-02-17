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

#define TEST_TASK_PRIORITY ( tskIDLE_PRIORITY + 1UL )

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
using Display = pimoroni::PicoGraphics_PenRGB565;
using DisplayHQ = pimoroni::PicoVector;
using Screen = pimoroni::ST7701;
using Touchscreen = FT6236;

constexpr uint32_t time_ms() { return time_us_64() / 1000; }
constexpr uint32_t time_s() { return time_us_64() / 1000000; }

// void init_psram() {
//     gpio_set_function(47, GPIO_FUNC_XIP_CS1); // CS for PSRAM
//     xip_ctrl_hw->ctrl|=XIP_CTRL_WRITABLE_M1_BITS;
// }

static Screen& screen() {
    static std::array<uint16_t, FRAME_WIDTH * FRAME_HEIGHT> frame_buffer{};
    static Screen screen{FRAME_WIDTH, FRAME_HEIGHT, pimoroni::ROTATE_0, pimoroni::SPIPins{spi1, LCD_CS, LCD_CLK, LCD_DAT, pimoroni::PIN_UNUSED, LCD_DC, BACKLIGHT}, frame_buffer.data()};
    return screen;
}

static Display& display() {
    static std::array<uint16_t, FRAME_WIDTH * FRAME_HEIGHT> frame_buffer{};
    static uint16_t *screen_buffer = (uint16_t*)std::uintptr_t(0x11000000);
    static Display draw_buffer{FRAME_WIDTH, FRAME_HEIGHT, frame_buffer.data()};
    return draw_buffer;
}

static DisplayHQ& display_hq(Display& display = display()) {
    static DisplayHQ display_hq{&display};
    return display_hq;
}

static Touchscreen& touchscreen() {
    static Touchscreen t{};
    return t;
}

static std::atomic<float> page_offset;

std::array<std::string_view, 4> texts{"Hello darkness", "my old friend,", "shall peace and glory", "thy remove"};
void display_task(void *) {
    LogInfo("Display task started");
    constexpr uint32_t FPS_CAP{16};
    // init all globals
    screen().init();
    display().set_font("bitmap8");
    display_hq().set_antialiasing(PP_AA_X4);
    display_hq().set_font("Roboto", 15);


    uint32_t last_ms = time_ms();
    float dms{};
    float cur_page_offset = page_offset;
    for (;;) {
        uint32_t ms = time_ms();
        uint32_t delta_ms = ms - last_ms;
        dms = .9 * dms + .1 * delta_ms;
        float fps = 1000. / std::max(dms, 1.f);
        std::string_view fps_string = static_format<20>("FPS: {}", int(fps + .5));
        last_ms = ms;
        float page_a = 0.2;
        cur_page_offset = (1 - page_a) * cur_page_offset + page_a * page_offset;

        float x_off = std::sin(ms * .003) * 10;
        float y_off = std::cos(ms * .003) * 10;
        pp_mat3_t text_pos = pp_mat3_identity();
        pp_mat3_translate(&text_pos, 50 + x_off, 100 + y_off);

        display().set_pen(65535);
        display().clear();
        display().set_pen(0);
        pp_transform(NULL);
        display_hq().text("Ab gehts!", 15, 50, &text_pos);
        display().text("Minifuzi EMM", {50, 5}, 150);
        display().text(texts[ms / 3000 % texts.size()], {30 + x_off, 30 + y_off}, 240);
        display().text(fps_string, {210, 1}, 40, 1);
        overview_page().draw(display(), display_hq(), {ms, delta_ms}, cur_page_offset, {}, {}, {});
        history_page().draw(display(), display_hq(), {ms, delta_ms}, cur_page_offset, {});
        settings_page().draw(display(), display_hq(), {ms, delta_ms}, cur_page_offset, settings::Default());
        screen().update(&display());
        vTaskDelay(pdMS_TO_TICKS(FPS_CAP - std::min(time_ms() - ms, uint32_t(FPS_CAP)))); // creates a stable 60fps 
    }
}

void touchscreen_task(void *) {
    LogInfo("Touchscreen task started");
    if (!touchscreen().begin())
        LogError("Failed to init touchscreen");

    std::optional<TS_Point> last_touch;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        uint8_t touches = touchscreen().touched();
        if (!touches) {
            last_touch = {};
            // snap to closest page
            page_offset = std::clamp(std::round(page_offset / 240), -2.f, .0f) * 240.f;
            continue;
        }
        TS_Point p = touchscreen().getPoint(0);
        if (!last_touch) { // new touch
        } else { // dragging
            float dx = (p.x - last_touch->x) / 2; // divide by 2 due to diff in screen res and internal pixel
            page_offset += dx;
        }
        last_touch = p;
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
    stdio_init_all();

    LogInfo("Starting FreeRTOS on all cores.");
    std::cout << "Starting FreeRTOS on all cores\n";

    xTaskCreate(startup_task, "StartupThread", 512, NULL, 1, NULL);

    vTaskStartScheduler();
    return 0;
}
