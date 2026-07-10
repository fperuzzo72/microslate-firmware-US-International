# X3 support — migration notes

This documents the changes made to add Xteink X3 support to this MicroSlate
fork, and — just as important — what still needs to be validated on real X3
hardware, since none of this has been compiled or run on a device.

## Summary

MicroSlate's own display/battery/input/SD-card code was written only for the
X4 (fixed 800×480 resolution, 40 MHz SPI, ADC-based battery). Rather than
hand-port the X3's panel LUTs, SPI timing, and BQ27220 fuel-gauge driver from
scratch, this switches those four libraries to the **FreeInk SDK**
(`Free-Ink/freeink-sdk`), the same SDK CrossInk uses for its own X3/X4 dual
support. FreeInk is drop-in compatible with the original `EInkDisplay` /
`BatteryMonitor` / `InputManager` / `SDCardManager` APIs, so this was a
targeted integration, not a rewrite of MicroSlate's own code.

## What changed

- **`platformio.ini`** — `lib_deps` now points `BatteryMonitor`,
  `InputManager`, `EInkDisplay`, and `SDCardManager` at symlinked paths into
  `freeink-sdk/` (a new git submodule — see setup commands below) instead of
  the old `lib/EInkDisplay`, `lib/BatteryMonitor`, `lib/InputManager`,
  `lib/SDCardManager` directories, which are now deleted. Added
  `-DFREEINK_DEVICE_X4=1 -DFREEINK_DEVICE_X3=1` so both panels are compiled
  into the one ESP32-C3 binary. Renamed the env from `xteink_x4` to
  `xteink_x3_x4` (and `sdkconfig.xteink_x4` → `sdkconfig.xteink_x3_x4`) since
  it's no longer X4-only; CI workflow paths were updated to match.

- **`lib/hal/HalGPIO.h` / `.cpp`** — added `DeviceType`/`deviceIsX3()`/
  `deviceIsX4()`, mirroring CrossInk. `begin()` now calls
  `freeink::selectXteinkDevice()` **first**, before `inputMgr.begin()` or any
  `pinMode()` call — GPIO0/GPIO20 are the battery-ADC/USB-detect pins on X4
  but the I²C bus (SCL/SDA) to the BQ27220/DS3231/QMI8658 chips on X3, so
  detection has to run before those pins are claimed for anything else.
  `getBatteryPercentage()` now uses `BatteryMonitor()`'s default constructor
  (picks ADC vs. the I²C gauge automatically via `BoardConfig::ACTIVE`)
  instead of the hardcoded `BatteryMonitor(BAT_GPIO0)`. `isUsbConnected()`
  branches: X4 keeps the original `digitalRead(UART0_RXD)`; X3 reads external
  power off the battery gauge instead, since its USB pin is repurposed for I²C.

- **`lib/hal/HalDisplay.h` / `.cpp`** — `begin()` now calls
  `einkDisplay.setDisplayX3()` before `einkDisplay.begin()` when
  `gpio.deviceIsX3()`. Added runtime `getDisplayWidth/Height/WidthBytes/
  getBufferSize()` passthroughs — the old `DISPLAY_WIDTH`/`DISPLAY_HEIGHT`/
  `BUFFER_SIZE` constants are still there (X4 values) but are no longer safe
  to use for anything sizing a buffer or indexing a pixel, since X3 is
  792×528, not 800×480. `beginRefresh()`/`isRefreshing()`/`pollRefresh()` now
  map onto FreeInk's `displayBufferAsync()`/`refreshBusy()` — same idea
  (push the frame, return immediately, poll for completion) but see the
  **known gap** below.

- **`lib/GfxRenderer/GfxRenderer.h` / `.cpp`** — this was the deepest change.
  The BW buffer chunk array was a *compile-time-sized* C array
  (`uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS]`) with a `static_assert`
  that only held because X4's 48000-byte buffer divides evenly by the 8000-
  byte chunk size. X3's 52272-byte buffer doesn't divide evenly, so this is
  now a `std::vector<uint8_t*>`, sized in a new `GfxRenderer::begin()` from
  the real, runtime-detected buffer size (last chunk clamped to whatever's
  left over). Every place that referenced `HalDisplay::DISPLAY_WIDTH`,
  `DISPLAY_HEIGHT`, `DISPLAY_WIDTH_BYTES`, or `BUFFER_SIZE` — coordinate
  rotation, pixel bounds-checking, `invertScreen()`, `getScreenWidth/Height()`,
  `getBufferSize()` — now reads the equivalent runtime member
  (`panelWidth`/`panelHeight`/`panelWidthBytes`/`frameBufferSize`) instead.
  This exact pattern (fixed array → vector, constants → runtime members) is
  copied from CrossInk's own `GfxRenderer`, which already ships this to real
  X3 devices.

- **`src/main.cpp`** — added `renderer.begin();` right after `display.begin();`
  in `setup()`, so the chunk buffer gets sized correctly *after* the panel is
  known. Order matters: `gpio.begin()` → `display.begin()` → `renderer.begin()`.

## Known gaps — not yet validated on hardware

I don't have an X3 (or an ESP-IDF toolchain here) to actually build and flash
this, so two things are flagged rather than silently assumed correct:

1. **`isUsbConnected()` on X3** (`HalGPIO.cpp`) infers USB/charging state from
   the battery gauge instead of a dedicated GPIO, because X3 has no spare pin
   for it. This affects `getWakeupReason()`'s boot-time classification too.
   If wake-from-sleep or the charging indicator behaves oddly on a real X3,
   this is the first place to check.

2. **`turnOffScreen` is dropped on async refreshes** (`HalDisplay::beginRefresh`).
   FreeInk's `displayBufferAsync()` has no `turnOffScreen` parameter — the
   old blocking-refresh behavior of powering down the panel's analog circuits
   as the last step doesn't have an equivalent in the async path. Blocking
   calls (`displayBuffer()`, `clearScreen()`) elsewhere still honor it, so
   this should only mean slightly higher idle draw between fast refreshes
   (e.g. while typing), not a functional bug — but it's worth confirming with
   a power meter. If it matters, the right fix is adding an optional
   `turnOffScreen` to `FreeInkDisplay::displayBufferAsync()` upstream, not a
   workaround here.

Everything else (panel LUTs, SPI clock, device detection, buffer sizing,
coordinate math) reuses FreeInk's tested driver code rather than reimplementing
it, so it should be on much more solid ground than the two items above.

## Setup — one-time, after copying these files into your local clone

```bash
# 1. Add the FreeInk SDK as a submodule (lib_deps in platformio.ini expects
#    it at ./freeink-sdk, alongside platformio.ini)
git submodule add https://github.com/Free-Ink/freeink-sdk.git freeink-sdk
git submodule update --init --recursive

# 2. The old vendored libs are already removed from this patch; if git still
#    shows them as untracked/modified, clean them up explicitly:
git rm -r --cached lib/EInkDisplay lib/BatteryMonitor lib/InputManager lib/SDCardManager 2>/dev/null || true

# 3. Review, then commit
git add -A
git commit -m "Add Xteink X3 support via FreeInk SDK"
git push
```

Then build/flash as usual — the env is now `xteink_x3_x4` instead of
`xteink_x4` (update any local scripts/IDE run configs that referenced the old
env name):

```bash
pio run -e xteink_x3_x4 --target upload --upload-port /dev/ttyUSB0
```

The first boot on each device (X3 or X4) will run the I²C fingerprint probe
and log which one it detected (`DBG_PRINTLN` output over serial) — worth
checking that once before relying on it silently.
