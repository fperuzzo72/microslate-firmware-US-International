#include <HalDisplay.h>
#include <HalGPIO.h>

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin() {
  // Must run before einkDisplay.begin(): switches the FreeInk driver to the
  // X3 panel (UC8253, 792x528, 10 MHz SPI, X3 waveforms) instead of the X4
  // default (SSD1677, 800x480, 40 MHz SPI). gpio.begin() must already have
  // run by this point (see main.cpp setup()) so deviceIsX3() is valid.
  if (gpio.deviceIsX3()) {
    einkDisplay.setDisplayX3();
  }
  einkDisplay.begin();
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

// FreeInkDisplay's non-blocking path is displayBufferAsync()/refreshBusy(),
// not beginRefresh()/isRefreshing()/pollRefresh() — same idea (push the frame,
// return in ~25ms, poll for completion) but a different name and, importantly,
// NO turnOffScreen parameter: the original EInkDisplay powered down the panel's
// analog circuits as the last step of a refresh (used here because
// GfxRenderer::setFadingFix(true) is on permanently, see main.cpp setup()).
//
// KNOWN GAP, not yet validated on hardware: async refreshes below will NOT
// power down the analog circuits the way the old blocking-with-turnOffScreen
// path did. In practice this should just mean slightly higher idle draw
// between keystrokes (analog stays warm for the next fast refresh) rather
// than a functional bug, since idle/sleep transitions elsewhere in main.cpp
// go through the blocking displayBuffer()/clearScreen() calls, which DO still
// honor turnOffScreen. Worth confirming on real X3/X4 hardware with a power
// meter; if it matters, the fix is a small addition to FreeInkDisplay
// (an optional turnOffScreen on displayBufferAsync), not something to hack
// around here.
void HalDisplay::beginRefresh(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  (void)turnOffScreen;
  einkDisplay.displayBufferAsync(convertRefreshMode(mode));
}

bool HalDisplay::isRefreshing() { return einkDisplay.refreshBusy(); }

bool HalDisplay::pollRefresh() { return einkDisplay.refreshBusy(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
