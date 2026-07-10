#pragma once
#include <Arduino.h>
#include <EInkDisplay.h>

class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Initialize the display hardware and driver
  void begin();

  // Compile-time X4 defaults — kept for static_assert-style checks elsewhere
  // that still need a constant, but NOT valid for buffer sizing or coordinate
  // math anymore now that X3 (792x528) shares this binary. Use the runtime
  // getters below (getDisplayWidth() etc.) for anything that touches an
  // actual frame buffer or pixel coordinate.
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  // Largest buffer either panel needs (X3's 792x528 is bigger than X4's
  // 800x480 once padded to bytes) — the only safe constant for fixed-size
  // scratch allocations that must exist before the device is detected.
  static constexpr uint32_t MAX_BUFFER_SIZE = EInkDisplay::MAX_BUFFER_SIZE;

  // Runtime geometry — reflects the actual detected panel (X3 or X4) once
  // begin() has run. Use these, not the constexprs above, for anything
  // sizing or indexing into the live frame buffer.
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Non-blocking refresh API
  void beginRefresh(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  bool isRefreshing();
  bool pollRefresh();

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

 private:
  EInkDisplay einkDisplay;
};
