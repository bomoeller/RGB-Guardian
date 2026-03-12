# RGB Guardian - How to Play

**Version:** 1.0  
**Date:** February 7, 2026

---

## Overview

RGB Guardian is a color-matching LED strip game where you defend your position by shooting colored projectiles at an advancing boss. Match the boss's colors to destroy it segment by segment before it reaches you!

---

## Hardware Setup

### Required Components

1. **ESP32-C3 SuperMini** (microcontroller)
2. **WS2815 LED Strip** - 288 LEDs (or WS2812B with 30 LEDs)
3. **5 Push Buttons** (for color shooting)
4. **WIZ-Remote** (optional - for wireless control)
5. **Power Supply:**
   - WS2815 (288 LEDs): 5V/20A minimum
   - WS2812B (30 LEDs): 5V/2A minimum
6. **Jumper wires**

### Physical Connections

**LED Strip:**
- LED Data Pin → ESP32-C3 GPIO 8
- LED +5V → External 5V power supply
- LED GND → Common ground (ESP32 + Power Supply)

**Buttons (active-low with internal pull-ups):**
- Button 1 (RED) → ESP32-C3 GPIO 0 → GND when pressed
- Button 2 (GREEN) → ESP32-C3 GPIO 1 → GND when pressed
- Button 3 (BLUE) → ESP32-C3 GPIO 2 → GND when pressed
- Button 4 (WHITE) → ESP32-C3 GPIO 3 → GND when pressed
- Button 5 (unused) → ESP32-C3 GPIO 4 → GND when pressed

**Power:**
- ESP32-C3 powered via USB (5V)
- LED strip powered from external supply
- **CRITICAL:** Connect all grounds together (ESP32 GND + LED GND + Power Supply GND)

### Wiring Diagram Notes

```
[5V Power Supply] ----+---- [LED Strip +5V]
                      |
                      +---- [Common GND] ----+---- [LED Strip GND]
                                             |
[ESP32-C3 USB] -------------------------------+---- [ESP32 GND]
[ESP32 GPIO10] ----------------------------------- [LED Strip Data]

[Game Buttons] ---- [ESP32 GPIO 0-3] (Red=0, Green=1, Blue=2, White=3, internal pull-up, connect to GND)
[Button LEDs] ----- [ESP32 GPIO 5-8] (Red=5, Green=6, Blue=7, White=8, OUTPUT)
```

**[WARNING]** Do not power 288 LEDs from USB! Use proper external power supply with adequate wire gauge (14-16 AWG for 288 LEDs).

---

## Game Mechanics

### Objective
Defeat the boss by shooting it with matching colors before it reaches your position at the start of the LED strip.

### Game Elements

**Player Position:**
- You occupy the first 3 LEDs (white) at position 0-2
- Each white LED represents one life
- Lose a life when the boss reaches your position

**Boss:**
- Spawns at the far end of the LED strip
- Starts with 1 segment at Level 1
- Grows by 1 segment each level (max 20 segments)
- Each segment has a random color (Red, Green, Blue, or White in 4-color mode)
- Moves toward you at increasing speed each level

**Shots:**
- Fire colored shots (Red/Green/Blue/White) from your position
- Shots travel away from you toward the boss
- Maximum 15 shots can be active at once

### Gameplay

1. **Level Start:** Boss spawns with random colored segments
2. **Identify Colors:** Watch the boss's front segment color
3. **Fire Matching Shot:** Press the corresponding color button
4. **Hit Detection:** 
   - Correct color → Front segment destroyed
   - Wrong color → Boss speeds up by 20%
5. **Victory:** Destroy all boss segments to advance to next level
6. **Defeat:** Boss reaches your position → Lose 1 life, boss respawns with 1 segment

### Difficulty Progression

**Speed Changes:**
- WS2815 (288 LEDs): Level 1 = 300ms/step → Level 6+ = 50ms/step
- WS2812B (30 LEDs): Level 1 = 1500ms/step → Level 6+ = 250ms/step

**Boss Growth:**
- Level 1: 1 segment
- Level 5: 5 segments
- Level 10: 10 segments
- Level 15+: 20 segments (maximum)

**Lives:**
- Start with 3 lives
- Lose 1 life when boss reaches you
- Visual: Red flash across strip when life lost
- Game Over: All lives lost → Explosion animation → Restart

---

## Controls

### Physical Buttons (3-Color Mode - Default)

| Button | GPIO | Color | Action |
|--------|------|-------|--------|
| 1 | 0 | Red | Fire RED shot |
| 2 | 1 | Green | Fire GREEN shot |
| 3 | 2 | Blue | Fire BLUE shot |

### Physical Buttons (4-Color Mode)

| Button | GPIO | Color | Action |
|--------|------|-------|--------|
| 1 | 0 | Red | Fire RED shot |
| 2 | 1 | Green | Fire GREEN shot |
| 3 | 2 | Blue | Fire BLUE shot |
| 4 | 3 | White | Fire WHITE shot |

### WIZ-Remote (Wireless)

| Remote Button | Action |
|---------------|--------|
| Button 1 | Fire RED shot |
| Button 2 | Fire GREEN shot |
| Button 3 | Fire BLUE shot |
| Button 4 | Fire WHITE shot (4-color mode only) |
| On | Reset game (restart from Level 1) |
| Sleep | Toggle between 3-color and 4-color mode |
| Higher | Increase LED brightness (+10%) |
| Lower | Decrease LED brightness (-10%) |
| Off | (unused) |

