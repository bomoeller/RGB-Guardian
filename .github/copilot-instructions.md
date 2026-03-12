# GitHub Copilot Instructions - EMBEDDED/MICROCONTROLLER TEMPLATE

**Copy this file to: `YourProject/.github/copilot-instructions.md`**

**For projects using: PlatformIO, Arduino, ESP32, Raspberry Pi, STM32, embedded systems**

---

## CRITICAL ENCODING RULES - NEVER VIOLATE

### Rule 1: ASCII ONLY - NO Unicode/Emoji/Special Characters

**This causes encoding crashes on Windows and serial output issues. ALWAYS use ASCII alternatives.**

**FORBIDDEN (causes crashes or display issues):**
- Emojis: No smiley faces, icons, symbols
- Unicode symbols: No checkmarks, arrows, boxes
- Smart quotes: Use straight quotes only
- Unicode box drawing: No horizontal/vertical lines
- Extended ASCII (>127): May not display on serial monitors

**REQUIRED (ASCII only, 0-127):**
- Checkmark: `[OK]` or `PASS`
- Error: `[ERROR]` or `FAIL`
- Warning: `[WARNING]`
- Info: `[INFO]`
- Separator: `=` or `-`
- Arrows: `->` or `<-`
- Quotes: `"` or `'` (straight ASCII only)

**VERIFY BEFORE EVERY RESPONSE:**
```cpp
// WRONG - causes crash or garbled serial output:
Serial.println("✓ Sensor OK");

// CORRECT - ASCII only:
Serial.println("[OK] Sensor initialized");
```

---

## Rule 2: NEVER Modify Working Code Without Explicit Request

**Embedded systems are fragile. NEVER change working code without asking!**

If code is working, DO NOT:
- "Optimize" timing-critical code without testing
- Refactor interrupt handlers
- Change pin assignments
- Modify working peripheral configurations
- Update library versions without asking

ONLY modify code when:
- User explicitly asks to fix a bug
- User explicitly asks to add a feature
- Code is demonstrably broken (with error messages)

**Embedded bugs can brick hardware - ask first!**

---

## EMBEDDED-SPECIFIC RULES

### Rule E1: Hardware Safety First

**ALWAYS consider hardware implications before suggesting code changes.**

Before modifying:
- Pin configurations (could short circuit or damage components)
- PWM frequencies (could damage motors/LEDs)
- Voltage levels (could fry components)
- Current limits (could overheat components)
- Timing-critical code (could cause system instability)

When suggesting changes:
1. State hardware assumptions: "Assuming 5V logic levels..."
2. Warn about risks: "[WARNING] This could damage hardware if..."
3. Ask for confirmation: "Please verify your hardware supports..."

**Code can be fixed. Fried hardware cannot.**

---

### Rule E2: Memory Constraints

**Embedded systems have limited RAM and Flash. Always consider memory usage.**

When writing code:
- Avoid dynamic memory allocation (malloc/new) if possible
- Use `const` and `PROGMEM` for constants (saves RAM)
- Minimize string usage (use F() macro for Flash storage)
- Consider array sizes carefully
- Warn if code might cause stack overflow

Example (Arduino/ESP32):
```cpp
// GOOD - stores string in Flash, not RAM
Serial.println(F("Sensor initialized"));

// BAD - wastes RAM
Serial.println("Sensor initialized");

// GOOD - const array in Flash
const uint8_t lookup[] PROGMEM = {0, 1, 2, 3, 4};

// BAD - uses RAM
uint8_t lookup[] = {0, 1, 2, 3, 4};
```

**Always mention memory impact when suggesting code changes.**

---

### Rule E3: Timing and Interrupts

**Timing is critical in embedded systems. Understand timing implications.**

Guidelines:
- Keep interrupt service routines (ISRs) SHORT
- Don't use delay() in ISRs
- Don't print to Serial in ISRs
- Don't call complex functions in ISRs
- Use volatile for variables shared between ISR and main code
- Consider watchdog timer implications

