# RGB Guardian - How to Play

**Version:** 1.2  
**Date:** March 12, 2026

---

## Overview

RGB Guardian is a color-matching LED strip game where you defend your position by shooting colored projectiles at an advancing boss. Match the boss's colors to destroy it segment by segment before it reaches you!

---

## Hardware Setup

### Required Components

1. **ESP32-C3 SuperMini** (microcontroller)
2. **WS2815 LED Strip** - up to 300 LEDs (default active length 288) (or WS2812B with 30 LEDs)
3. **5 Push Buttons** (for color shooting)
4. **WIZ-Remote** (optional - for wireless control)
5. **Optional second ESP32-C3 for Player-2** (secondary wireless controller with 4 illuminated buttons)
6. **Power Supply:**
   - WS2815 (up to 300 LEDs): 12V external PSU (size current by strip spec)
   - WS2812B (30 LEDs): 5V/2A minimum
7. **Jumper wires**

### Physical Connections

**LED Strip:**
- LED Data Pin → ESP32-C3 GPIO10
- WS2815 +12V → External 12V power supply
- LED GND → Common ground (ESP32 + Power Supply)

**Buttons (active-low with internal pull-ups):**
- Button 1 (RED) → ESP32-C3 GPIO 0 → GND when pressed
- Button 2 (GREEN) → ESP32-C3 GPIO 1 → GND when pressed
- Button 3 (BLUE) → ESP32-C3 GPIO 2 → GND when pressed
- Button 4 (WHITE) → ESP32-C3 GPIO 3 → GND when pressed

**Power:**
- ESP32-C3 powered via USB (5V)
- LED strip powered from external supply
- **CRITICAL:** Connect all grounds together (ESP32 GND + LED GND + Power Supply GND)

### Wiring Diagram Notes

```
[12V Power Supply] ---+---- [LED Strip +12V]
                      |
                      +---- [Common GND] ----+---- [LED Strip GND]
                                             |
[ESP32-C3 USB] -------------------------------+---- [ESP32 GND]
[ESP32 GPIO10] ----------------------------------- [LED Strip Data]

[Game Buttons] ---- [ESP32 GPIO 0-3] (Red=0, Green=1, Blue=2, White=3, internal pull-up, connect to GND)
[Button LEDs] ----- [ESP32 GPIO 5-8] (Red=5, Green=6, Blue=7, White=8, OUTPUT)
```

**Optional Player-2 controller:**
- Uses the same button GPIO mapping as Player-1: GPIO0-3
- Uses the same illuminated button outputs: GPIO5-8
- Button LEDs are ON while idle and turn OFF while their button is pressed
- Currently sends only the 4 color shots to the main controller

**[WARNING]** Do not power high LED counts from USB! Use proper external power supply with adequate wire gauge (14-16 AWG for long strips).

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
- WS2815 (active length default 288, configurable up to 300): Level 1 = 300ms/step → Level 6+ = 50ms/step
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
| Off | Cycle Game Mode (1-8) |

**Note:** Physical buttons and remote work simultaneously - use whichever is more comfortable!

### Player-2 Controller (Wireless Secondary Controller)

| Player-2 Button | Action |
|-----------------|--------|
| RED | Fire RED shot |
| GREEN | Fire GREEN shot |
| BLUE | Fire BLUE shot |
| WHITE | Fire WHITE shot (4-color mode only) |

**Current status:**
- Button LEDs use GPIO5-8 and are lit until pressed
- Player-2 sends the same 4 color-shot packet codes as the WIZ remote
- Player-2 does not currently send mode, brightness, reset, or settings commands

### Pong Duel Controls

When **PONG DUEL** mode is active:
- **Local Player-1:** RED button
- **Local fallback Player-2:** WHITE button
- **Wireless Player-2:** any WIZ remote color button or any Player-2 controller color button
- GREEN and BLUE on the main controller are not used for local pong play

### Wired Settings Mode (Physical Buttons)

You can configure gameplay without the remote:

**Enter settings:**
- Hold **RED + WHITE** for 1 second
- Requirement: no shots fired in the last 1 second

**Inside settings:**
- **GREEN**: Mode up
- **BLUE**: Mode down
- **WHITE (short press)**: Toggle 3-color / 4-color
- **RED (short press)**: Save and exit
- **RED (long press >3s)**: Restart current mode immediately
- **Auto-exit**: 10 seconds inactivity (applies current selection)

