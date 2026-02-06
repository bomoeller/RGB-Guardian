# RGB Guardian - ESP32-C3 SuperMini

PlatformIO project for controlling WS2812B/WS2815 LED strip game with ESP32-C3 SuperMini.

## Hardware Configuration

**Two configurations available:**

### Setup 1: WS2812B (Default)
- **Board**: ESP32-C3 SuperMini
- **LED Strip**: WS2812B (30 LEDs)
- **Data Pin**: GPIO8
- **Color Order**: GRB
- **Power**: 5V (ensure adequate power supply for LED strip)

### Setup 2: WS2815
- **Board**: ESP32-C3 SuperMini
- **LED Strip**: WS2815 (288 LEDs)
- **Data Pin**: GPIO8
- **Color Order**: RGB
- **Power**: 5V (requires external power supply rated for 288 LEDs)

**To switch configurations:** Edit [src/main.cpp](src/main.cpp) and uncomment your desired setup:
```cpp
// Uncomment ONE of these:
#define LED_SETUP_WS2812B_30      // 30 LEDs, WS2812B strip
// #define LED_SETUP_WS2815_288   // 288 LEDs, WS2815 strip
```

## Wiring

```
ESP32-C3 SuperMini -> LED Strip (WS2812B or WS2815)
GPIO8              -> DIN (Data In)
GND                -> GND
5V                 -> 5V (use external power for WS2815 or >10 LEDs on WS2812B)
```

## Features

- FastLED library for smooth animations
- Rainbow cycling animation (default)
- Configurable brightness and FPS

## Getting Started

1. Connect the LED strip to GPIO8
2. Build and upload: `pio run --target upload`
3. Monitor serial output: `pio device monitor`

## Power Considerations

**WS2812B (30 LEDs):**
- 30 LEDs at full brightness can draw up to 1.8A
- Use an external 5V power supply capable of at least 2A
- Connect ESP32 GND to power supply GND

**WS2815 (288 LEDs):**
- 288 LEDs at full brightness can draw up to 17A
- MUST use external 5V power supply rated for at least 20A
- Use appropriate wire gauge for high current
- Connect ESP32 GND to power supply GND
