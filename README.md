# RGB Guardian - ESP32-C3 SuperMini

PlatformIO project for RGB Guardian with ESP32-C3, WIZ-Remote support, illuminated button outputs, and 7 game modes.

## Current Setup

- Board: ESP32-C3 SuperMini
- Active strip: WS2815 (288 LEDs)
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
- GPIO4: Button 5 (currently not used by gameplay)
- GPIO9: BOOT button

Outputs:
- GPIO10: LED strip data
- GPIO5: Red button LED
- GPIO6: Green button LED
- GPIO7: Blue button LED
- GPIO8: White button LED

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
2. PRESS-TO-LIGHT
3. FOLLOW-ME
4. MEMORY
5. GHOST BOSS
6. DUEL
7. CO-PLAY

Mode indicator:
- Far-end lilac dots show mode number for 2 seconds before mode starts.

## Build and Upload

```bash
pio run
pio run --target upload
pio device monitor
```

## LED Configuration in Code

Edit `src/main.cpp` and select one:

```cpp
// #define LED_SETUP_WS2812B_30
#define LED_SETUP_WS2815_288
```

## Power Notes

- WS2815 uses 12V strip power.
- WS2812B uses 5V strip power.
- Do not power the LED strip from the ESP32 USB port.
- Use a PSU and wire gauge sized for your strip length and brightness.
