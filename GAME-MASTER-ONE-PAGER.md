# RGB Guardian - Game Master One-Page Guide

## 1) Core Controls (What players use)

### Physical buttons
- Button 1 (GPIO0): Red shot
- Button 2 (GPIO1): Green shot
- Button 3 (GPIO2): Blue shot
- Button 4 (GPIO3): White shot (only in 4-color mode)

### WIZ-Remote
- 1/2/3/4: Fire Red/Green/Blue/White
- Sleep: Toggle 3-color <-> 4-color mode
- Off: Cycle game modes (1-8)
- On: Restart game from Level 1
- Higher/Lower: Brightness up/down

---

## 2) Settings Mode (No remote needed)

### Enter settings
- Hold RED + WHITE for 1 second
- Requirement: no shots fired during the last 1 second

### In settings
- Green: mode +
- Blue: mode -
- White short: toggle 3-color / 4-color
- Red short: save and exit
- Red long (>3s): restart current mode
- Auto save/exit after 10s inactivity

### LED length adjust
- White long: enter LED length adjust
- Green/Blue short: +1 / -1 LED
- Green/Blue hold: faster stepping
- White press: exit LED length adjust
- Red save: apply and restart if length changed

Tip: mode indicator dots always show at the active strip end.

---

## 3) Game Modes (Off button cycles)

1. INVERTED - Button LEDs are ON by default and turn OFF when pressed.
2. PRESS-TO-LIGHT - Button LEDs light only while a button is pressed.
3. FOLLOW-ME - Helper lighting shows the next likely correct color.
4. MEMORY - System plays a sequence first, then players repeat with inputs.
5. GHOST BOSS - Boss visibility is limited, requiring memory and timing.
6. DUEL - Two players compete from opposite ends; first to survive/win conditions.
7. CO-PLAY - Two players cooperate against an expanding shared boss.
8. ALL-VS-ALL - Two players and boss conflict; shots can also eliminate opponent.

Mode 8 quick rule:
- Same-color shots cancel each other.
- Different-color shots pass through.
- If boss reaches one side, both players lose that round.

---

## 4) How to Host a Round (Script for game masters)

1. Explain: "Match the front boss color to destroy it."
2. Start in 3-color mode for beginners.
3. Let players try 2-3 levels.
4. Increase challenge: 4-color mode, then mode 5/6/8.
5. For groups: use mode 7 or 8.
6. If confusion: restart with remote On.