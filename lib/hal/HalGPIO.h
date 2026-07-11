#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <XteinkDetect.h>

// Display SPI pins (shared by X3 and X4 — same pinout, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

// X4-only: GPIO0/GPIO20 are a battery ADC pin and the USB-detect UART0_RXD pin.
// On X3 the SAME two pins are instead the I2C bus (SDA=20, SCL=0) used to reach
// the BQ27220 fuel gauge, DS3231 RTC, and QMI8658 IMU — see XteinkDetect.h.
// Never read these as plain GPIO without checking deviceIsX3() first.
#define BAT_GPIO0 0   // Battery voltage (X4 only)
#define UART0_RXD 20  // USB connection detection (X4 only)

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

  // Temporary diagnostic: raw ADC readings + which button (if any) each one
  // currently maps to, for the two button-ladder groups. See
  // debugPrintButtonAdc() in HalGPIO.cpp for how this gets used.
  void debugPrintButtonAdc();

  // Setup wake up GPIO and enter deep sleep
  void startDeepSleep();

  // Get battery percentage (range 0-100)
  int getBatteryPercentage() const;

  // Check if USB is connected
  bool isUsbConnected() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

// Defined in main.cpp. HalDisplay::begin() reads gpio.deviceIsX3() to pick the
// panel driver, so gpio.begin() must run before display.begin() (it already
// does — see setup() in main.cpp).
extern HalGPIO gpio;