Example:
```cpp
// GOOD - ISR sets flag, main loop handles it
volatile bool sensor_triggered = false;

void IRAM_ATTR sensor_isr() {
    sensor_triggered = true;  // Fast, no delays
}

void loop() {
    if (sensor_triggered) {
        sensor_triggered = false;
        handle_sensor();  // Complex processing in main loop
    }
}

// BAD - ISR does too much
void sensor_isr() {
    Serial.println("Sensor!");  // DON'T print in ISR
    delay(100);  // DON'T delay in ISR
    process_data();  // DON'T do complex work in ISR
}
```

**Warn user about timing implications when modifying ISRs or time-critical code.**

---

### Rule E4: Power Consumption

**Battery-powered devices need power-efficient code.**

When applicable, consider:
- Use sleep modes when idle
- Turn off unused peripherals
- Lower CPU frequency when possible
- Minimize wireless transmissions
- Use efficient polling intervals

Mention power implications:
- "This code polls every 1ms - consider increasing interval for battery life"
- "WiFi uses significant power - recommend sleep mode between transmissions"

---

### Rule E5: Serial Monitor Output

**Serial output is main debugging tool. Make it useful.**

Serial output standards:
- Use clear prefixes: `[INFO]`, `[ERROR]`, `[DEBUG]`
- Include timestamps when helpful
- Use consistent formatting
- Don't spam serial (slows down system)
- Consider baud rate (115200 is standard, verify with user)

Example:
```cpp
// GOOD - clear, informative
Serial.println("[INFO] WiFi connected");
Serial.print("[DEBUG] Sensor value: ");
Serial.println(value);

// BAD - unclear
Serial.println("ok");
Serial.println(value);  // What value?
```

---

### Rule E6: Platform-Specific Code

**Always specify which platform code is for.**

When providing code, clearly state:
- Target platform: ESP32, Arduino Uno, Raspberry Pi, etc.
- Required libraries and versions
- Pin compatibility
- Voltage level requirements

Example response:
```
This code is for ESP32 (ESP-IDF or Arduino framework).
Requires: Wire library (I2C communication)
Pins: GPIO21 (SDA), GPIO22 (SCL)
Voltage: 3.3V logic levels
```

**Don't provide ESP32 code if user has Arduino Uno - verify platform first!**

---

### Rule E7: PlatformIO-Specific Guidelines

**When working with PlatformIO projects:**

platformio.ini standards:
- Always specify exact board/framework versions
- Include required libraries with versions
- Document upload_port if specific
- Set appropriate monitor_speed

Example:
```ini
[env:esp32dev]
platform = espressif32 @ 6.5.0
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps = 
    Wire
    SPI
    adafruit/Adafruit Sensor @ ^1.1.4
```

When suggesting library additions:
1. Provide full library name (not just "install sensor library")
2. Suggest version constraints
3. Verify library exists in PlatformIO registry

---

### Rule E8: Raspberry Pi Guidelines

**Raspberry Pi is Linux-based - different from microcontrollers.**

Key differences:
- Full Linux OS (can use Python, shell scripts, etc.)
- More RAM/storage (less constrained than MCUs)
- Different GPIO libraries (RPi.GPIO, gpiozero, pigpio)
- Has package manager (apt, pip)
- Consider systemd services for background tasks

When providing Pi code:
- Specify Python version (Python 3.x)
- Mention required packages: `sudo apt install...`
- Handle proper GPIO cleanup
- Consider running as service vs one-shot script

Example:
```python
# Raspberry Pi GPIO example (Python)
import RPi.GPIO as GPIO
import time

GPIO.setmode(GPIO.BCM)
GPIO.setup(18, GPIO.OUT)

try:
    while True:
        GPIO.output(18, GPIO.HIGH)
        time.sleep(1)
        GPIO.output(18, GPIO.LOW)
        time.sleep(1)
finally:
    GPIO.cleanup()  # ALWAYS cleanup on exit
```

