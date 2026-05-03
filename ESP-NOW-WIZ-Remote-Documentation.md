# ESP-NOW Wireless Control Documentation

**Date:** May 3, 2026  
**Project:** RGB Guardian  
**Purpose:** Reference guide for the current ESP-NOW controller and Player-2 implementation

---

## Current Hardware Roles

### Controller Receiver
- Board: ESP32-C3 SuperMini
- Firmware: `src/controller.cpp`
- Role: Main LED game controller and ESP-NOW receiver
- Serial output shows:
  - local controller MAC address
  - configured allowed sender MAC slots
  - unknown sender MAC discovery messages
  - accepted packet sender slot numbers

### Supported Senders
- WIZ-Remote
- Optional Player-2 ESP32 controller running `src/player-2.cpp`

### Current Allowlist Model
- Controller supports up to 2 allowed sender MAC addresses
- Unknown sender MACs are printed on Serial and ignored until added to the allowlist
- Leave unused allowlist slots as `00:00:00:00:00:00`

---

## Current Protocol Usage

### Packet Structure
- Packet length: 13 bytes
- Transport: ESP-NOW
- Key fields currently used by the controller:
  - `Byte[1]`: sequence counter
  - `Byte[6]`: command / button code

### Button / Command Mapping

| Sender | Supported codes in current firmware | Notes |
|--------|-------------------------------------|-------|
| WIZ-Remote | `0x01`, `0x02`, `0x03`, `0x08`, `0x09`, `0x10`, `0x11`, `0x12`, `0x13` | Full remote support |
| Player-2 | `0x10`, `0x11`, `0x12`, `0x13` | Color shots only |

### Color Shot Mapping

| Code | Meaning |
|------|---------|
| `0x10` | RED shot |
| `0x11` | GREEN shot |
| `0x12` | BLUE shot |
| `0x13` | WHITE shot |

### System Command Mapping

| Code | Meaning |
|------|---------|
| `0x01` | On / reset game |
| `0x02` | Off / cycle game mode |
| `0x03` | Sleep / toggle 3-color vs 4-color |
| `0x08` | Lower brightness |
| `0x09` | Higher brightness |

---

## Current Controller Behavior

### MAC Filtering
- The receive callback checks the sender MAC against the configured allowlist
- If no slot matches, the packet is ignored
- Unknown sender MACs are queued and printed from the main loop so they can be copied safely from Serial

### Debouncing
- WIZ-Remote sends repeated packets for one press
- The controller now tracks the last seen sequence number per sender slot, not globally
- This allows WIZ-Remote and Player-2 to operate at the same time without suppressing each other

### Command Queue
- Accepted commands are pushed into a small FIFO queue before processing
- This avoids one sender overwriting another sender's pending command if buttons are pressed close together
- If the queue overflows, the controller prints a warning on Serial

---

## Current Player-2 Behavior

### Transmitter Role
- Board: ESP32-C3 SuperMini
- Firmware: `src/player-2.cpp`
- Sends WIZ-compatible 13-byte packets
- Uses GPIO0-3 for active-low button inputs
- Uses GPIO5-8 for illuminated button LEDs
- Button LEDs are ON when idle and OFF while pressed

### Current Limits
- Player-2 currently sends only RED / GREEN / BLUE / WHITE shot commands
- Player-2 does not currently send reset, mode, brightness, or settings commands
- Player-2 currently transmits to broadcast during bring-up
- After MAC discovery, it can later be changed to target the controller MAC directly if desired

---

## Serial Output You Should Expect

### Controller Boot
```text
[ESPNOW] WiFi initialized in Station mode
[ESPNOW] ESP32-C3 MAC Address: XX:XX:XX:XX:XX:XX
[ESPNOW] ESP-NOW initialized successfully
[ESPNOW] Allowed sender MACs:
[ESPNOW]   Slot 1: XX:XX:XX:XX:XX:XX
[ESPNOW]   Slot 2: <empty>
[ESPNOW] Receive callback registered
```

### Unknown Sender Discovery
```text
[ESPNOW] Unknown sender MAC detected: XX:XX:XX:XX:XX:XX (len: 13)
[ESPNOW] Add this MAC to ALLOWED_REMOTE_MACS in src/controller.cpp to authorize it.
```

### Accepted Packet
```text
[ESPNOW] Remote slot 2: XX:XX:XX:XX:XX:XX, Seq: 17, Button: 0x10
```

### Player-2 Boot
```text
[TX] WiFi initialized in Station mode
[TX] Local MAC: XX:XX:XX:XX:XX:XX
[TX] ESP-NOW initialized successfully
[TX] Destination MAC: FF:FF:FF:FF:FF:FF [broadcast]
[TX] Ready - press GPIO0-3 buttons to send color shots
```

---

## MAC Discovery Workflow

1. Upload the controller firmware.
2. Open Serial Monitor at `115200`.
3. Press a button on the WIZ-Remote or Player-2.
4. Watch for `Unknown sender MAC detected: ...`.
5. Copy that MAC into the next free slot in `ALLOWED_REMOTE_MACS` inside `src/controller.cpp`.
6. Rebuild and upload the controller.

---

## Troubleshooting

### No Wireless Input Is Accepted
- Confirm controller Serial shows ESP-NOW initialization success
- Confirm the sender MAC is in an allowed slot
- If not, use the discovery workflow above
- Check that the sender is powered and in range

### WIZ-Remote Works But Player-2 Does Not
- Confirm Player-2 Serial shows its local MAC and successful ESP-NOW startup
- Confirm the controller printed the Player-2 MAC during discovery
- Confirm the Player-2 MAC was added to the allowlist
- Confirm you are testing color buttons only; Player-2 does not send system commands yet

### Inputs Seem To Be Missed During Heavy Button Use
- The controller now uses per-sender sequence tracking and a command queue
- If you see a queue overflow warning, reduce burst rate or increase queue size in code

### MAC Address Shows `00:00:00:00:00:00`
- Ensure WiFi was put into `WIFI_STA` mode before reading the MAC
- Add a short delay after WiFi initialization if needed

---

## Next Logical Improvements

- Replace Player-2 broadcast transmission with a fixed controller destination MAC after bring-up
- Add controller-side configuration for more than 2 sender slots if needed
- Add optional acknowledgements or delivery status for Player-2
- Add Player-2 support for reset / mode / brightness commands if you want feature parity with the WIZ-Remote

---

**Document Version:** 2.0  
**Last Updated:** May 3, 2026
```
