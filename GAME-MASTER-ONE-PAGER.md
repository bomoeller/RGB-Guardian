# RGB Guardian - Game Master One-Page Guide

## 1) Core Controls (What players use)

### Physical buttons (Wemos D1 Mini32)
- Button 1 (GPIO16): Red shot
- Button 2 (GPIO17): Green shot
- Button 3 (GPIO21): Blue shot
- Button 4 (GPIO22): White shot (only in 4-color mode)

### WIZ-Remote
- 1/2/3/4: Fire Red/Green/Blue/White
- Sleep: Toggle 3-color <-> 4-color mode
- Off: Cycle game modes (1-8)
- On: Restart game from Level 1
- Higher/Lower: Brightness up/down

### Optional Player-2 controller (Wemos D1 Mini32, COM26)
- Same 4 color-shot buttons as Player-1 / remote
- Button LEDs are controller-driven by current mode policy while link is active
- If control packets time out, LEDs fall back to local button feedback
- Sends color shots only — no mode, brightness, or reset commands

---

## 2) Settings Mode (No remote needed)

### Enter settings
- Hold RED + WHITE for 3 seconds
- Requirement: no shots fired during the last 3 seconds

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

1. 1 Player - Normal - Button LEDs are ON by default and turn OFF when pressed.
2. 1 Player - Follow-Me - Helper lighting shows the next likely correct color.
3. 1 Player - Memory - System plays a sequence first, then players repeat with inputs.
4. 1 Player - Ghost Boss - Boss visibility is limited, requiring memory and timing.
5. 2 Player - Duel - Two players compete from opposite ends; first to survive/win conditions.
6. 2 Player - Co-Play - Two players cooperate against an expanding shared boss.
7. 2 Player - All-vs-All - Two players and boss conflict; shots can also eliminate opponent.
8. 2 Player - Pong Duel - Two players rally one ball back and forth and race to 5 points.

Mode 7 quick rule:
- Same-color shots cancel each other.
- Different-color shots pass through.
- If boss reaches one side, both players lose that round.

Mode 8 quick rule:
- Local Player-1 uses RED.
- Local fallback Player-2 on controller is disabled.
- Wireless Player-2 uses BLUE only (WIZ remote BLUE or Player-2 ESP BLUE).
- Hit the ball only when it is in your side zone.
- Early-hit bonus area is the center-side part of your zone (about the inner 1/3), shown brighter during ball movement.
- Returning from that brighter area speeds the ball up more; ball speed resets to base on each new serve.

---

## 4) How to Host a Round (Script for game masters)

1. Explain: "Match the front boss color to destroy it."
2. Start in 3-color mode for beginners.
3. Let players try 2-3 levels.
4. Increase challenge: 4-color mode, then mode 4/5/7.
5. For groups: use mode 6 or 7.
6. If confusion: restart with remote On.