# ESP-NOW Wireless Control Documentation

**Date:** May 31, 2026
**Project:** RGB Guardian
**Purpose:** Reference guide for the current ESP-NOW controller and Player-2 implementation

---

## Current Hardware Roles

### Controller Receiver + Player-2 Control Sender
- Board: Wemos D1 Mini32 (ESP-WROOM-32)
- Firmware: `src/controller.cpp`
- COM port: COM25
- MAC: `84:1F:E8:39:AC:1C`
- Role: Main LED game controller, ESP-NOW receiver, and Player-2 LED policy sender
- Serial output shows:
  - Local controller MAC address
  - Configured allowed sender MAC slots (WIZ remotes)
  - Unknown sender MAC discovery messages
  - Accepted packet sender slot numbers and Player-2 packets

### Supported Senders
- WIZ-Remote (13-byte WIZ-format packets, identified by MAC allowlist)
- Player-2 ESP32 controller running `src/player-2.cpp` (identified by packet magic bytes `'P','2'`)

### Current Allowlist Model
- Controller supports up to 2 allowed WIZ-remote MAC addresses
- Player-2 does NOT need an allowlist entry — identified by the `'P','2'` magic header
- Unknown non-Player-2 sender MACs are printed on Serial and ignored until added to the allowlist
- Leave unused allowlist slots as `00:00:00:00:00:00`

---

## ESP-NOW Configuration

### Channel and Mode
- WiFi mode: `WIFI_STA` (station, no AP)
- Channel: 1 (fixed, both controller and Player-2)
- Power save: disabled (`esp_wifi_set_ps(WIFI_PS_NONE)`) — required for reliable RX
- Init sequence: `WiFi.mode(WIFI_STA)` → `esp_wifi_set_ps(WIFI_PS_NONE)` → `esp_now_init()` → `esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)`

### Unicast Only
- ESP-NOW broadcast receive does not fire in Arduino-ESP32 v3.x / IDF5
- All packets must be unicast
- Player-2 uses a hardcoded controller MAC peer (`84:1F:E8:39:AC:1C`, channel 1, `WIFI_IF_STA`)
- Controller registers a Player-2 peer for outbound control packets (fallback MAC plus dynamic sender rebind)

### Callback Signatures (IDF5 / Arduino-ESP32 v3.x)
```cpp
// Receive callback — use esp_now_recv_info_t*
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

// Send callback — use two-argument form (NOT esp_now_send_info_t*)
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status);
```

---

## Current Protocol Usage

### WIZ-Remote Packet Structure
- Packet length: 13 bytes
- Transport: ESP-NOW unicast
- Key fields currently used by the controller:
  - `Byte[1]`: sequence counter (per-sender debounce)
  - `Byte[6]`: command / button code

### Player-2 Packet Structure (`include/player2_espnow_packet.h`)
- v1 packet (5 bytes): magic0='P', magic1='2', version=1, sequence, buttonCode
- v2 packet (8 bytes): magic0, magic1, version=2, packetType, sequence, arg0, arg1, flags
- Packet types currently used:
  - `BUTTON_EVENT` for color-shot button ingress
  - `LED_STATE` for controller-driven Player-2 button-light behavior
  - `BEEP_CMD` reserved for future predefined sounds
- Identified by magic bytes — any board running `player-2.cpp` is accepted without MAC registration

### Button / Command Mapping

| Sender | Supported codes in current firmware | Notes |
|--------|-------------------------------------|-------|
| WIZ-Remote | `0x01`, `0x02`, `0x03`, `0x08`, `0x09`, `0x10`, `0x11`, `0x12`, `0x13` | Full remote support |
| Player-2   | `0x10`, `0x11`, `0x12`, `0x13` | Color shots only |

### Color Shot Mapping

| Code   | Meaning    |
|--------|------------|
| `0x10` | RED shot   |
| `0x11` | GREEN shot |
| `0x12` | BLUE shot  |
| `0x13` | WHITE shot |

### System Command Mapping

| Code   | Meaning                        |
|--------|--------------------------------|
| `0x01` | On / reset game                |
| `0x02` | Off / cycle game mode          |
| `0x03` | Sleep / toggle 3-color vs 4-color |
| `0x08` | Lower brightness               |
| `0x09` | Higher brightness              |

---

## Current Controller Behavior

### Receive Flow
1. `onDataRecv` fires on every incoming packet
2. Magic-byte check: if first two bytes are `'P'` and `'2'` → handle as Player-2 packet
3. Otherwise: check sender MAC against WIZ-remote allowlist
4. Matched WIZ-remote → push to command queue with per-sender sequence debounce
5. No match → queue unknown MAC report (printed from main loop)

### MAC Filtering (WIZ-Remote)
- The receive callback checks the sender MAC against the configured allowlist
- If no slot matches and packet is not Player-2, the packet is ignored
- Unknown sender MACs are queued and printed from the main loop so they can be copied from Serial

### Debouncing
- WIZ-Remote sends repeated packets for one press
- The controller tracks the last seen sequence number per sender slot, not globally
- This allows WIZ-Remote and Player-2 to operate at the same time without suppressing each other

### Command Queue
- Accepted commands are pushed into a small FIFO queue before processing
- This avoids one sender overwriting another sender's pending command if buttons are pressed close together
- If the queue overflows, the controller prints a warning on Serial

### Player-2 LED Control Path
- Controller computes mode-aware LED policy and sends v2 `LED_STATE` packets on change and heartbeat.
- `LED_STATE` fields:
  - `arg0`: base RGBW visible mask
  - `arg1`: RGBW mask where local press overlay is enabled
  - `flags`: policy bits (`PRESS_TURNS_OFF`, `PRESS_TURNS_ON`, optional `LOCAL_TOGGLE` support)
