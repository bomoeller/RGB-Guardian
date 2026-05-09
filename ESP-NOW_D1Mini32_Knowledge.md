# ESP-NOW Knowledge File — D1 Mini32 (WROOM-32)

**Validated:** 2026-05-09 | Platform: PlatformIO + Arduino-ESP32 v3.x (IDF 5.x)

---

## Hardware That Works

**Board:** Wemos D1 Mini32 V1.0.0 (ESP-WROOM-32 module)
- Dual-core Xtensa LX6, 240 MHz, 4 MB Flash, 520 KB SRAM
- USB-UART chip: **CH9102** (shows as "USB-Enhanced-SERIAL CH9102" in Device Manager)
- COM port is **stable** — does NOT re-enumerate during flash (unlike ESP32-C3 USB-JTAG)
- Onboard LED: `LED_BUILTIN` = **GPIO2**, **active LOW** (LOW = ON)
- Safe to drive GPIO2 as OUTPUT — no RF antenna interference (unlike ESP32-C3 SuperMini GPIO8)

**What did NOT work:** ESP32-C3 SuperMini
- ESP-NOW `esp_now_send` returned `ESP_NOW_SEND_FAIL` consistently on one role
- USB-JTAG re-enumerates to a new COM port during bootloader (confusing, not a separate board)
- GPIO8 (onboard LED) shares antenna circuitry on some PCB variants — driving it breaks RF receive

---

## PlatformIO Configuration

```ini
[env:mydevice]
platform   = espressif32
board      = wemos_d1_mini32
framework  = arduino
build_flags =
    -DCORE_DEBUG_LEVEL=0
    ; DO NOT add -DARDUINO_USB_MODE or -DARDUINO_USB_CDC_ON_BOOT
    ; Those are only for ESP32-C3/S2/S3 native USB. CH9102 doesn't need them.
upload_port  = COM21        ; adjust per board
monitor_port = COM21
monitor_speed   = 115200
monitor_filters = direct
monitor_rts = 0
monitor_dtr = 0
```

**Key difference from ESP32-C3:** No `-DARDUINO_USB_MODE=1` or `-DARDUINO_USB_CDC_ON_BOOT=1` flags.

---

## Flashing — Critical: Unicode Encoding Fix

esptool progress bar characters crash PowerShell pipes. **Always use this pattern:**

```powershell
$env:PYTHONIOENCODING = "utf-8"
pio run -e mydevice --target upload *> flash_out.txt
Get-Content flash_out.txt | Select-String "SUCCESS|FAILED|Hard reset|MAC:|Error"
```

**Never** do `pio run ... 2>&1 | Select-String ...` — the pipe encoding will crash esptool mid-flash and lock the COM port at 460800 baud.

**If COM port gets stuck at 460800 baud:** Unplug and replug the USB cable. Programmatic reset (`Disable-PnpDevice`) requires elevation and may not work.

**Flash one board at a time** — flashing both simultaneously risks one port getting stuck if a crash occurs.

---

## Working ESP-NOW Firmware Pattern

### Includes
```cpp
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
```

### Setup Sequence (order matters)
```cpp
WiFi.mode(WIFI_STA);
esp_wifi_set_ps(WIFI_PS_NONE);   // REQUIRED — disable power save for reliable RX
delay(100);

// Read own MAC, decide which peer to talk to
uint8_t myMac[6];
WiFi.macAddress(myMac);

esp_now_init();                               // init BEFORE setting channel
esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // set channel AFTER init

esp_now_register_send_cb(onDataSent);
esp_now_register_recv_cb(onDataRecv);

esp_now_peer_info_t peer = {};
memcpy(peer.peer_addr, peerMac, 6);
peer.channel = 1;      // must match esp_wifi_set_channel value
peer.encrypt = false;
esp_now_add_peer(&peer);
```

### Receive Callback — Arduino-ESP32 v3.x API

```cpp
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const uint8_t *srcAddr = info->src_addr;
#else
void onDataRecv(const uint8_t *srcAddr, const uint8_t *data, int len) {
#endif
    // handle packet
}
```

The v3.x signature **must** use `esp_now_recv_info_t *` — passing the old `(uint8_t*, uint8_t*, int)` signature compiles but silently never fires.

### Broadcast is Broken — Use Unicast
ESP-NOW broadcast (`FF:FF:FF:FF:FF:FF`) receive callback **never fires** in IDF 5.x / Arduino-ESP32 v3.x.
**Solution:** Hardcode peer MACs and use unicast. Use a two-pass flash workflow:
1. Flash with placeholder MACs (`0x00` × 6) → firmware prints own MAC and halts
2. Record both MACs from serial output
3. Update firmware with real MACs → reflash

---

## Simultaneous WizRemote + Inter-ESP ESP-NOW

Both work on the **same ESP-NOW channel** without any special configuration.

WizRemote sends 13-byte ESP-NOW packets. To receive them alongside your own protocol, check payload length in the receive callback:

```cpp
void onDataRecv(...) {
    if (len != sizeof(MyMessage)) {
        // Foreign packet (e.g. WizRemote)
        Serial.printf("[WIZ-RX] from=%s  len=%d  hex:", srcMac, len);
        for (int i = 0; i < len; i++) Serial.printf(" %02X", data[i]);
        Serial.println();
        return;
    }
    // Handle own protocol
    memcpy(&inMsg, data, sizeof(inMsg));
}
```

**WizRemote packet format (13 bytes, observed):**
```
Byte 0: 0x81 — button/event type
Byte 1: brightness? (0x0C observed)
Byte 2: scene/color temp? (0x0A observed)
Bytes 3–12: additional params (TBD — not yet decoded)
```
WizRemote MAC observed: `98:77:D5:98:33:8A` (varies per remote unit).
No peer registration needed — the callback fires for any incoming ESP-NOW packet regardless of sender.

---

## MAC Address Workflow for New Boards

1. Add placeholder MACs to firmware:
   ```cpp
   static const uint8_t MAC_A[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // TBD
   static const uint8_t MAC_B[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // TBD
   ```
2. In MAC detection block, **halt** on unknown MAC (prevents TX spam scrolling past the MAC printout):
   ```cpp
   } else {
       Serial.println("WARNING: unknown MAC — update firmware and reflash");
       while (true) { delay(1000); }
   }
   ```
3. Flash each board, open monitor, record `MY MAC: XX:XX:XX:XX:XX:XX`
4. Fill in real MACs, reflash both boards

---

## Validated Result

| Metric | Result |
|---|---|
| TX status | `OK` every message, both directions |
| RX | Every peer message received, correct MAC/counter |
| WizRemote coexistence | `[WIZ-RX]` packets received without disrupting peer comms |
| Stability | 200+ messages exchanged, zero failures |
| Channel | 1 (both boards) |
| TX interval | 2 seconds |

---

## Production Boards — Assigned for Stalled Project

These two D1 Mini32 boards were validated 2026-05-09 and are reserved for the production project.

| Role | PlatformIO env | COM port | MAC address |
|------|---------------|----------|-------------|
| **Controller** | `device5` | COM25 | `84:1F:E8:39:AC:1C` |
| **Player-2** | `device6` | COM26 | `84:1F:E8:39:D2:48` |

Both boards confirmed bidirectional ESP-NOW TX=OK at 2 s intervals (tested 30+ messages, zero failures).
Controller has `-DBLINK_ID=1` build flag — onboard LED blinks 10× at boot for physical identification.
