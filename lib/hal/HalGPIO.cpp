#include <HalGPIO.h>
#include <Preferences.h>
#include <SPI.h>
#include <esp_sleep.h>

void HalGPIO::begin() {
  // Must run first and before inputMgr.begin()/pinMode() below: on X4, GPIO0 and
  // GPIO20 are the battery ADC pin and the USB-detect UART0_RXD pin; on X3 they
  // are the I2C bus (SDA=20, SCL=0) that this probe uses to fingerprint the
  // BQ27220/DS3231/QMI8658 chips. selectXteinkDevice() releases the I2C bus and
  // leaves both pins back in INPUT mode when it's done, so it's safe to
  // reconfigure them for ADC/UART use immediately after.
  _deviceType = freeink::selectXteinkDevice() ? DeviceType::X3 : DeviceType::X4;

  inputMgr.begin();

  // Buttons on both X3 and X4 are read as an analog "ladder" (InputStyle::
  // XteinkAdcLadder) — several buttons share one ADC pin, distinguished by
  // voltage range. ESP32's ADC1 is known to read noisy/settling values for
  // the first several samples right after a pin starts being read, which can
  // land inside a button's voltage window and register as a phantom press —
  // observed as the main menu jumping to the second item on first boot with
  // nothing touched. Discard a handful of reads here, before the main loop
  // starts trusting "just pressed" edges, to let the ADC settle first.
  for (int i = 0; i < 5; i++) {
    inputMgr.update();
    delay(20);
  }

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  if (deviceIsX4()) {
    // BAT_GPIO0 is configured for ADC via adc1_config_channel_atten in InputManager::begin()
    // — do NOT call pinMode() here as it reconfigures the pin as digital input in dual framework
    pinMode(UART0_RXD, INPUT);
  }
}

void HalGPIO::update() { inputMgr.update(); }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::debugPrintButtonAdc() {
  InputManager::ButtonAdcSample group1, group2;
  inputMgr.readButtonAdc(group1, group2);
  if (Serial) {
    Serial.printf("[%lu] [BTN-ADC] pin%d=%d (btn=%d)  pin%d=%d (btn=%d)\n", millis(), group1.pin, group1.raw,
                  group1.button, group2.pin, group2.raw, group2.button);
  }
}

void HalGPIO::startDeepSleep() {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (inputMgr.isPressed(BTN_POWER)) {
    delay(50);
    inputMgr.update();
  }
  // Arm the wakeup trigger *after* the button is released
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

int HalGPIO::getBatteryPercentage() const {
  // Default ctor reads BoardConfig::ACTIVE (set by selectXteinkDevice() in
  // begin()) and picks the right backend at runtime: ADC on BAT_GPIO0 for X4,
  // the BQ27220 I2C fuel gauge for X3. Do not pass BAT_GPIO0 explicitly here —
  // that pin is meaningless on X3 (it's the I2C SCL line).
  static const BatteryMonitor battery = BatteryMonitor();
  static int cachedPct = -1;
  static float smoothedMv = -1.0f;
  static unsigned long lastReadMs = 0;
  static bool adcSettled = false;

  // On first call, load the last persisted reading so we don't show a stale default
  if (cachedPct < 0) {
    Preferences prefs;
    prefs.begin("battery", true);  // read-only
    cachedPct = prefs.getInt("pct", -1);
    prefs.end();
  }

  unsigned long now = millis();

  const bool usbCharging = isUsbConnected();

  // ADC reads high for ~2 minutes after boot/wake — trust NVS cache during settling.
  // Skip this window when charging: voltage is actively changing and we want real readings.
  if (!adcSettled) {
    if (!usbCharging && now < 120000) {
      // Still settling — return NVS cached value if we have one
      if (cachedPct >= 0) return cachedPct;
      // No NVS value at all — fall through to read ADC (better than showing nothing)
    }
    adcSettled = true;
  }

  // Battery voltage changes on a timescale of minutes — no need to read every frame
  if (cachedPct < 0 || (now - lastReadMs) >= 30000) {
    float rawMv = static_cast<float>(battery.readMillivolts());

    // EMA smoothing on millivolts (before the nonlinear polynomial).
    // Alpha=0.3 means ~70% weight on history — takes ~5 reads (~2.5 min) to converge,
    // which rejects brief voltage spikes from charging cycles / SPI / BLE noise.
    // When charging, use alpha=1.0 (no smoothing) so voltage tracks in real time.
    if (smoothedMv < 0) {
      smoothedMv = rawMv;  // seed with first real reading
    } else if (usbCharging) {
      smoothedMv = rawMv;  // no smoothing while charging
    } else {
      smoothedMv = 0.3f * rawMv + 0.7f * smoothedMv;
    }

    int newPct = BatteryMonitor::percentageFromMillivolts(static_cast<uint16_t>(smoothedMv));

    // Rate-limit drops only: max 2% decrease per read cycle (every 30s) on battery.
    // Rising is uncapped so the display catches up quickly after charging.
    // When charging via USB, drops are also uncapped (voltage actively rising anyway).
    if (cachedPct >= 0) {
      const int maxDrop = usbCharging ? 20 : 2;
      if (newPct < cachedPct - maxDrop) newPct = cachedPct - maxDrop;
    }

    if (newPct != cachedPct) {
      Preferences prefs;
      prefs.begin("battery", false);
      prefs.putInt("pct", newPct);
      prefs.end();
    }
    cachedPct = newPct;
    lastReadMs = now;
  }
  return cachedPct;
}

bool HalGPIO::isUsbConnected() const {
  if (deviceIsX3()) {
    // X3 has no dedicated USB-detect GPIO — GPIO20 is the I2C SDA line shared
    // with the BQ27220 gauge, DS3231 RTC, and QMI8658 IMU. Ask the gauge
    // instead: externalPower reflects the charger IC/gauge's own read of VBUS,
    // falling back to "is it currently charging" if that field isn't reported.
    //
    // Cached: this is called from getBatteryPercentage(), which every screen's
    // drawBattery() calls on every redraw — an uncached I2C transaction here
    // ran on every single screen update, competing with button-input polling
    // in the same main loop and occasionally delaying it enough to miss a
    // quick press. USB/charging state doesn't need per-frame freshness, so
    // it's re-read at most every 2 seconds.
    static const BatteryMonitor battery = BatteryMonitor();
    static bool cachedConnected = false;
    static unsigned long lastReadMs = 0;
    const unsigned long now = millis();
    if (lastReadMs == 0 || (now - lastReadMs) >= 2000) {
      const BatteryMonitor::Status status = battery.readStatus();
      cachedConnected = status.externalPowerKnown ? status.externalPower
                                                    : (status.chargingKnown && status.charging);
      lastReadMs = now;
    }
    return cachedConnected;
  }
  // X4: U0RXD/GPIO20 reads HIGH when USB is connected — a plain digitalRead(),
  // cheap enough to not need caching.
  return digitalRead(UART0_RXD) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