- Controller forces LED control refresh on mode transitions to keep Player-2 behavior aligned immediately.

---

## Current Player-2 Behavior

### Transmitter Role
- Board: Wemos D1 Mini32 (ESP-WROOM-32)
- Firmware: `src/player-2.cpp`
- COM port: COM26
- MAC: `84:1F:E8:39:D2:48`
- Sends v1 button-event packets (unicast)
- Uses GPIO16/17/21/22 for active-low button inputs
- Uses GPIO23/19/18/26 for illuminated button LEDs
- Receives v2 controller LED policy packets and applies mode-aware button-light behavior while fresh
- Falls back to local button-feedback LED behavior when control packets time out

### Current Limits
- Player-2 sends only RED / GREEN / BLUE / WHITE shot commands
- Player-2 does not currently send reset, mode, brightness, or settings commands

---

## Serial Output You Should Expect

### Controller Boot
```text
[ESPNOW] WiFi initialized in Station mode
[ESPNOW] MAC Address: 84:1F:E8:39:AC:1C
[ESPNOW] ESP-NOW initialized successfully
[ESPNOW] Allowed sender MACs:
[ESPNOW]   Slot 1: 98:77:D5:98:33:8A
[ESPNOW]   Slot 2: <empty>
[ESPNOW] Receive callback registered
```

### Unknown WIZ-Remote Discovery
```text
[ESPNOW] Unknown sender MAC detected: XX:XX:XX:XX:XX:XX (len: 13)
[ESPNOW] Add this MAC to ALLOWED_REMOTE_MACS in src/controller.cpp to authorize it.
```

### Accepted WIZ-Remote Packet
```text
[ESPNOW] Remote slot 1: 98:77:D5:98:33:8A, Seq: 13, Button: 0x10
```

### Accepted Player-2 Packet
```text
[ESPNOW] Player-2 packet: 84:1F:E8:39:D2:48, Seq: 0, Button: 0x10 [new sender]
```

### Player-2 Boot
```text
[TX] WiFi initialized in Station mode
[TX] Local MAC: 84:1F:E8:39:D2:48
[TX] ESP-NOW initialized successfully
[TX] Destination MAC (unicast): 84:1F:E8:39:AC:1C
[TX] Ready - press GPIO16/17/21/22 buttons to send color shots
```

### Player-2 Successful Send
```text
[TX] Send SUCCESS dest=84:1F:E8:39:AC:1C
```

---

## MAC Discovery Workflow (WIZ-Remote)

1. Upload the controller firmware.
2. Open Serial Monitor at `115200`.
3. Press a button on the WIZ-Remote.
4. Watch for `Unknown sender MAC detected: ...`.
5. Copy that MAC into the next free slot in `ALLOWED_REMOTE_MACS` inside `src/controller.cpp`.
6. Rebuild and upload the controller.

Note: Player-2 does NOT require this workflow — it is auto-accepted by magic bytes.

---

## Troubleshooting

### No Wireless Input Is Accepted
- Confirm controller Serial shows ESP-NOW initialization success
- For WIZ-Remote: confirm sender MAC is in an allowed slot; use discovery workflow if not
- For Player-2: confirm Player-2 Serial shows `Send SUCCESS` on button press
- Check that the sender is powered and in range

### WIZ-Remote Works But Player-2 Does Not
- Confirm Player-2 Serial shows its local MAC and successful ESP-NOW startup
- Confirm Player-2 sends to the correct controller MAC (`84:1F:E8:39:AC:1C`)
- Confirm both boards are on channel 1
- Player-2 does not need a MAC allowlist entry — accepted by magic bytes

### Inputs Seem To Be Missed During Heavy Button Use
- The controller uses per-sender sequence tracking and a command queue
- If you see a queue overflow warning, reduce burst rate or increase queue size in code

### MAC Address Shows `00:00:00:00:00:00`
- Ensure WiFi was put into `WIFI_STA` mode before reading the MAC
- Add a short delay after WiFi initialization if needed

---

## Known Platform Constraints (Arduino-ESP32 v3.x / IDF5)

- **Broadcast receive broken:** `FF:FF:FF:FF:FF:FF` broadcast receive callback never fires — use unicast
- **Send callback signature:** `(const uint8_t* mac_addr, esp_now_send_status_t status)` — `esp_now_send_info_t*` does not exist in IDF5
- **Channel must be set AFTER `esp_now_init()`:** calling `esp_wifi_set_channel()` before init is silently ignored
- **Power save must be off:** `esp_wifi_set_ps(WIFI_PS_NONE)` required before ESP-NOW init
- **GPIO6-11 reserved on ESP-WROOM-32:** internal flash — never use for GPIO; GPIO33 is used for LED strip data in this project

---

## Known MACs

| Device | MAC | Port |
|--------|-----|------|
| Controller | `84:1F:E8:39:AC:1C` | COM25 |
| Player-2   | `84:1F:E8:39:D2:48` | COM26 |
| WizRemote 1 | `98:77:D5:98:33:8A` | — |
| Unknown remote seen | `44:4F:8E:BE:37:3D` | — (not allowlisted) |

---

## Next Logical Improvements

- Add Player-2 support for reset / mode / brightness commands for feature parity with WIZ-Remote
- Add controller-side configuration for more than 2 WIZ-remote sender slots if needed
- Add optional delivery acknowledgements for Player-2
- Implement piezo speaker feedback on GPIO27 (defined but not yet wired)

---

**Document Version:** 3.1
**Last Updated:** May 31, 2026