---

## GENERAL RULES (Same as General Template)

### Rule 2: NEVER Modify Working Code Without Explicit Request
[Same as general template]

### Rule 3: NEVER Save to Git Without Permission
[Same as general template]

### Rule 4: Structured Approach - One Step at a Time
[Same as general template]

### Rule 5: Progress Tracking Standards
[Same as general template]

### Rule 6: Error Handling Standards
**Special considerations for embedded:**
- Hardware errors need recovery strategies
- Consider watchdog timer resets
- Log errors to EEPROM/Flash if no serial available
- Implement safe defaults if sensor fails

### Rule 7: ALWAYS Verify Before Asserting
[Same as general template]

### Rule 8: Think Deeply - Don't Jump to Conclusions
**Embedded-specific considerations:**
- Could be hardware issue, not software
- Check wiring diagrams before suggesting code changes
- Consider power supply issues
- Check for timing/race conditions

### Rule 9: Ask Questions When Uncertain
**Ask about hardware setup:**
- "What voltage are you using for this sensor?"
- "Which pins is your display connected to?"
- "What's your power supply rating?"
- "Are you using external pull-up resistors?"

**Use Interactive Questions for Configuration Choices:**

When you need user input for configuration decisions (like button mappings, pin assignments, feature selections), use the `ask_questions` tool to create clickable option menus:

```
Example usage:
- Button/key assignments (which button does what?)
- Hardware configuration choices (which sensor on which pin?)
- Feature toggles (enable/disable specific features?)
- Parameter selections (baud rate, LED count, etc.)
```

Guidelines for `ask_questions`:
- Ask ONE question at a time for complex decisions
- Provide 2-6 clear options
- Mark the most sensible option as `recommended`
- Keep option labels short and clear
- Use this for choices, not for information gathering

Example:
```
"Which remote button should fire RED shots?"
Options: Button 1 (recommended), Button 2, Button 3, Other
```

This creates a better UX than typing answers for configuration tasks.

### Rule 10: Follow Existing Instructions FIRST
[Same as general template]

---

## EMBEDDED PRE-RESPONSE CHECKLIST

Before every response, verify:
- [ ] ASCII only (no Unicode - breaks serial monitors)
- [ ] Not modifying working embedded code without permission
- [ ] Considered hardware safety implications
- [ ] Checked memory usage (RAM/Flash)
- [ ] Verified timing/interrupt correctness
- [ ] Specified target platform clearly
- [ ] Included required libraries/versions
- [ ] Considered power consumption if battery-powered
- [ ] Provided clear serial output formatting
- [ ] Asked about hardware setup if needed

---

## PROJECT-SPECIFIC SECTION (Customize This)

### Project Name
RGB Guardian - LED Game System

### Target Hardware
- Microcontroller: ESP32-C3 SuperMini
- Board version: ESP32-C3-DevKitM-1 compatible
- Clock speed: 160MHz
- RAM: 400KB SRAM
- Flash: 4MB

### Development Environment
- IDE: PlatformIO
- Framework: Arduino
- Upload method: USB Serial (CDC)

### Pin Configuration
GPIO usage (same for both LED strip configurations):
```
GPIO0  - Button 1 / Red Shot (input, pull-up)
GPIO1  - Button 2 / Green Shot (input, pull-up)
GPIO2  - Button 3 / Blue Shot (input, pull-up) [Strapping pin - avoid holding during boot]
GPIO3  - Button 4 / White Shot - 4-color mode (input, pull-up)
GPIO4  - Button 5 (input, pull-up, unused)
GPIO5  - Red Button LED (output)
GPIO6  - Green Button LED (output)
GPIO7  - Blue Button LED (output)
GPIO8  - White Button LED (output)
GPIO9  - BOOT Button (input, pull-up, built-in)
GPIO10 - WS2812B/WS2815 LED Strip Data (output)
```

