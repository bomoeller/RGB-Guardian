# RGB Guardian - Wemos D1 Mini32

PlatformIO project for RGB Guardian with ESP32 (WROOM-32), WIZ-Remote support, illuminated button outputs, and 8 game modes.

Firmware layout:
- Controller: LED controller and Player-1 interface (COM25)
- Player-2: Secondary wireless controller with 4 illuminated buttons sending color-shot presses to the primary controller (COM26)

Current wireless status:
- Controller accepts up to 2 allowed ESP-NOW sender MAC addresses (WIZ remotes via allowlist).
- Player-2 is identified by packet magic bytes (`'P','2'`) — no MAC lock needed, easy board swap.
- Unknown sender MACs are printed on Serial so new remotes can be discovered and added to the allowlist.
- Remote packet sequence debouncing is tracked per sender slot, so WIZ remote and Player-2 can operate at the same time.
- Player-2 sends only the 4 color-shot buttons (RED/GREEN/BLUE/WHITE) via unicast to the hardcoded controller MAC.
- Controller also sends Player-2 control packets (v2 `LED_STATE`) for mode-aware button-light behavior.

## Current Setup

- Board: Wemos D1 Mini32 (ESP-WROOM-32, dual-core 240 MHz, 4 MB flash)
- USB chip: CH9102 — stable COM port, does not re-enumerate during flash
- Active strip profile: WS2815 (300 max LEDs initialized, default active length 288)
- LED data pin: GPIO33
- LED strip power: external 12 V power supply
- ESP32 power: USB (5 V)
- Controller MAC: `84:1F:E8:39:AC:1C` (COM25)
- Player-2 MAC:   `84:1F:E8:39:D2:48` (COM26)

Optional alternate setup in code:
- WS2812B (30 LEDs, 5 V)

## GPIO Mapping — Controller

Inputs (active-low with internal pull-ups):
- GPIO16: Red button
- GPIO17: Green button
- GPIO21: Blue button
- GPIO22: White button (4-color modes)
- GPIO0  (IO0): BOOT button (mode cycle)

Outputs:
- GPIO33: LED strip data
- GPIO23: Red button LED
- GPIO19: Green button LED
- GPIO18: Blue button LED
- GPIO26: White button LED
- GPIO27: Piezo speaker — reserved, not yet wired

## GPIO Mapping — Player-2

Inputs (active-low with internal pull-ups):
- GPIO16: Red button
- GPIO17: Green button
- GPIO21: Blue button
- GPIO22: White button

Outputs (active-LOW electrical drive; visible behavior is mode-policy driven):
- GPIO23: Red button LED
- GPIO19: Green button LED
- GPIO18: Blue button LED
- GPIO26: White button LED

Note:
- During active controller link, Player-2 LEDs follow controller-driven mode policy.
- If control packets time out, Player-2 falls back to local button feedback.

## Wiring

```text
Wemos D1 Mini32 (controller) -> LED strip
GPIO33                        -> DIN (data in)
GND                           -> strip GND

WS2815 power supply (external)
+12V                          -> strip +12V
GND                           -> strip GND

Important: ESP32 GND and strip power GND must be connected together.
```

## Game Modes (Remote Off cycles)

1. 1 Player - Normal
2. 1 Player - Follow-Me
3. 1 Player - Memory
4. 1 Player - Ghost Boss
5. 2 Player - Duel
6. 2 Player - Co-Play
7. 2 Player - All-vs-All
8. 2 Player - Pong Duel

Mode indicator:
- Far-end dark yellow (orange-ish) dots show mode number for 2 seconds before mode starts.

## Wired Settings Mode (Physical Buttons)

Enter settings:
- Hold RED + WHITE for 3 seconds
- Requirement: no shots fired in the last 3 seconds

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
```

Fresh upload workflow (one board at a time):
1. Connect only controller board (COM25) over USB.
2. Upload controller:
	```powershell
	$env:PYTHONIOENCODING="utf-8"; pio run -e controller --target upload > flash_controller.txt 2>&1; Get-Content flash_controller.txt | Select-String "SUCCESS|FAILED|Hard reset|Error"
	```
3. Disconnect controller USB.
4. Connect only player-2 board (COM26) over USB.
5. Upload player-2:
	```powershell
	$env:PYTHONIOENCODING="utf-8"; pio run -e player-2 --target upload > flash_player2.txt 2>&1; Get-Content flash_player2.txt | Select-String "SUCCESS|FAILED|Hard reset|Error"
	```
6. Reconnect both boards after flashing and start monitors.

Safe flash (avoids Unicode corruption of esptool output on Windows):
```powershell
$env:PYTHONIOENCODING="utf-8"; pio run -e controller --target upload > flash_out.txt 2>&1; Get-Content flash_out.txt | Select-String "SUCCESS|FAILED|Hard reset|Error"
```

PlatformIO environments:
- `controller` -> `src/controller.cpp`  (upload port COM25)
- `player-2`   -> `src/player-2.cpp`   (upload port COM26)

Current Player-2 behavior:
- 4 active-low buttons on GPIO16/17/21/22
- 4 illuminated button outputs on GPIO23/19/18/26
- Sends v1 button-event packets (`0x10..0x13`) to controller
- Receives v2 control packets (`LED_STATE`) from controller for mode-aware button lights
- LED behavior is policy-driven per mode (guided-only, press-off, press-on)
- Local toggle capability exists in protocol support but is not assigned to gameplay modes by default
- Sends unicast ESP-NOW packets to controller MAC `84:1F:E8:39:AC:1C`

Current Pong Duel behavior:
- Local Player-1 uses the RED button
- Local controller fallback inputs are disabled for Player-2 side
- Wireless Player-2 uses BLUE only (WIZ remote BLUE or Player-2 BLUE)
- Match format is first to 5 points with a center serve and shrinking hit zones

Platform notes:
- Uses standard `espressif32` platform. Arduino-ESP32 v3.x / IDF5.
- ESP-NOW broadcast receive is broken in IDF5 — unicast only between controller and Player-2.
- Power save must be disabled (`esp_wifi_set_ps(WIFI_PS_NONE)`) for reliable ESP-NOW RX.
- GPIO6-11 are internal flash on ESP-WROOM-32 — never use for GPIO. GPIO33 is used for LED strip data in this wiring.

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

- WS2815 uses 12 V strip power.
- WS2812B uses 5 V strip power.
- Do not power the LED strip from the ESP32 USB port.
- Use a PSU and wire gauge sized for your strip length and brightness.