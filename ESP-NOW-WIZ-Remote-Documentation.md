# ESP-NOW with WIZ-Remote Documentation

**Date:** January 25, 2026  
**Project:** LED Shooting ESP32 System  
**Purpose:** Reference guide for implementing ESP-NOW receiver with WIZ-remote control

---

## Hardware Information

### ESP32 Receiver (LED Controller)
- **Device:** ESP32 Wroom32 MiniKit v2.0
- **MAC Address:** A0:A3:B3:26:7D:F4

### WIZ-Remote Sender
- **MAC Address:** 98:77:D5:83:A4:77

---

## Protocol Specification

### Message Structure
- **Packet Length:** 13 bytes (fixed)
- **Protocol:** ESP-NOW (connectionless WiFi communication)
- **Frequency:** 2.4 GHz WiFi channels

### Byte Layout
```
Byte[0]  : Message Type (0x81 = normal, 0x91 = On button)
Byte[1]  : Sequence Counter (increments with each transmission)
Byte[2-5]: Unknown/Reserved (always 0x00 0x00 0x00 0x20)
Byte[6]  : BUTTON IDENTIFIER (key field for command detection)
Byte[7-8]: Unknown (0x01 0x64)
Byte[9-12]: CRC/Checksum (varies per message)
```

---

## Button Mapping

### Complete Button Map

| Button Name | Byte[6] HEX | Byte[6] DEC | Use Case Example |
|-------------|-------------|-------------|------------------|
| **On**      | 0x01        | 1           | Enable system / Start effects |
| **Off**     | 0x02        | 2           | Disable system / Stop effects |
| **Sleep**   | 0x03        | 3           | Low power mode / Dim LEDs |
| **Lower**   | 0x08        | 8           | Decrease brightness / sensitivity |
| **Higher**  | 0x09        | 9           | Increase brightness / sensitivity |
| **Button 1** | 0x10       | 16          | Custom effect 1 |
| **Button 2** | 0x11       | 17          | Custom effect 2 |
| **Button 3** | 0x12       | 18          | Custom effect 3 |
| **Button 4** | 0x13       | 19          | Custom effect 4 |

### Additional Notes
- Buttons send **multiple identical packets** when pressed (8-10 packets per press)
- Use debouncing logic to prevent multiple triggers
- Byte[1] sequence counter helps identify unique button presses

---

## Implementation Guide

### Required Libraries
```cpp
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
```

### WiFi Initialization
```cpp
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.println("[ESPNOW] WiFi initialized in Station mode");
  Serial.print("[ESPNOW] MAC Address: ");
  Serial.println(WiFi.macAddress());
}
```

### ESP-NOW Receive Callback
```cpp
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len == 13) {
    uint8_t buttonCode = data[6];  // Extract button identifier
    
    // Add debouncing logic here if needed
    
    switch(buttonCode) {
      case 0x01: // On button
        Serial.println("[CMD] ON");
        break;
      case 0x02: // Off button
        Serial.println("[CMD] OFF");
        break;
      case 0x03: // Sleep button
        Serial.println("[CMD] SLEEP");
        break;
      case 0x08: // Lower button
        Serial.println("[CMD] LOWER");
        break;
      case 0x09: // Higher button
        Serial.println("[CMD] HIGHER");
        break;
      case 0x10: // Button 1
        Serial.println("[CMD] BUTTON 1");
        break;
      case 0x11: // Button 2
        Serial.println("[CMD] BUTTON 2");
        break;
      case 0x12: // Button 3
        Serial.println("[CMD] BUTTON 3");
        break;
      case 0x13: // Button 4
        Serial.println("[CMD] BUTTON 4");
        break;
    }
  }
}
```

### Setup Sequence
```cpp
void setup() {
  Serial.begin(115200);
  
  // 1. Initialize WiFi
  initWiFi();
  
  // 2. Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] Error initializing ESP-NOW");
    return;
  }
  Serial.println("[ESPNOW] ESP-NOW initialized successfully");
  
  // 3. Register receive callback
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("[ESPNOW] Receive callback registered");
  
  // Continue with other setup...
}
```

---

## Debouncing Strategy

Since WIZ-remote sends 8-10 packets per button press, implement one of these debouncing methods:

### Method 1: Sequence Counter Tracking
```cpp
volatile uint8_t lastSequence = 0;

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len == 13) {
    uint8_t currentSequence = data[1];
    
    // Only process if sequence changed
    if (currentSequence != lastSequence) {
      lastSequence = currentSequence;
      uint8_t buttonCode = data[6];
      // Process button command...
    }
  }
}
```

### Method 2: Time-Based Debouncing
```cpp
unsigned long lastButtonTime = 0;
const unsigned long DEBOUNCE_MS = 200;

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  unsigned long now = millis();
  
  if (now - lastButtonTime > DEBOUNCE_MS) {
    lastButtonTime = now;
    uint8_t buttonCode = data[6];
    // Process button command...
  }
}
```

---

## Testing Protocol

1. Upload firmware with Serial Monitor enabled
2. Press each button individually
3. Verify byte[6] values match this documentation
4. Confirm multiple packets per press (8-10 typical)
5. Test debouncing effectiveness

---

## Sample Serial Output

```
[INFO] LED Shooting program starting on ESP32 Wroom32 MiniKit v2.0
[ESPNOW] WiFi initialized in Station mode
[ESPNOW] MAC Address: A0:A3:B3:26:7D:F4
[ESPNOW] ESP-NOW initialized successfully
[ESPNOW] Receive callback registered
[ESPNOW] Ready to receive data from WIZ-remote

========== ESP-NOW Data Received ==========
[ESPNOW] From MAC: 98:77:D5:83:A4:77
[ESPNOW] Data length: 13
[ESPNOW] Data (HEX): 81 09 00 00 00 20 10 01 64 2B EC 2D 00
[ESPNOW] Data (DEC): 129 9 0 0 0 32 16 1 100 43 236 45 0
==========================================
[CMD] BUTTON 1
```

---

## Troubleshooting

### No Data Received
- Verify WiFi is in STA mode (not AP mode)
- Check MAC addresses match
- Ensure WIZ-remote is powered and in range
- Confirm ESP-NOW initialized successfully

### Multiple Triggers Per Press
- Implement debouncing (see strategies above)
- Check sequence counter is incrementing
- Add minimum time delay between commands

### MAC Address Shows 00:00:00:00:00:00
- WiFi.macAddress() called before WiFi.mode(WIFI_STA)
- Add delay after WiFi initialization
- Use esp_wifi_get_mac() as alternative

---

## Future Enhancements

- Add peer registration for bidirectional communication
- Implement acknowledgment system
- Create command queue for reliable execution
- Add RSSI monitoring for range detection
- Support multiple WIZ-remotes with different MAC addresses

---

## References

- **ESP-NOW Documentation:** [Espressif ESP-NOW Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- **Max Payload:** 250 bytes (no encryption), 1470 bytes (encrypted)
- **Max Paired Devices:** 20 (10 encrypted + 10 unencrypted)
- **Range:** ~220m (line of sight), ~30-50m (indoor typical)

---

**Document Version:** 1.0  
**Last Updated:** January 25, 2026  
**Author:** LED Shooting Project Team