**Note:** Physical buttons and remote work simultaneously - use whichever is more comfortable!

---

## Color Modes

### 3-Color Mode (Default)
- Boss uses: Red, Green, Blue
- Available shots: Red, Green, Blue
- Buttons 1-3 active
- Recommended for beginners

### 4-Color Mode (Advanced)
- Boss uses: Red, Green, Blue, White
- Available shots: Red, Green, Blue, White
- Buttons 1-4 active
- Toggle with remote "Sleep" button
- Higher difficulty (more colors to match)

**Switching Modes:**
1. Press "Sleep" button on WIZ-remote
2. Serial output confirms: "[REMOTE] Sleep - Switched to X-COLOR mode"
3. Mode persists until toggled again
4. Can switch anytime (even during animations)

---

## Visual Feedback

### LED Animations

**Player Lives:**
- 3 white LEDs at strip start = 3 lives remaining
- 2 white LEDs = 2 lives remaining
- 1 white LED = 1 life remaining

**Boss Appearance:**
- Solid colored segments moving toward you
- Front segment (closest to you) is what you need to hit
- No gaps between segments

**Shots:**
- Single colored LED moving away from you
- Bright and easily visible

**Win Animation (Boss Defeated):**
- 500ms sparkle burst (white/gold flashing randomly)
- Auto-advances to next level

**Life Lost Animation:**
- 400ms full strip red flash
- 100ms blackout
- Boss respawns at far end with 1 segment

**Game Over Animation (All Lives Lost):**
- 4 seconds of red/orange flashing
- Auto-restarts game at Level 1

---

## Strategy Tips

1. **Watch the Front:** Only the front-most boss segment matters for color matching
2. **Don't Spam:** Wrong colors make the boss faster! Take time to identify the correct color
3. **Plan Ahead:** Boss segments are shown in order - prepare for the next color
4. **Use Both Hands:** Physical buttons + remote allows faster reactions
5. **Manage Speed:** Each wrong shot increases boss speed by 20% - avoid mistakes at high levels
6. **4-Color Challenge:** White segments can be hard to see against bright backgrounds - adjust brightness

---

## Troubleshooting

### LEDs Not Lighting Up
- Check power supply connections (proper voltage/current)
- Verify all grounds connected together
- Confirm LED data pin connected to GPIO 8
- Check LED strip type matches code configuration

### Buttons Not Working
- Verify buttons wired to correct GPIO pins
- Ensure buttons connect GPIO to GND (not to +5V)
- Check serial monitor for "[DEBUG] Shot fired" messages

### Remote Not Working
- Check serial monitor shows ESP32 MAC address
- Press remote buttons and watch for "[ESPNOW] Remote: XX:XX:XX..." messages
- Ensure remote has fresh batteries
- Remote works on 2.4GHz WiFi - avoid interference

### Boss Too Fast/Slow
- Adjust code configuration (see src/main.cpp)
- 288 LED setup runs 5x faster than 30 LED setup
- Consider switching LED strip configuration

### Serial Monitor Shows Garbled Text
- Set baud rate to 115200
- Ensure USB cable supports data (not charge-only)
- Try different USB port

---

## Advanced Configuration

### LED Strip Configuration

Edit `src/main.cpp` lines 8-9 to select your LED strip:

```cpp
// Uncomment ONE of these:
// #define LED_SETUP_WS2812B_30      // 30 LEDs, WS2812B strip
#define LED_SETUP_WS2815_288   // 288 LEDs, WS2815 strip
```

### Game Speed Tuning

Adjust difficulty in `src/main.cpp`:

**WS2815 (288 LEDs):**
```cpp
#define BOSS_INITIAL_SPEED 300   // Level 1 speed (ms per step)
#define BOSS_SPEED_DECREASE 30   // Speed gain per level
#define BOSS_MIN_SPEED 50        // Maximum speed (fastest)
#define SHOT_SPEED 10            // Shot movement speed
```

**WS2812B (30 LEDs):**
```cpp
#define BOSS_INITIAL_SPEED 1500  // Level 1 speed (ms per step)
#define BOSS_SPEED_DECREASE 150  // Speed gain per level
#define BOSS_MIN_SPEED 250       // Maximum speed (fastest)
#define SHOT_SPEED 50            // Shot movement speed
```

---

## Serial Monitor Debug Info

Connect to serial monitor (115200 baud) to see:

- ESP32-C3 MAC address (for remote pairing)
- Boss spawn details (segments, colors, speed)
- Shot firing events (color, position)
- Collision detection (hits, misses, wrong colors)
- Life loss events (position, remaining lives)
- Level progression
- Remote button presses
- Color mode changes

**Useful for:**
- Verifying remote functionality
- Understanding game mechanics
- Debugging hardware issues
- Tracking high score progress

---

## Safety Warnings

1. **[WARNING]** 288 LEDs at full brightness draw ~17A - use proper power supply and wire gauge
2. **[WARNING]** Do not power LED strips from ESP32 USB port - can damage board
3. **[WARNING]** Connect all grounds together (power supply, ESP32, LED strip)
4. **[WARNING]** GPIO2 is strapping pin - avoid holding Button 3 during ESP32 boot
5. **[WARNING]** Ensure proper ventilation - LEDs and power supply generate heat

---

## Credits

**Game Design:** RGB Guardian  
**Platform:** ESP32-C3 SuperMini + PlatformIO  
**LED Library:** FastLED 3.10.3  
**Wireless:** ESP-NOW protocol  
**Date Created:** February 2026  

---

**Have fun defending against the RGB invasion!**
