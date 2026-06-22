#include "Interfaces.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
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
        case DisplayFace::SMILE_FACE:
            return "SmileFace";
        case DisplayFace::SLEEP_FACE:
            return "SleepFace";
        case DisplayFace::ERROR_FACE:
            return "ErrorFace";
    }
    return "UnknownFace";
}

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
    // 如果 gpio 是 -1，说明这个 GPIO 不需要软件控制。
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

#if defined(RV1126B_HAS_LVGL)
DisplayDevice* g_lvgl_display_device = nullptr;

#if LVGL_VERSION_MAJOR >= 9
void lvglFlushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    if (g_lvgl_display_device != nullptr && area != nullptr && px_map != nullptr) {
        const int32_t width = area->x2 - area->x1 + 1;
        const int32_t height = area->y2 - area->y1 + 1;
        if (width > 0 && height > 0) {
            const std::size_t bytes = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 2U;
            (void)g_lvgl_display_device->flushLvglRgb565(
                area->x1, area->y1, area->x2, area->y2, px_map, bytes);
        }
    }
    lv_display_flush_ready(display);
}
#else
void lvglFlushCallback(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t* px_map) {
    if (g_lvgl_display_device != nullptr && area != nullptr && px_map != nullptr) {
        const int32_t width = area->x2 - area->x1 + 1;
        const int32_t height = area->y2 - area->y1 + 1;
        if (width > 0 && height > 0) {
            const std::size_t bytes = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 2U;
            (void)g_lvgl_display_device->flushLvglRgb565(
                area->x1,
                area->y1,
                area->x2,
                area->y2,
                reinterpret_cast<const uint8_t*>(px_map),
                bytes);
        }
    }
    lv_disp_flush_ready(display);
}
#endif
#endif

}  // namespace

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
        if (config_.enable_lvgl_display) {
            if (!openLvgl()) {
                close();
                return false;
            }
        } else {
            std::cout << "[Display] legacy ST7789 renderer enabled\n";
            showHeartExpression();
        }
        return opened_;
    }
    return opened_;
}

void DisplayDevice::showFace(DisplayFace face) {
    if (config_.enable_lvgl_display) {
        std::cout << "[Display][LVGL] ignore legacy face="
                  << static_cast<int>(face) << "\n";
        return;
    }

    if (!opened_) {
        std::cout << "[DisplayMock] face=" << toString(face) << "\n";
        return;
    }

    if (face == DisplayFace::GESTURE_OK_FACE) {
        showHeartExpression();
        return;
    }

    if (face == DisplayFace::SMILE_FACE) {
        showSmileExpression();
        return;
    }

    std::cout << "[Display] " << toString(face) << "\n";
}

void DisplayDevice::showHeartExpression() {
    if (config_.enable_lvgl_display) {
        std::cout << "[Display][LVGL] heart expression ignored by LVGL backend\n";
        return;
    }
    if (!config_.enable_display) {
        std::cout << "[Display] mock heart expression\n";
        return;
    }
    if (!opened_) {
        return;
    }

    const int width = config_.st7789_width;
    const int height = config_.st7789_height;

    std::vector<uint16_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0x0000);  // black

    const auto setPixel = [&](int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        pixels[static_cast<std::size_t>(y * width + x)] = color;
    };

    const auto fillCircle = [&](int cx, int cy, int r, uint16_t color) {
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) {
                    setPixel(x, y, color);
                }
            }
        }
    };

    const uint16_t pink = 0xF81F;
    const uint16_t white = 0xFFFF;

    const int cx = width / 2;
    const int cy = height / 2 + 10;
    const int r = std::min(width, height) / 7;

    fillCircle(cx - r, cy - r, r, pink);
    fillCircle(cx + r, cy - r, r, pink);

    for (int y = cy - r; y <= cy + 3 * r; ++y) {
        for (int x = cx - 3 * r; x <= cx + 3 * r; ++x) {
            const int dx = std::abs(x - cx);
            const int dy = y - cy;
            if (dy >= -r && dx + dy < 3 * r) {
                setPixel(x, y, pink);
            }
        }
    }

    // 简单白色边框
    for (int i = 0; i < width; ++i) {
        setPixel(i, 0, white);
        setPixel(i, height - 1, white);
    }
    for (int i = 0; i < height; ++i) {
        setPixel(0, i, white);
        setPixel(width - 1, i, white);
    }

    (void)drawRgb565Bitmap(pixels.data(), width, height);
    std::cout << "[Display] show heart expression\n";
}

