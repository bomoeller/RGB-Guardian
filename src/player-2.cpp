#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

#include "player2_espnow_packet.h"

// ============================================
// RGB Guardian - Player-2
// ============================================
// Hardware: Wemos D1 Mini32 (ESP-WROOM-32)
// Buttons:     RGBW = GPIO16,17,21,22 (active-low, INPUT_PULLUP)
// Button LEDs: RGBW = GPIO23,19,18,26 (active-LOW OUTPUT)
// Purpose: Secondary controller sending button presses to the primary controller.
// Sends Player-2 ESP-NOW unicast packets (0x10-0x13) to the controller.
// Platform: espressif32 / Arduino-ESP32 v3.x (IDF5) — broadcast broken, unicast only.

#define BTN1_PIN          16   // RED button
#define BTN2_PIN          17   // GREEN button
#define BTN3_PIN          21   // BLUE button
#define BTN4_PIN          22   // WHITE button
#define BTN_LED_RED_PIN   23   // RED LED
#define BTN_LED_GREEN_PIN 19   // GREEN LED
#define BTN_LED_BLUE_PIN  18   // BLUE LED
#define BTN_LED_WHITE_PIN 26   // WHITE LED

static const uint8_t BUTTON_COUNT = 4;
static const uint8_t buttonPins[BUTTON_COUNT] = {BTN1_PIN, BTN2_PIN, BTN3_PIN, BTN4_PIN};
static const uint8_t buttonLedPins[BUTTON_COUNT] = {BTN_LED_RED_PIN, BTN_LED_GREEN_PIN, BTN_LED_BLUE_PIN, BTN_LED_WHITE_PIN};
static const uint8_t buttonCodes[BUTTON_COUNT] = {0x10, 0x11, 0x12, 0x13};
static const char* buttonNames[BUTTON_COUNT] = {"RED", "GREEN", "BLUE", "WHITE"};

static const unsigned long LOCAL_DEBOUNCE_MS = 35;
static const uint8_t ESPNOW_FIXED_CHANNEL = 1;
static const uint16_t STARTUP_SERIAL_GRACE_MS = 1500;
static const uint16_t STARTUP_SUMMARY_REPEAT_MS = 2000;
static const uint8_t STARTUP_SUMMARY_REPEAT_COUNT = 5;

// Controller MAC (COM25) — Wemos D1 Mini32
static const uint8_t CONTROLLER_MAC[6] = {0x84, 0x1F, 0xE8, 0x39, 0xAC, 0x1C};

Player2EspNowPacket packetBuffer = {};
uint8_t sequenceCounter = 0;
bool stableButtonState[BUTTON_COUNT] = {HIGH, HIGH, HIGH, HIGH};
bool rawButtonState[BUTTON_COUNT] = {HIGH, HIGH, HIGH, HIGH};
unsigned long rawStateChangedAt[BUTTON_COUNT] = {0, 0, 0, 0};
bool startupSummaryEnabled = false;
unsigned long nextStartupSummaryAt = 0;
uint8_t startupSummaryRepeatsRemaining = 0;
int espNowVersion = -1;
int espNowPeerCount = -1;

uint8_t getCurrentWiFiChannel();

static void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  const char* statusStr = (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAILED";
  if (mac_addr != nullptr) {
    Serial.printf("[TX] Send %s dest=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  statusStr,
                  mac_addr[0], mac_addr[1], mac_addr[2],
                  mac_addr[3], mac_addr[4], mac_addr[5]);
  } else {
    Serial.printf("[TX] Send %s\n", statusStr);
  }
}

void printStartupSummary() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("RGB Guardian Player-2 (D1 Mini32)");
  Serial.println("========================================");
  Serial.print("[TX] STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[TX] Current WiFi channel: %u\n", (unsigned)getCurrentWiFiChannel());
  if (espNowVersion >= 0) {
    Serial.printf("[TX] ESP-NOW version: %d, peers: %d\n", espNowVersion, espNowPeerCount);
  }
  Serial.printf("[TX] Controller MAC: %02X:%02X:%02X:%02X:%02X:%02X [unicast]\n",
                CONTROLLER_MAC[0], CONTROLLER_MAC[1], CONTROLLER_MAC[2],
                CONTROLLER_MAC[3], CONTROLLER_MAC[4], CONTROLLER_MAC[5]);
  Serial.println("[TX] Ready - press GPIO16/17/21/22 buttons to send ESP-NOW packets");
}

void scheduleStartupSummaryRepeats() {
  startupSummaryEnabled = true;
  startupSummaryRepeatsRemaining = STARTUP_SUMMARY_REPEAT_COUNT;
  nextStartupSummaryAt = millis() + STARTUP_SUMMARY_REPEAT_MS;
}

