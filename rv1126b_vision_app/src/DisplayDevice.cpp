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
        showHeartExpression();  //为了确认屏幕已经通，初始化加上显示逻辑
    }
    return opened_;
}

void DisplayDevice::showFace(DisplayFace face) {
    if (!opened_) {
        std::cout << "[DisplayMock] face=" << displayFaceToLogString(face) << "\n";
        return;
    }

    /*
     * 第一版只打通业务状态到显示接口：
     * GESTURE_OK_FACE 复用已有比心表情；其他表情先日志占位，后续再补真实图案。
     */
    if (face == DisplayFace::GESTURE_OK_FACE) {
        showHeartExpression();
        return;
    }

    std::cout << "[Display] " << displayFaceToLogString(face) << "\n";
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