void DisplayDevice::showSmileExpression() {
    if (config_.enable_lvgl_display) {
        std::cout << "[Display][LVGL] smile expression ignored by LVGL backend\n";
        return;
    }
    if (!config_.enable_display) {
        std::cout << "[Display] mock smile expression\n";
        return;
    }
    if (!opened_) {
        return;
    }

    const int width = config_.st7789_width;
    const int height = config_.st7789_height;

    std::vector<uint16_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0x0000);  // black

    const auto setPixel = [&](int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        pixels[static_cast<std::size_t>(y * width + x)] = color;
    };

    const auto fillCircle = [&](int cx, int cy, int r, uint16_t color) {
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) {
                    setPixel(x, y, color);
                }
            }
        }
    };

    const uint16_t yellow = 0xFFE0;
    const uint16_t black = 0x0000;
    const uint16_t white = 0xFFFF;

    const int cx = width / 2;
    const int cy = height / 2;
    const int face_r = std::min(width, height) / 3;

    // 脸
    fillCircle(cx, cy, face_r, yellow);

    // 眼睛
    fillCircle(cx - face_r / 3, cy - face_r / 4, face_r / 10, black);
    fillCircle(cx + face_r / 3, cy - face_r / 4, face_r / 10, black);

    // 微笑，用简单抛物线画
    for (int x = -face_r / 2; x <= face_r / 2; ++x) {
        const int y = (x * x) / (face_r * 2);
        for (int t = -2; t <= 2; ++t) {
            setPixel(cx + x, cy + face_r / 5 + y + t, black);
        }
    }

    // 白色边框
    for (int i = 0; i < width; ++i) {
        setPixel(i, 0, white);
        setPixel(i, height - 1, white);
    }
    for (int i = 0; i < height; ++i) {
        setPixel(0, i, white);
        setPixel(width - 1, i, white);
    }

    (void)drawRgb565Bitmap(pixels.data(), width, height);
    std::cout << "[Display] show smile expression\n";
}

