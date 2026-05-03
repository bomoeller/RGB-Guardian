# RGB Guardian - ESP32-C3 SuperMini

PlatformIO project for RGB Guardian with ESP32-C3, WIZ-Remote support, illuminated button outputs, and 7 game modes.

Firmware layout:
- Controller: LED controller and Player-1 interface
- Player-2: Secondary wireless controller with 4 illuminated buttons sending color-shot presses to the primary controller

Current wireless status:
- Controller accepts up to 2 allowed ESP-NOW sender MAC addresses.
- Unknown sender MACs are printed on Serial so new remotes can be discovered and added to the allowlist.
- Remote packet sequence debouncing is tracked per sender slot, so WIZ remote and Player-2 can operate at the same time.
- Player-2 currently sends only the 4 color-shot buttons (RED/GREEN/BLUE/WHITE).
- Player-2 button LEDs use GPIO5-8 and stay lit until the matching button is pressed.
- Player-2 currently transmits to broadcast during bring-up; you can later replace that with the controller MAC if desired.

## Current Setup

- Board: ESP32-C3 SuperMini
- Active strip profile: WS2815 (300 max LEDs initialized, default active length 288)
- LED data pin: GPIO10
- LED strip power: external 12V power supply
- ESP32 power: USB (5V)

Optional alternate setup in code:
- WS2812B (30 LEDs, 5V)

## GPIO Mapping

Inputs (active-low with internal pull-ups):
- GPIO0: Red button
- GPIO1: Green button
- GPIO2: Blue button
- GPIO3: White button (4-color modes)
- GPIO9: BOOT button

Outputs:
- GPIO10: LED strip data
- GPIO5: Red button LED
- GPIO6: Green button LED
- GPIO7: Blue button LED
- GPIO8: White button LED

Player-2 uses the same button GPIO layout:
- Inputs: GPIO0-3
- Button LEDs: GPIO5-8

## Wiring

```text
ESP32-C3 SuperMini -> LED strip
GPIO10             -> DIN (data in)
GND                -> strip GND

WS2815 power supply (external)
+12V               -> strip +12V
GND                -> strip GND

Important: ESP32 GND and strip power GND must be connected together.
```

## Game Modes (Remote Off cycles)

1. INVERTED
2. FOLLOW-ME
3. MEMORY
4. GHOST BOSS
5. DUEL
6. CO-PLAY
7. ALL-VS-ALL

Mode indicator:
- Far-end lilac dots show mode number for 2 seconds before mode starts.

## Wired Settings Mode (Physical Buttons)

Enter settings:
- Hold RED + WHITE for 1 second
- Requirement: no shots fired in the last 1 second

In settings:
- GREEN: mode up
- BLUE: mode down
- WHITE (short): toggle 3-color / 4-color
- RED (short): save and exit
- RED (long >3s): restart current mode
- Auto-exit after 10 seconds inactivity (applies selection)

LED length adjust sub-mode:
- WHITE (long): enter/exit LED length adjust
- GREEN/BLUE short: +/- 1 LED
- GREEN/BLUE long: accelerated repeated change
- RED save applies changes and restarts game if length changed

## Build and Upload

```bash
pio run -e controller
pio run -e controller --target upload
pio run -e player-2
pio run -e player-2 --target upload
pio device monitor
```

PlatformIO environments:
- `controller` -> `src/controller.cpp`
- `player-2` -> `src/player-2.cpp`

Current Player-2 behavior:
- 4 active-low buttons on GPIO0-3
- 4 illuminated button outputs on GPIO5-8
- LEDs are on by default and turn off while the matching button is pressed
- Sends WIZ-compatible 13-byte ESP-NOW packets for color buttons only

Platform note:
- The project intentionally pins the PlatformIO `espressif32` platform release in `platformio.ini` to keep builds reproducible and avoid toolchain/package drift.

## LED Configuration in Code

Edit `src/controller.cpp` and select one:

```cpp
// #define LED_SETUP_WS2812B_30
#define LED_SETUP_WS2815_288   // WS2815 profile: 300 max LEDs, default active length 288
```

Notes:
- WS2815 profile initializes up to 300 LEDs (`NUM_LEDS`)
- Default gameplay uses 288 active LEDs (`DEFAULT_ACTIVE_LED_COUNT`)
- Active LED length is adjustable in wired settings mode

## Power Notes

- WS2815 uses 12V strip power.
- WS2812B uses 5V strip power.
- Do not power the LED strip from the ESP32 USB port.
- Use a PSU and wire gauge sized for your strip length and brightness.