### Power Requirements
- Supply voltage: 5V USB (3.3V logic)
- Current consumption: 
  - ESP32-C3: ~60mA
  - WS2812B (30 LEDs): ~1.8A max at full brightness
  - WS2815 (288 LEDs): ~17A max at full brightness
- Battery life target: N/A (USB/external powered)
- [WARNING] WS2812B: Use external 5V/2A power supply - do not power 30 LEDs from USB!
- [WARNING] WS2815: MUST use external 5V/20A power supply - use proper wire gauge!

### Communication Protocols
- Serial: 115200 baud (USB CDC)
- I2C: Not used
- SPI: Not used (WS2812B uses custom protocol)
- WiFi: Not currently used
- Bluetooth: Not currently used

### Libraries Used
List with versions:
- FastLED @ ^3.6.0 (WS2812B control)

### Hardware Notes
- ESP32-C3 uses native USB (not UART bridge) - requires special CDC flags
- Two LED strip configurations available (switch in main.cpp):
  - WS2812B: 30 LEDs, GRB color order, GPIO8
  - WS2815: 288 LEDs, RGB color order, GPIO8
- WS2812B requires precise timing - use GPIO8 (good signal integrity)
- All buttons use internal pull-ups - wire to GND when pressed

---

## COMMON EMBEDDED PITFALLS TO AVOID

### Pitfall 1: Blocking Delays
**DON'T:**
```cpp
void loop() {
    digitalWrite(LED, HIGH);
    delay(1000);  // Blocks everything!
    digitalWrite(LED, LOW);
    delay(1000);
}
```

**DO:**
```cpp
unsigned long lastBlink = 0;
void loop() {
    if (millis() - lastBlink >= 1000) {
        lastBlink = millis();
        digitalWrite(LED, !digitalRead(LED));
    }
    // Other code can run here
}
```

### Pitfall 2: String Concatenation Memory Leaks
**DON'T:**
```cpp
String message = "Sensor: ";
message += String(value);  // Creates temporary objects
Serial.println(message);
```

**DO:**
```cpp
Serial.print("Sensor: ");
Serial.println(value);  // No String objects
```

### Pitfall 3: Float Math on Constrained MCUs
**DON'T:**
```cpp
float result = sin(angle) * 180.0 / PI;  // Slow on 8-bit AVR
```

**DO:**
```cpp
int16_t result = (sin_lookup[angle] * 180) / 256;  // Use lookup table
```

### Pitfall 4: Not Checking I2C/SPI Return Values
**DON'T:**
```cpp
Wire.write(data);  // Did it work? Who knows!
```

**DO:**
```cpp
if (Wire.write(data) == 0) {
    Serial.println("[ERROR] I2C write failed");
    // Handle error
}
```

---

## QUICK REFERENCE - EMBEDDED

**Most Important Rules:**
1. ASCII only (serial monitor compatibility)
2. Hardware safety first (don't fry components)
3. Memory constraints (limited RAM/Flash)
4. Keep ISRs short and simple
5. Ask about hardware setup when uncertain

**When Debugging:**
1. Check serial monitor output
2. Verify wiring/connections
3. Check power supply voltage/current
4. Measure pin voltages with multimeter
5. Review timing with logic analyzer/oscilloscope

**When Suggesting Code:**
1. State target platform clearly
2. Mention hardware requirements
3. Specify library versions needed
4. Warn about memory/timing implications
5. Provide complete, testable examples

---

**Template Version:** 1.0 - Embedded/Microcontroller Edition  
**Last Updated:** February 4, 2026  
**Usage:** Copy to `.github/copilot-instructions.md` in your embedded project  
**Platforms:** PlatformIO, Arduino, ESP32, ESP8266, STM32, Raspberry Pi, AVR