**LED length adjust sub-mode:**
- **WHITE (long press)**: Enter LED length adjust
- **GREEN/BLUE short press**: +/- 1 LED
- **GREEN/BLUE long press**: faster repeated change in larger steps
- **WHITE press**: Exit LED length adjust sub-mode
- **RED save**: Applies length and restarts game when length changed

---

## Game Modes

Use remote **Off** button to cycle game modes. The strip shows lilac mode-dots for 2 seconds before mode starts.

1. **INVERTED** - Button LEDs on when not pressed
2. **FOLLOW-ME** - Helper shows next target color
3. **MEMORY** - Sequence playback, then player input
4. **GHOST BOSS** - Boss visibility challenge
5. **DUEL** - 2-player versus from both ends
6. **CO-PLAY** - 2-player cooperative expanding boss
7. **ALL-VS-ALL** - 2 players versus each other and the boss
8. **PONG DUEL** - 2-player timing rally on one strip

**Mode 7 (ALL-VS-ALL) rules:**
- Boss expands from center like CO-PLAY
- If boss reaches either side, both players lose immediately
- Same-color shots cancel each other; different-color shots pass through
- A shot that reaches the opposite player side can eliminate that player and end the round

**Mode 8 (PONG DUEL) rules:**
- Ball starts near the center after a short serve countdown
- Player-1 protects the near end, Player-2 protects the far end
- Press when the ball is inside your hit zone to return it
- Each successful return speeds the ball up
- A miss or mistimed press gives the point to the other player
- First player to 5 points wins the match
- Hit zones shrink as each player's score rises

### 3-color / 4-color toggle

- Remote **Sleep** toggles between 3-color and 4-color palettes
- 3-color: Red/Green/Blue
- 4-color: Red/Green/Blue/White

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
- Confirm LED data pin connected to GPIO10
- Check LED strip type matches code configuration

### Buttons Not Working
- Verify buttons wired to correct GPIO pins
- Ensure buttons connect GPIO to GND (not to +5V)
- Check serial monitor for "[DEBUG] Shot fired" messages

### Wireless Remote / Player-2 Not Working
- Check serial monitor shows the controller ESP32 MAC address and allowed sender MAC slots
- Press remote or Player-2 buttons and watch for "[ESPNOW] Remote slot ..." messages
- If a sender is not yet authorized, look for "[ESPNOW] Unknown sender MAC detected: ..."
- Add the discovered sender MAC to the controller allowlist when needed
- Ensure remote batteries are fresh and the Player-2 board is powered
- Wireless control uses 2.4GHz WiFi / ESP-NOW - avoid interference

### Boss Too Fast/Slow
- Adjust code configuration (see src/controller.cpp)
- 288 LED setup runs 5x faster than 30 LED setup
- Consider switching LED strip configuration

### Serial Monitor Shows Garbled Text
- Set baud rate to 115200
- Ensure USB cable supports data (not charge-only)
- Try different USB port

---

## Advanced Configuration

### LED Strip Configuration

Edit `src/controller.cpp` lines 11-12 to select your LED strip:

```cpp
// Uncomment ONE of these:
// #define LED_SETUP_WS2812B_30      // 30 LEDs, WS2812B strip
#define LED_SETUP_WS2815_288   // WS2815 profile (300 max initialized, 288 default active)
```

**Important:**
- `NUM_LEDS` (hardware max initialized) is **300** for WS2815 profile
- Default gameplay length starts at **288** (`activeLedCount`)
- You can change active length in wired settings mode (LED length adjust)

### Game Speed Tuning

Adjust difficulty in `src/controller.cpp`:

**WS2815 (up to 300 LEDs, default active 288):**
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
- Allowed ESP-NOW sender MAC slots
- Unknown sender MAC discovery messages for new remotes / Player-2 boards
- Boss spawn details (segments, colors, speed)
- Shot firing events (color, position)
- Collision detection (hits, misses, wrong colors)
- Life loss events (position, remaining lives)
- Level progression
- Remote / Player-2 button presses, including sender slot number
- Color mode changes

**Useful for:**
- Verifying remote functionality
- Identifying Player-2 MAC address before allowlisting it
- Understanding game mechanics
- Debugging hardware issues
- Tracking high score progress

---

## Safety Warnings

1. **[WARNING]** WS2815 strip uses 12V external power, not 5V
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
