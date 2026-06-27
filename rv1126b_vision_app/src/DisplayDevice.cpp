#include "Interfaces.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(RV1126B_HAS_LVGL)
#include <lvgl.h>
#endif

#ifdef __linux__
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace rv1126b {

namespace {

constexpr uint8_t ST7789_SLPOUT = 0x11;
constexpr uint8_t ST7789_COLMOD = 0x3A;
constexpr uint8_t ST7789_MADCTL = 0x36;
constexpr uint8_t ST7789_INVON = 0x21;
constexpr uint8_t ST7789_INVOFF = 0x20;
constexpr uint8_t ST7789_DISPON = 0x29;
constexpr uint8_t ST7789_CASET = 0x2A;
constexpr uint8_t ST7789_RASET = 0x2B;
constexpr uint8_t ST7789_RAMWR = 0x2C;

void sleepMs(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int64_t steadyMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

uint8_t madctlForRotation(int rotation) {
    switch (rotation) {
        case 90:
            return 0x60;
        case 180:
            return 0xC0;
        case 270:
            return 0xA0;
        case 0:
        default:
            return 0x00;
    }
}

const char* displayFaceToLogString(DisplayFace face) {
    switch (face) {
        case DisplayFace::NORMAL_FACE:
            return "NormalFace";
        case DisplayFace::BAD_POSTURE_FACE:
            return "BadPostureFace";
        case DisplayFace::DRINK_REMIND_FACE:
            return "DrinkRemindFace";
        case DisplayFace::DRINK_OK_FACE:
            return "DrinkOkFace";
        case DisplayFace::GESTURE_OK_FACE:
            return "GestureOkFace";
        case DisplayFace::START_FACE:
            return "StartFace";
        case DisplayFace::STOP_FACE:
            return "StopFace";
        case DisplayFace::CONFIRM_FACE:
            return "ConfirmFace";
        case DisplayFace::ROCK_FACE:
            return "RockFace";
        case DisplayFace::IDLE_CLOCK:
            return "IdleClock";
        case DisplayFace::SLEEP_FACE:
            return "SleepFace";
        case DisplayFace::ERROR_FACE:
            return "ErrorFace";
    }
    return "UnknownFace";
}

#if defined(RV1126B_HAS_LVGL)
DisplayDevice* g_lvgl_display_device = nullptr;
std::vector<uint16_t> g_lvgl_draw_buffer;

#if LVGL_VERSION_MAJOR >= 9
lv_display_t* g_lvgl_display = nullptr;

void lvglFlush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    (void)display;
    if (g_lvgl_display_device == nullptr || area == nullptr || px_map == nullptr) {
        lv_display_flush_ready(display);
        return;
    }

    const int width = static_cast<int>(area->x2 - area->x1 + 1);
    const int height = static_cast<int>(area->y2 - area->y1 + 1);
    (void)g_lvgl_display_device->drawRgb565Area(
        static_cast<int>(area->x1),
        static_cast<int>(area->y1),
        width,
        height,
        reinterpret_cast<const uint16_t*>(px_map));
    lv_display_flush_ready(display);
}
#else
lv_disp_draw_buf_t g_lvgl_draw_buf;
lv_disp_drv_t g_lvgl_disp_drv;

void lvglFlush(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t* color_p) {
    if (g_lvgl_display_device == nullptr || area == nullptr || color_p == nullptr) {
        lv_disp_flush_ready(display);
        return;
    }

    const int width = static_cast<int>(area->x2 - area->x1 + 1);
    const int height = static_cast<int>(area->y2 - area->y1 + 1);
    (void)g_lvgl_display_device->drawRgb565Area(
        static_cast<int>(area->x1),
        static_cast<int>(area->y1),
        width,
        height,
        reinterpret_cast<const uint16_t*>(color_p));
    lv_disp_flush_ready(display);
}
#endif

bool utcOffsetTime(int offset_minutes, std::tm& output) {
    const std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return false;
    }
    const std::time_t adjusted = now + static_cast<std::time_t>(offset_minutes) * 60;
#if defined(_WIN32)
    if (gmtime_s(&output, &adjusted) != 0) {
        return false;
    }
#else
    if (gmtime_r(&adjusted, &output) == nullptr) {
        return false;
    }
#endif
    return true;
}
#endif

#ifdef __linux__
bool writeTextFile(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << value;
    return file.good();
}

bool prepareGpio(int gpio) {
    if (gpio < 0) {
        return true;
    }
    const std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio);
    if (!std::ifstream(gpio_path).good()) {
        (void)writeTextFile("/sys/class/gpio/export", std::to_string(gpio));
        sleepMs(10);
    }
    return writeTextFile(gpio_path + "/direction", "out");
}
#endif

}  // namespace