void DisplayDevice::close() {
    closeLvgl();

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

bool DisplayDevice::setAddressWindow(int x0, int y0, int x1, int y1) {
    x0 = std::clamp(x0, 0, std::max(0, config_.st7789_width - 1));
    y0 = std::clamp(y0, 0, std::max(0, config_.st7789_height - 1));
    x1 = std::clamp(x1, 0, std::max(0, config_.st7789_width - 1));
    y1 = std::clamp(y1, 0, std::max(0, config_.st7789_height - 1));
    if (x1 < x0 || y1 < y0) {
        return false;
    }

    const std::array<uint8_t, 4> col = {
        static_cast<uint8_t>((x0 >> 8) & 0xFF), static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>((x1 >> 8) & 0xFF), static_cast<uint8_t>(x1 & 0xFF)
    };
    const std::array<uint8_t, 4> row = {
        static_cast<uint8_t>((y0 >> 8) & 0xFF), static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>((y1 >> 8) & 0xFF), static_cast<uint8_t>(y1 & 0xFF)
    };

    return writeCommand(ST7789_CASET) && writeData(col.data(), col.size()) &&
           writeCommand(ST7789_RASET) && writeData(row.data(), row.size()) &&
           writeCommand(ST7789_RAMWR);
}

bool DisplayDevice::flushLvglRgb565(
    int x0,
    int y0,
    int x1,
    int y1,
    const uint8_t* pixels,
    std::size_t length) {
    if (pixels == nullptr || length == 0) {
        return false;
    }
    if (!setAddressWindow(x0, y0, x1, y1)) {
        return false;
    }

    std::vector<uint8_t> big_endian(length);
    for (std::size_t i = 0; i + 1U < length; i += 2U) {
        big_endian[i] = pixels[i + 1U];
        big_endian[i + 1U] = pixels[i];
    }
    return writeData(big_endian.data(), big_endian.size());
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

    if (!setAddressWindow(x0, y0, x1, y1)) {
        return false;
    }

    /* RGB565 高字节在前；逐行写入，避免一次性申请大临时 buffer。 */
    std::vector<uint8_t> line(static_cast<std::size_t>(draw_width) * 2U);
    for (int y = 0; y < draw_height; ++y) {
        for (int x = 0; x < draw_width; ++x) {
            const uint16_t color = pixels[static_cast<std::size_t>(y * width + x)];
            line[static_cast<std::size_t>(x * 2)] = static_cast<uint8_t>((color >> 8) & 0xFF);
            line[static_cast<std::size_t>(x * 2 + 1)] = static_cast<uint8_t>(color & 0xFF);
        }
        if (!writeData(line.data(), line.size())) {
            return false;
        }
    }

    return true;
}

bool DisplayDevice::openLvgl() {
#if defined(RV1126B_HAS_LVGL)
    lv_init();
    g_lvgl_display_device = this;

    const int width = config_.st7789_width > 0 ? config_.st7789_width : 240;
    const int height = config_.st7789_height > 0 ? config_.st7789_height : 240;
    const std::size_t draw_pixels = static_cast<std::size_t>(width) * 40U;
    auto* draw_buffer = new lv_color_t[draw_pixels];

#if LVGL_VERSION_MAJOR >= 9
    lv_display_t* display = lv_display_create(width, height);
    lv_display_set_flush_cb(display, lvglFlushCallback);
    lv_display_set_buffers(
        display,
        draw_buffer,
        nullptr,
        draw_pixels * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_t* screen = lv_screen_active();
#else
    auto* draw_buf = new lv_disp_draw_buf_t;
    lv_disp_draw_buf_init(draw_buf, draw_buffer, nullptr, static_cast<uint32_t>(draw_pixels));

    auto* display_driver = new lv_disp_drv_t;
    lv_disp_drv_init(display_driver);
    display_driver->hor_res = width;
    display_driver->ver_res = height;
    display_driver->flush_cb = lvglFlushCallback;
    display_driver->draw_buf = draw_buf;
    (void)lv_disp_drv_register(display_driver);

    lv_obj_t* screen = lv_scr_act();
#endif
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text(label, "LVGL OK");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    (void)lv_timer_handler();

    lvgl_exit_requested_ = false;
    lvgl_thread_ = std::thread(&DisplayDevice::lvglLoop, this);

    std::cout << "[Display][LVGL] enabled\n";
    std::cout << "[Display][LVGL] resolution=" << width << "x" << height << "\n";
    return true;
#else
    std::cerr << "[Display][LVGL] disabled: built without LVGL\n";
    return false;
#endif
}

void DisplayDevice::closeLvgl() {
    lvgl_exit_requested_ = true;
    if (lvgl_thread_.joinable()) {
        lvgl_thread_.join();
    }
#if defined(RV1126B_HAS_LVGL)
    if (g_lvgl_display_device == this) {
        g_lvgl_display_device = nullptr;
    }
#endif
}

void DisplayDevice::lvglLoop() {
#if defined(RV1126B_HAS_LVGL)
    const int tick_ms = std::max(1, config_.display_tick_ms);
    const int refresh_ms = std::max(tick_ms, config_.display_refresh_ms);
    int elapsed_ms = 0;

    while (!lvgl_exit_requested_) {
        sleepMs(tick_ms);
        lv_tick_inc(static_cast<uint32_t>(tick_ms));
        elapsed_ms += tick_ms;
        if (elapsed_ms >= refresh_ms) {
            (void)lv_timer_handler();
            elapsed_ms = 0;
        }
    }
#endif
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
#ifdef __linux__
    if (gpio < 0) {
        return true;
    }
    const std::string value_path = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/value";
    return writeTextFile(value_path, high ? "1" : "0");
#else
    (void)gpio;
    (void)high;
    return true;
#endif
}

}  // namespace rv1126b