void serviceStartupSummaryRepeats() {
  if (!startupSummaryEnabled || startupSummaryRepeatsRemaining == 0) {
    return;
  }

  if (millis() < nextStartupSummaryAt) {
    return;
  }

  printStartupSummary();
  startupSummaryRepeatsRemaining--;
  nextStartupSummaryAt = millis() + STARTUP_SUMMARY_REPEAT_MS;

  if (startupSummaryRepeatsRemaining == 0) {
    startupSummaryEnabled = false;
  }
}

uint8_t getCurrentWiFiChannel() {
  uint8_t primaryChannel = 0;
  wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;

  if (esp_wifi_get_channel(&primaryChannel, &secondChannel) != ESP_OK) {
    return 0;
  }

  return primaryChannel;
}

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(100);

  Serial.println("[TX] WiFi initialized in STA mode");
  Serial.print("[TX] Local MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[TX] ESP-NOW channel: %u\n", (unsigned)ESPNOW_FIXED_CHANNEL);
}

// Forward declaration
static void onEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len);

bool initEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[TX] ERROR: Failed to initialize ESP-NOW");
    return false;
  }

  // Set channel AFTER esp_now_init
  esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[TX] ESP-NOW initialized on channel %u\n", (unsigned)getCurrentWiFiChannel());

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  // Add controller as unicast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, CONTROLLER_MAC, 6);
  peerInfo.channel = ESPNOW_FIXED_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[TX] ERROR: Failed to add controller peer");
    return false;
  }

  Serial.printf("[TX] Controller peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
                CONTROLLER_MAC[0], CONTROLLER_MAC[1], CONTROLLER_MAC[2],
                CONTROLLER_MAC[3], CONTROLLER_MAC[4], CONTROLLER_MAC[5]);

  uint32_t ver = 0;
  esp_now_get_version(&ver);
  espNowVersion = (int)ver;

  esp_now_peer_num_t peerNum = {};
  esp_now_get_peer_num(&peerNum);
  espNowPeerCount = peerNum.total_num;

  return true;
}

static void onEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
  Serial.printf("[RX] From %02X:%02X:%02X:%02X:%02X:%02X len=%d\n",
                recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
                len);
}

void preparePacket(uint8_t buttonCode) {
  packetBuffer.magic0 = PLAYER2_PACKET_MAGIC_0;
  packetBuffer.magic1 = PLAYER2_PACKET_MAGIC_1;
  packetBuffer.version = PLAYER2_PACKET_VERSION;
  packetBuffer.sequence = sequenceCounter;
  packetBuffer.buttonCode = buttonCode;
}

void sendButtonPress(uint8_t buttonIndex) {
  uint8_t buttonCode = buttonCodes[buttonIndex];
  preparePacket(buttonCode);

  Serial.printf("[TX] Button %s -> code 0x%02X, seq %u\n",
                buttonNames[buttonIndex], buttonCode, sequenceCounter);

  esp_err_t result = esp_now_send(CONTROLLER_MAC,
                                  reinterpret_cast<const uint8_t*>(&packetBuffer),
                                  sizeof(packetBuffer));
  if (result != ESP_OK) {
    Serial.printf("[TX] ERROR: esp_now_send returned %d\n", (int)result);
  }

  sequenceCounter++;
}

void setupButtons() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    bool initialState = digitalRead(buttonPins[i]);
    stableButtonState[i] = initialState;
    rawButtonState[i] = initialState;
    rawStateChangedAt[i] = millis();
  }

  Serial.println("[TX] Buttons initialized on GPIO16,17,21,22");
}

void setupButtonLeds() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttonLedPins[i], OUTPUT);
    digitalWrite(buttonLedPins[i], HIGH);
  }

  Serial.println("[TX] Button LEDs initialized on GPIO23,19,18,26");
}

void updateButtonLeds() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
    digitalWrite(buttonLedPins[i], buttonPressed ? LOW : HIGH);
  }
}

void pollButtons() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    bool currentRawState = digitalRead(buttonPins[i]);

    if (currentRawState != rawButtonState[i]) {
      rawButtonState[i] = currentRawState;
      rawStateChangedAt[i] = now;
    }

    if (stableButtonState[i] != rawButtonState[i] &&
        (now - rawStateChangedAt[i] >= LOCAL_DEBOUNCE_MS)) {
      stableButtonState[i] = rawButtonState[i];

      // Send one press only when a debounced transition reaches active-low pressed.
      if (stableButtonState[i] == LOW) {
        sendButtonPress(i);
      }
    }
  }

  updateButtonLeds();
}

void setup() {
  Serial.begin(115200);
  delay(STARTUP_SERIAL_GRACE_MS);

  setupButtons();
  setupButtonLeds();
  initWiFi();

  if (!initEspNow()) {
    Serial.println("[TX] Startup failed - check ESP-NOW configuration");
    return;
  }

  printStartupSummary();
  scheduleStartupSummaryRepeats();
}

void loop() {
  serviceStartupSummaryRepeats();
  pollButtons();
  delay(1);
}