struct DisplayDevice::LvglUiState {
#if defined(RV1126B_HAS_LVGL)
    bool styles_initialized{false};
    lv_obj_t* root{nullptr};
    lv_obj_t* title_label{nullptr};
    lv_obj_t* time_label{nullptr};
    lv_obj_t* date_label{nullptr};
    lv_obj_t* seconds_arc{nullptr};
    lv_obj_t* status_pill{nullptr};
    lv_obj_t* status_label{nullptr};
    lv_obj_t* breathing_dot{nullptr};

    lv_style_t root_style;
    lv_style_t title_style;
    lv_style_t time_style;
    lv_style_t date_style;
    lv_style_t pill_style;
    lv_style_t status_style;
    lv_style_t dot_style;
    lv_style_t arc_style;
    lv_style_t arc_indicator_style;
#endif
};

DisplayDevice::DisplayDevice() = default;

#if defined(RV1126B_HAS_LVGL)
namespace {

const lv_font_t* pickTitleFont() {
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* pickSubtitleFont() {
#if defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* pickTimeFont() {
#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#elif defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* pickDateFont() {
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#elif defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

void breathingDotAnim(void* object, int32_t value) {
    auto* dot = static_cast<lv_obj_t*>(object);
    if (dot == nullptr) {
        return;
    }
    lv_obj_set_style_bg_opa(dot, static_cast<lv_opa_t>(value), 0);
    lv_obj_set_style_shadow_opa(dot, static_cast<lv_opa_t>(value), 0);
}

}  // namespace
#endif

DisplayDevice::~DisplayDevice() {
    close();
}

bool DisplayDevice::open(const AppConfig& config) {
    config_ = config;

    if (!config_.enable_display) {
        opened_ = false;
        std::cout << "[Display] disabled by config, use log mock output\n";
        return true;
    }

#ifdef __linux__
    spi_fd_ = ::open(config.st7789_spi_device.c_str(), O_RDWR);
    if (spi_fd_ < 0) {
        std::cerr << "[Display] open SPI failed: " << config.st7789_spi_device << "\n";
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = static_cast<uint32_t>(config.st7789_spi_speed_hz);
    if (::ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ::ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ::ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        std::cerr << "[Display] configure SPI failed\n";
        close();
        return false;
    }

    if (!prepareGpio(config.st7789_gpio_dc) ||
        !prepareGpio(config.st7789_gpio_reset) ||
        !prepareGpio(config.st7789_gpio_backlight)) {
        std::cerr << "[Display] prepare GPIO failed\n";
        close();
        return false;
    }
#endif

    /*
     * ST7789 初始化顺序保持不变：
     * 1. 打开 SPI，配置 mode=0、bits=8、speed_hz。
     * 2. 配置 DC/RESET/BL GPIO。
     * 3. 发送 SLPOUT、COLMOD、MADCTL、INVON/INVOFF、DISPON。
     * 4. 显示时发送 CASET/RASET/RAMWR，再写 RGB565 像素。
     */
    opened_ = resetPanel() && initPanel();
    if (opened_) {
        (void)setGpio(config_.st7789_gpio_backlight, true);
        std::cout << "[Display] ST7789 open, spi=" << config.st7789_spi_device
                  << ", speed=" << config.st7789_spi_speed_hz
                  << ", size=" << config.st7789_width << "x" << config.st7789_height << "\n";
    }
    return opened_;
}

void DisplayDevice::showFace(DisplayFace face) {
    if (config_.enable_lvgl_display && config_.lvgl_idle_only_test) {
        if (face == DisplayFace::IDLE_CLOCK) {
            showIdleClock();
        } else {
            std::cout << "[Display][LVGL] 空闲时钟测试模式：忽略表情事件\n";
        }
        return;
    }

    if (!opened_) {
        std::cout << "[DisplayMock] face=" << displayFaceToLogString(face) << "\n";
        return;
    }

    auto show_simple_face_page = [&](const char* title, const char* subtitle, uint16_t color) {
        std::cout << "[Display] " << displayFaceToLogString(face)
                  << ": " << (title == nullptr ? "" : title)
                  << " / " << (subtitle == nullptr ? "" : subtitle) << "\n";

#if defined(RV1126B_HAS_LVGL)
        if (config_.enable_lvgl_display && ensureLvglInitialized()) {
            clearLvglPage();

            lvgl_ui_->root = lv_obj_create(lv_scr_act());
            lv_obj_remove_style_all(lvgl_ui_->root);
            lv_obj_add_style(lvgl_ui_->root, &lvgl_ui_->root_style, 0);
            lv_obj_set_size(lvgl_ui_->root, config_.st7789_width, config_.st7789_height);
            lv_obj_center(lvgl_ui_->root);
            lv_obj_clear_flag(lvgl_ui_->root, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* accent = lv_obj_create(lvgl_ui_->root);
            lv_obj_remove_style_all(accent);
            lv_obj_set_size(accent, 86, 86);
            lv_obj_set_pos(accent, 77, 28);
            lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(accent, lv_color_make(
                static_cast<uint8_t>(((color >> 11) & 0x1F) << 3),
                static_cast<uint8_t>(((color >> 5) & 0x3F) << 2),
                static_cast<uint8_t>((color & 0x1F) << 3)), 0);
            lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* title_label = lv_label_create(lvgl_ui_->root);
            lv_obj_add_style(title_label, &lvgl_ui_->title_style, 0);
            lv_label_set_text(title_label, title == nullptr ? "" : title);
            lv_obj_set_pos(title_label, 0, 126);
            lv_obj_set_size(title_label, config_.st7789_width, 36);
            lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

            lv_obj_t* subtitle_label = lv_label_create(lvgl_ui_->root);
            lv_obj_add_style(subtitle_label, &lvgl_ui_->status_style, 0);
            lv_label_set_text(subtitle_label, subtitle == nullptr ? "" : subtitle);
            lv_obj_set_pos(subtitle_label, 0, 170);
            lv_obj_set_size(subtitle_label, config_.st7789_width, 32);
            lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);

            idle_clock_visible_ = false;
            (void)lv_timer_handler();
            return;
        }
#endif

        std::array<uint16_t, 16 * 16> pixels{};
        pixels.fill(0x0000);
        for (int y = 2; y < 14; ++y) {
            for (int x = 2; x < 14; ++x) {
                pixels[static_cast<std::size_t>(y * 16 + x)] = color;
            }
        }
        (void)drawRgb565Bitmap(pixels.data(), 16, 16);
    };

    if (face == DisplayFace::GESTURE_OK_FACE || face == DisplayFace::CONFIRM_FACE) {
        show_simple_face_page("OK", "Got it", 0x07E0);
        return;
    }

    const char* title = nullptr;
    const char* subtitle = nullptr;
    uint16_t color = 0xFFFF;
    switch (face) {
        case DisplayFace::START_FACE:
            title = "START";
            subtitle = "Monitoring ON";
            color = 0x07E0;
            break;
        case DisplayFace::STOP_FACE:
            title = "STOP";
            subtitle = "Standby";
            color = 0xFBE0;
            break;
        case DisplayFace::ROCK_FACE:
            title = "ROCK";
            subtitle = "Nice!";
            color = 0xF81F;
            break;
        case DisplayFace::SLEEP_FACE:
            title = "STANDBY";
            subtitle = "Sleep";
            color = 0x8410;
            break;
        case DisplayFace::NORMAL_FACE:
            title = "RUNNING";
            subtitle = "Monitoring";
            color = 0x07FF;
            break;
        case DisplayFace::BAD_POSTURE_FACE:
            title = "POSTURE";
            subtitle = "Sit up";
            color = 0xF800;
            break;
        case DisplayFace::DRINK_REMIND_FACE:
            title = "DRINK";
            subtitle = "Water time";
            color = 0x001F;
            break;
        case DisplayFace::DRINK_OK_FACE:
            title = "DRINK OK";
            subtitle = "Good";
            color = 0x07E0;
            break;
        case DisplayFace::ERROR_FACE:
            title = "ERROR";
            subtitle = "Check logs";
            color = 0xF800;
            break;
        case DisplayFace::IDLE_CLOCK:
            showIdleClock();
            return;
        case DisplayFace::GESTURE_OK_FACE:
        case DisplayFace::CONFIRM_FACE:
            break;
    }

    if (title != nullptr) {
        show_simple_face_page(title, subtitle, color);
        return;
    }

    std::cout << "[Display] " << displayFaceToLogString(face) << "\n";
}

void DisplayDevice::tick() {
    if (!config_.enable_display || !config_.enable_lvgl_display || !config_.lvgl_idle_only_test) {
        return;
    }

#if defined(RV1126B_HAS_LVGL)
    if (!ensureLvglInitialized()) {
        return;
    }

    const int64_t now_ms = steadyMs();
    if (lvgl_last_tick_ms_ <= 0) {
        lvgl_last_tick_ms_ = now_ms;
    }
    const int64_t delta_ms = std::max<int64_t>(0, now_ms - lvgl_last_tick_ms_);
    lvgl_last_tick_ms_ = now_ms;
    if (delta_ms > 0) {
        lv_tick_inc(static_cast<uint32_t>(delta_ms));
    }

    updateIdleClock(false);
    (void)lv_timer_handler();
#else
    if (!lvgl_compile_warning_printed_) {
        std::cerr << "[Display][LVGL] requested but LVGL is not compiled in\n";
        lvgl_compile_warning_printed_ = true;
    }
#endif
}

void DisplayDevice::showIdleClock() {
    if (!config_.enable_display || !config_.enable_lvgl_display) {
        return;
    }

#if defined(RV1126B_HAS_LVGL)
    if (!ensureLvglInitialized()) {
        return;
    }
    updateIdleClock(true);
    (void)lv_timer_handler();
#else
    if (!lvgl_compile_warning_printed_) {
        std::cerr << "[Display][LVGL] requested but LVGL is not compiled in\n";
        lvgl_compile_warning_printed_ = true;
    }
#endif
}

void DisplayDevice::showHeartExpression() {
    if (!config_.enable_display) {
        std::cout << "[Display] mock heart expression\n";
        return;
    }
    if (!opened_) {
        return;
    }

    /*
     * 保留已有比心表情接口。
     * 真实项目建议把 240x240 PNG 离线转换成 RGB565 数组，这里仍用 16x16 示例图案。
     */
    std::array<uint16_t, 16 * 16> pixels{};
    pixels.fill(0x0000);
    for (int y = 3; y < 13; ++y) {
        for (int x = 3; x < 13; ++x) {
            const int dx = x - 8;
            const int dy = y - 8;
            if ((dx * dx + dy * dy) < 35 || (y > 8 && x > 5 && x < 11)) {
                pixels[static_cast<std::size_t>(y * 16 + x)] = 0xF81F;
            }
        }
    }

    (void)drawRgb565Bitmap(pixels.data(), 16, 16);
    std::cout << "[Display] show heart expression\n";
}

void DisplayDevice::close() {
    clearLvglPage();

    if (opened_) {
        (void)setGpio(config_.st7789_gpio_backlight, false);
        std::cout << "[Display] close\n";
    }

#ifdef __linux__
    if (spi_fd_ >= 0) {
        ::close(spi_fd_);
        spi_fd_ = -1;
    }
#endif
    opened_ = false;
    lvgl_initialized_ = false;
    lvgl_time_warning_printed_ = false;
    lvgl_time_status_printed_ = false;
    idle_clock_visible_ = false;
    idle_clock_last_second_ = -1;
    idle_clock_last_update_ms_ = 0;
    lvgl_last_tick_ms_ = 0;
    lvgl_ui_.reset();
}

bool DisplayDevice::ensureLvglInitialized() {
    if (lvgl_initialized_) {
        return true;
    }
    if (!opened_ || !config_.enable_lvgl_display) {
        return false;
    }

#if defined(RV1126B_HAS_LVGL)
    lv_init();
    g_lvgl_display_device = this;
    const std::size_t buffer_pixels =
        static_cast<std::size_t>(config_.st7789_width) * 40U;
    g_lvgl_draw_buffer.assign(buffer_pixels, 0);

#if LVGL_VERSION_MAJOR >= 9
    g_lvgl_display = lv_display_create(config_.st7789_width, config_.st7789_height);
    lv_display_set_flush_cb(g_lvgl_display, lvglFlush);
    lv_display_set_buffers(
        g_lvgl_display,
        g_lvgl_draw_buffer.data(),
        nullptr,
        g_lvgl_draw_buffer.size() * sizeof(uint16_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    lv_disp_draw_buf_init(&g_lvgl_draw_buf, g_lvgl_draw_buffer.data(), nullptr, g_lvgl_draw_buffer.size());
    lv_disp_drv_init(&g_lvgl_disp_drv);
    g_lvgl_disp_drv.hor_res = config_.st7789_width;
    g_lvgl_disp_drv.ver_res = config_.st7789_height;
    g_lvgl_disp_drv.flush_cb = lvglFlush;
    g_lvgl_disp_drv.draw_buf = &g_lvgl_draw_buf;
    (void)lv_disp_drv_register(&g_lvgl_disp_drv);
#endif

    lvgl_ui_ = std::make_unique<LvglUiState>();
    applyThemeStyles();
    createIdleClockPage();

    lvgl_initialized_ = true;
    lvgl_last_tick_ms_ = steadyMs();
    std::cout << "[Display][LVGL] enabled, resolution="
              << config_.st7789_width << "x" << config_.st7789_height << "\n";
    if (config_.lvgl_idle_only_test) {
        std::cout << "[Display][LVGL] 空闲时钟测试模式\n";
    }
    std::cout << "[Display][LVGL] show IdleClock\n";
    updateIdleClock(true);
    idle_clock_visible_ = true;
    return true;
#else
    if (!lvgl_compile_warning_printed_) {
        std::cerr << "[Display][LVGL] requested but LVGL is not compiled in\n";
        lvgl_compile_warning_printed_ = true;
    }
    return false;
#endif
}

void DisplayDevice::clearLvglPage() {
#if defined(RV1126B_HAS_LVGL)
    if (lvgl_ui_ == nullptr) {
        return;
    }

    if (lvgl_ui_->breathing_dot != nullptr) {
        lv_anim_del(lvgl_ui_->breathing_dot, nullptr);
    }

    if (lvgl_ui_->root != nullptr) {
        lv_obj_del(lvgl_ui_->root);
    }

    lvgl_ui_->root = nullptr;
    lvgl_ui_->title_label = nullptr;
    lvgl_ui_->time_label = nullptr;
    lvgl_ui_->date_label = nullptr;
    lvgl_ui_->seconds_arc = nullptr;
    lvgl_ui_->status_pill = nullptr;
    lvgl_ui_->status_label = nullptr;
    lvgl_ui_->breathing_dot = nullptr;
#endif
}

void DisplayDevice::applyThemeStyles() {
#if defined(RV1126B_HAS_LVGL)
    if (lvgl_ui_ == nullptr || lvgl_ui_->styles_initialized) {
        return;
    }

    lv_style_init(&lvgl_ui_->root_style);
    lv_style_set_bg_opa(&lvgl_ui_->root_style, LV_OPA_COVER);
    lv_style_set_bg_color(&lvgl_ui_->root_style, lv_color_hex(0x06121B));
    lv_style_set_bg_grad_color(&lvgl_ui_->root_style, lv_color_hex(0x0B1E2A));
    lv_style_set_bg_grad_dir(&lvgl_ui_->root_style, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&lvgl_ui_->root_style, 0);
    lv_style_set_pad_all(&lvgl_ui_->root_style, 0);
    lv_style_set_radius(&lvgl_ui_->root_style, 0);

    lv_style_init(&lvgl_ui_->title_style);
    lv_style_set_text_color(&lvgl_ui_->title_style, lv_color_hex(0xF4FBFF));
    lv_style_set_text_font(&lvgl_ui_->title_style, pickTitleFont());
    lv_style_set_text_letter_space(&lvgl_ui_->title_style, 0);
    lv_style_set_bg_opa(&lvgl_ui_->title_style, LV_OPA_TRANSP);

    lv_style_init(&lvgl_ui_->time_style);
    lv_style_set_text_color(&lvgl_ui_->time_style, lv_color_hex(0xF7FDFF));
    lv_style_set_text_font(&lvgl_ui_->time_style, pickTimeFont());
    lv_style_set_bg_opa(&lvgl_ui_->time_style, LV_OPA_TRANSP);

    lv_style_init(&lvgl_ui_->date_style);
    lv_style_set_text_color(&lvgl_ui_->date_style, lv_color_hex(0xB8E5F3));
    lv_style_set_text_font(&lvgl_ui_->date_style, pickDateFont());
    lv_style_set_bg_opa(&lvgl_ui_->date_style, LV_OPA_TRANSP);

    lv_style_init(&lvgl_ui_->pill_style);
    lv_style_set_radius(&lvgl_ui_->pill_style, 15);
    lv_style_set_pad_top(&lvgl_ui_->pill_style, 6);
    lv_style_set_pad_bottom(&lvgl_ui_->pill_style, 6);
    lv_style_set_pad_left(&lvgl_ui_->pill_style, 14);
    lv_style_set_pad_right(&lvgl_ui_->pill_style, 14);
    lv_style_set_bg_color(&lvgl_ui_->pill_style, lv_color_hex(0x112028));
    lv_style_set_bg_grad_color(&lvgl_ui_->pill_style, lv_color_hex(0x142933));
    lv_style_set_bg_grad_dir(&lvgl_ui_->pill_style, LV_GRAD_DIR_HOR);
    lv_style_set_bg_opa(&lvgl_ui_->pill_style, LV_OPA_80);
    lv_style_set_border_width(&lvgl_ui_->pill_style, 1);
    lv_style_set_border_color(&lvgl_ui_->pill_style, lv_color_hex(0x2ED9E8));
    lv_style_set_border_opa(&lvgl_ui_->pill_style, static_cast<lv_opa_t>(89));

    lv_style_init(&lvgl_ui_->status_style);
    lv_style_set_text_color(&lvgl_ui_->status_style, lv_color_hex(0xD7F6FF));
    lv_style_set_text_font(&lvgl_ui_->status_style, pickSubtitleFont());
    lv_style_set_bg_opa(&lvgl_ui_->status_style, LV_OPA_TRANSP);

    lv_style_init(&lvgl_ui_->dot_style);
    lv_style_set_radius(&lvgl_ui_->dot_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&lvgl_ui_->dot_style, lv_color_hex(0x1EF2C3));
    lv_style_set_bg_opa(&lvgl_ui_->dot_style, LV_OPA_70);
    lv_style_set_shadow_width(&lvgl_ui_->dot_style, 12);
    lv_style_set_shadow_color(&lvgl_ui_->dot_style, lv_color_hex(0x18E0F3));
    lv_style_set_shadow_opa(&lvgl_ui_->dot_style, LV_OPA_50);
    lv_style_set_border_width(&lvgl_ui_->dot_style, 0);

    lv_style_init(&lvgl_ui_->arc_style);
    lv_style_set_arc_width(&lvgl_ui_->arc_style, 8);
    lv_style_set_arc_color(&lvgl_ui_->arc_style, lv_color_hex(0x163241));
    lv_style_set_arc_opa(&lvgl_ui_->arc_style, LV_OPA_60);

    lv_style_init(&lvgl_ui_->arc_indicator_style);
    lv_style_set_arc_width(&lvgl_ui_->arc_indicator_style, 8);
    lv_style_set_arc_color(&lvgl_ui_->arc_indicator_style, lv_color_hex(0x24DFF2));
    lv_style_set_arc_opa(&lvgl_ui_->arc_indicator_style, LV_OPA_COVER);
    lv_style_set_shadow_width(&lvgl_ui_->arc_indicator_style, 10);
    lv_style_set_shadow_color(&lvgl_ui_->arc_indicator_style, lv_color_hex(0x24DFF2));
    lv_style_set_shadow_opa(&lvgl_ui_->arc_indicator_style, LV_OPA_20);

    lvgl_ui_->styles_initialized = true;
#endif
}

void DisplayDevice::createStatusPill() {
#if defined(RV1126B_HAS_LVGL)
    if (lvgl_ui_ == nullptr || lvgl_ui_->root == nullptr) {
        return;
    }

    lvgl_ui_->status_pill = lv_obj_create(lvgl_ui_->root);
    lv_obj_remove_style_all(lvgl_ui_->status_pill);
    lv_obj_add_style(lvgl_ui_->status_pill, &lvgl_ui_->pill_style, 0);
    lv_obj_set_size(lvgl_ui_->status_pill, 172, 30);
    lv_obj_set_pos(lvgl_ui_->status_pill, 34, 198);
    lv_obj_clear_flag(lvgl_ui_->status_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(lvgl_ui_->status_pill, true, 0);

    lvgl_ui_->status_label = lv_label_create(lvgl_ui_->status_pill);
    lv_obj_add_style(lvgl_ui_->status_label, &lvgl_ui_->status_style, 0);
    lv_label_set_text(lvgl_ui_->status_label, "Waiting for Gesture");
    lv_obj_set_width(lvgl_ui_->status_label, 160);
    lv_obj_set_style_text_align(lvgl_ui_->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lvgl_ui_->status_label, LV_ALIGN_CENTER, 0, 0);
#endif
}

void DisplayDevice::createBreathingDot() {
#if defined(RV1126B_HAS_LVGL)
    if (lvgl_ui_ == nullptr || lvgl_ui_->status_pill == nullptr) {
        return;
    }

    lvgl_ui_->breathing_dot = lv_obj_create(lvgl_ui_->status_pill);
    lv_obj_remove_style_all(lvgl_ui_->breathing_dot);
    lv_obj_add_style(lvgl_ui_->breathing_dot, &lvgl_ui_->dot_style, 0);
    lv_obj_set_size(lvgl_ui_->breathing_dot, 10, 10);
    lv_obj_align(lvgl_ui_->breathing_dot, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_clear_flag(lvgl_ui_->breathing_dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, lvgl_ui_->breathing_dot);
    lv_anim_set_values(&anim, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_time(&anim, 1400);
    lv_anim_set_playback_time(&anim, 1400);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, breathingDotAnim);
    lv_anim_start(&anim);
#endif
}

void DisplayDevice::createIdleClockPage() {
#if defined(RV1126B_HAS_LVGL)
    if (lvgl_ui_ == nullptr) {
        return;
    }

    clearLvglPage();

    lvgl_ui_->root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(lvgl_ui_->root);
    lv_obj_add_style(lvgl_ui_->root, &lvgl_ui_->root_style, 0);
    lv_obj_set_size(lvgl_ui_->root, config_.st7789_width, config_.st7789_height);
    lv_obj_center(lvgl_ui_->root);
    lv_obj_clear_flag(lvgl_ui_->root, LV_OBJ_FLAG_SCROLLABLE);

    lvgl_ui_->title_label = lv_label_create(lvgl_ui_->root);
    lv_obj_add_style(lvgl_ui_->title_label, &lvgl_ui_->title_style, 0);
    lv_label_set_text(lvgl_ui_->title_label, "Happy Life");
    lv_obj_set_pos(lvgl_ui_->title_label, 0, 10);
    lv_obj_set_size(lvgl_ui_->title_label, 240, 32);
    lv_obj_set_style_text_align(lvgl_ui_->title_label, LV_TEXT_ALIGN_CENTER, 0);

    lvgl_ui_->seconds_arc = lv_arc_create(lvgl_ui_->root);
    lv_obj_remove_style_all(lvgl_ui_->seconds_arc);
    lv_obj_add_style(lvgl_ui_->seconds_arc, &lvgl_ui_->arc_style, LV_PART_MAIN);
    lv_obj_add_style(lvgl_ui_->seconds_arc, &lvgl_ui_->arc_indicator_style, LV_PART_INDICATOR);
    lv_obj_set_pos(lvgl_ui_->seconds_arc, 49, 52);
    lv_obj_set_size(lvgl_ui_->seconds_arc, 142, 142);
    lv_arc_set_range(lvgl_ui_->seconds_arc, 0, 59);
    lv_arc_set_value(lvgl_ui_->seconds_arc, 0);
    lv_arc_set_rotation(lvgl_ui_->seconds_arc, 270);
    lv_arc_set_bg_angles(lvgl_ui_->seconds_arc, 0, 360);
    lv_obj_remove_style(lvgl_ui_->seconds_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(lvgl_ui_->seconds_arc, LV_OBJ_FLAG_CLICKABLE);

    lvgl_ui_->time_label = lv_label_create(lvgl_ui_->root);
    lv_obj_add_style(lvgl_ui_->time_label, &lvgl_ui_->time_style, 0);
    lv_label_set_text(lvgl_ui_->time_label, "--:--");
    lv_obj_set_pos(lvgl_ui_->time_label, 0, 91);
    lv_obj_set_size(lvgl_ui_->time_label, 240, 58);
    lv_obj_set_style_text_align(lvgl_ui_->time_label, LV_TEXT_ALIGN_CENTER, 0);

    lvgl_ui_->date_label = lv_label_create(lvgl_ui_->root);
    lv_obj_add_style(lvgl_ui_->date_label, &lvgl_ui_->date_style, 0);
    lv_label_set_text(lvgl_ui_->date_label, "---- -- --");
    lv_obj_set_pos(lvgl_ui_->date_label, 0, 148);
    lv_obj_set_size(lvgl_ui_->date_label, 240, 24);
    lv_obj_set_style_text_align(lvgl_ui_->date_label, LV_TEXT_ALIGN_CENTER, 0);

    createStatusPill();
    createBreathingDot();
    std::cout << "[Display][Clock] 使用北京时间 UTC+8，屏幕时区偏移="
              << config_.display_timezone_offset_minutes << " 分钟\n";
    std::cout << "[Display][LVGL] create IdleClock page\n";
#endif
}

void DisplayDevice::updateIdleClock(bool force) {
#if defined(RV1126B_HAS_LVGL)
    if (!lvgl_initialized_ || lvgl_ui_ == nullptr || lvgl_ui_->time_label == nullptr ||
        lvgl_ui_->date_label == nullptr || lvgl_ui_->status_label == nullptr ||
        lvgl_ui_->seconds_arc == nullptr) {
        return;
    }

    const int64_t now_ms = steadyMs();
    if (!force && (now_ms - idle_clock_last_update_ms_) < 1000) {
        return;
    }
    idle_clock_last_update_ms_ = now_ms;

    std::tm tm_now{};
    const bool time_ok =
        utcOffsetTime(config_.display_timezone_offset_minutes, tm_now) && (tm_now.tm_year + 1900) >= 2024;
    if (!time_ok) {
        if (!lvgl_time_warning_printed_) {
            std::cout << "[Display][LVGL] 系统时间可能尚未同步，无法显示北京时间\n";
            lvgl_time_warning_printed_ = true;
        }
        lv_label_set_text(lvgl_ui_->time_label, "--:--");
        lv_label_set_text(lvgl_ui_->date_label, "---- -- --");
        lv_label_set_text(lvgl_ui_->status_label, "Time not synced");
        lv_arc_set_value(lvgl_ui_->seconds_arc, 0);
        return;
    }

    if (!force && idle_clock_last_second_ == tm_now.tm_sec) {
        return;
    }
    idle_clock_last_second_ = tm_now.tm_sec;

    char time_text[8]{};
    char date_text[16]{};
    std::snprintf(time_text, sizeof(time_text), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    std::snprintf(
        date_text,
        sizeof(date_text),
        "%04d-%02d-%02d",
        tm_now.tm_year + 1900,
        tm_now.tm_mon + 1,
        tm_now.tm_mday);
    if (!lvgl_time_status_printed_) {
        std::cout << "[Display][Clock] 当前计算北京时间=" << date_text << " " << time_text
                  << ":" << (tm_now.tm_sec < 10 ? "0" : "") << tm_now.tm_sec
                  << ", offset_minutes=" << config_.display_timezone_offset_minutes << "\n";
        lvgl_time_status_printed_ = true;
    }
    lv_label_set_text(lvgl_ui_->time_label, time_text);
    lv_label_set_text(lvgl_ui_->date_label, date_text);
    lv_label_set_text(lvgl_ui_->status_label, "Waiting for Gesture");
    lv_arc_set_value(lvgl_ui_->seconds_arc, tm_now.tm_sec);
    idle_clock_visible_ = true;
#else
    (void)force;
#endif
}

bool DisplayDevice::resetPanel() {
    /* RESET GPIO 时序：低电平约 10ms，再拉高并等待 120ms。 */
    if (!setGpio(config_.st7789_gpio_reset, false)) {
        return false;
    }
    sleepMs(10);
    if (!setGpio(config_.st7789_gpio_reset, true)) {
        return false;
    }
    sleepMs(120);
    return true;
}

bool DisplayDevice::initPanel() {
    const uint8_t color_mode = 0x55;
    const uint8_t madctl = madctlForRotation(config_.st7789_rotation);

    if (!writeCommand(ST7789_SLPOUT)) {
        return false;
    }
    sleepMs(120);

    if (!writeCommand(ST7789_COLMOD) || !writeData(&color_mode, 1)) {
        return false;
    }

    if (!writeCommand(ST7789_MADCTL) || !writeData(&madctl, 1)) {
        return false;
    }

    if (!writeCommand(config_.st7789_invert_color ? ST7789_INVON : ST7789_INVOFF)) {
        return false;
    }

    if (!writeCommand(ST7789_DISPON)) {
        return false;
    }
    sleepMs(20);
    return true;
}

bool DisplayDevice::drawRgb565Area(int x, int y, int width, int height, const uint16_t* pixels) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int x0 = std::clamp(x, 0, std::max(0, config_.st7789_width - 1));
    const int y0 = std::clamp(y, 0, std::max(0, config_.st7789_height - 1));
    const int draw_width = std::min(width, config_.st7789_width - x0);
    const int draw_height = std::min(height, config_.st7789_height - y0);
    if (draw_width <= 0 || draw_height <= 0) {
        return false;
    }

    const int x1 = x0 + draw_width - 1;
    const int y1 = y0 + draw_height - 1;
    const std::array<uint8_t, 4> col = {
        static_cast<uint8_t>((x0 >> 8) & 0xFF), static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>((x1 >> 8) & 0xFF), static_cast<uint8_t>(x1 & 0xFF)
    };
    const std::array<uint8_t, 4> row = {
        static_cast<uint8_t>((y0 >> 8) & 0xFF), static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>((y1 >> 8) & 0xFF), static_cast<uint8_t>(y1 & 0xFF)
    };

    if (!writeCommand(ST7789_CASET) || !writeData(col.data(), col.size()) ||
        !writeCommand(ST7789_RASET) || !writeData(row.data(), row.size()) ||
        !writeCommand(ST7789_RAMWR)) {
        return false;
    }

    std::vector<uint8_t> line(static_cast<std::size_t>(draw_width) * 2U);
    for (int row_index = 0; row_index < draw_height; ++row_index) {
        for (int col_index = 0; col_index < draw_width; ++col_index) {
            const uint16_t color = pixels[static_cast<std::size_t>(row_index * width + col_index)];
            line[static_cast<std::size_t>(col_index * 2)] = static_cast<uint8_t>((color >> 8) & 0xFF);
            line[static_cast<std::size_t>(col_index * 2 + 1)] = static_cast<uint8_t>(color & 0xFF);
        }
        if (!writeData(line.data(), line.size())) {
            return false;
        }
    }
    return true;
}

bool DisplayDevice::drawRgb565Bitmap(const uint16_t* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int draw_width = std::min(width, config_.st7789_width);
    const int draw_height = std::min(height, config_.st7789_height);
    const int x0 = (config_.st7789_width - draw_width) / 2;
    const int y0 = (config_.st7789_height - draw_height) / 2;
    const int x1 = x0 + draw_width - 1;
    const int y1 = y0 + draw_height - 1;

    (void)x1;
    (void)y1;
    return drawRgb565Area(x0, y0, draw_width, draw_height, pixels);
}

bool DisplayDevice::writeCommand(uint8_t command) {
    if (!setGpio(config_.st7789_gpio_dc, false)) {
        return false;
    }

#ifdef __linux__
    if (spi_fd_ < 0) {
        return false;
    }

    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<unsigned long>(&command);
    transfer.len = 1;
    transfer.speed_hz = static_cast<uint32_t>(config_.st7789_spi_speed_hz);
    transfer.bits_per_word = 8;
    return ::ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer) >= 1;
#else
    (void)command;
    return true;
#endif
}

bool DisplayDevice::writeData(const uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0) {
        return false;
    }

#ifdef __linux__
    if (spi_fd_ < 0) {
        return false;
    }
    if (!setGpio(config_.st7789_gpio_dc, true)) {
        return false;
    }

    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<unsigned long>(data);
    transfer.len = static_cast<uint32_t>(length);
    transfer.speed_hz = static_cast<uint32_t>(config_.st7789_spi_speed_hz);
    transfer.bits_per_word = 8;
    return ::ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer) >= 1;
#else
    (void)data;
    (void)length;
    return true;
#endif
}

bool DisplayDevice::setGpio(int gpio, bool high) {
    if (gpio < 0) {
        return true;
    }
#ifdef __linux__
    const std::string value_path = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/value";
    return writeTextFile(value_path, high ? "1" : "0");
#else
    (void)gpio;
    (void)high;
    return true;
#endif
}

}  // namespace rv1126b
