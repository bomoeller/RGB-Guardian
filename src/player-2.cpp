#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

#include "player2_espnow_packet.h"

// ============================================
// RGB Guardian - Player-2
// ============================================
// Hardware: ESP32-C3 with 4 active-low buttons on GPIO0-3.
// Purpose: Secondary controller sending button presses to the primary controller.
// Sends WIZ-compatible color button packets (0x10-0x13).
// Note: Controller-side MAC allowlisting for this module is added later.

#define BTN1_PIN 0
#define BTN2_PIN 1
#define BTN3_PIN 2
#define BTN4_PIN 3
#define BTN_LED_RED_PIN 5
#define BTN_LED_GREEN_PIN 6
#define BTN_LED_BLUE_PIN 7
#define BTN_LED_WHITE_PIN 8

static const uint8_t BUTTON_COUNT = 4;
static const uint8_t buttonPins[BUTTON_COUNT] = {BTN1_PIN, BTN2_PIN, BTN3_PIN, BTN4_PIN};
static const uint8_t buttonLedPins[BUTTON_COUNT] = {BTN_LED_RED_PIN, BTN_LED_GREEN_PIN, BTN_LED_BLUE_PIN, BTN_LED_WHITE_PIN};
static const uint8_t buttonCodes[BUTTON_COUNT] = {0x10, 0x11, 0x12, 0x13};
static const char* buttonNames[BUTTON_COUNT] = {"RED", "GREEN", "BLUE", "WHITE"};

static const unsigned long LOCAL_DEBOUNCE_MS = 25;
static const uint8_t SEND_BURST_COUNT = 3;
static const uint16_t SEND_BURST_DELAY_MS = 6;
static const uint8_t ESPNOW_FIXED_CHANNEL = 1;
static const uint16_t STARTUP_SERIAL_GRACE_MS = 1500;
static const uint16_t STARTUP_SUMMARY_REPEAT_MS = 2000;
static const uint8_t STARTUP_SUMMARY_REPEAT_COUNT = 5;

Player2EspNowPacket packetBuffer = {};
uint8_t sequenceCounter = 0;
bool lastButtonState[BUTTON_COUNT] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceAt[BUTTON_COUNT] = {0, 0, 0, 0};
bool startupSummaryEnabled = false;
unsigned long nextStartupSummaryAt = 0;
uint8_t startupSummaryRepeatsRemaining = 0;
int espNowVersion = -1;
int espNowPeerCount = -1;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Diagnostic: controller MAC for unicast ACK test - confirms Player-2 radio TX works
static const uint8_t CONTROLLER_MAC[6] = {0x88, 0x56, 0xA6, 0x74, 0x51, 0xB4};
static bool unicastPeerAdded = false;

uint8_t getCurrentWiFiChannel();

static void onEspNowSent(const esp_now_send_info_t* tx_info, esp_now_send_status_t status) {
  const char* statusStr = (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAILED";
  if (tx_info != nullptr && tx_info->des_addr != nullptr) {
    Serial.printf("[TX] Send %s ch=%u dest=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  statusStr, (unsigned)getCurrentWiFiChannel(),
                  tx_info->des_addr[0], tx_info->des_addr[1], tx_info->des_addr[2],
                  tx_info->des_addr[3], tx_info->des_addr[4], tx_info->des_addr[5]);
  } else {
    Serial.printf("[TX] Send %s ch=%u\n", statusStr, (unsigned)getCurrentWiFiChannel());
  }
}

void printStartupSummary() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("RGB Guardian Player-2");
  Serial.println("========================================");
  Serial.print("[TX] STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[TX] AP  MAC: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.printf("[TX] Current WiFi channel: %u\n", (unsigned)getCurrentWiFiChannel());
  if (espNowVersion >= 0) {
    Serial.printf("[TX] ESP-NOW version: %d, peers: %d\n", espNowVersion, espNowPeerCount);
  }
  Serial.println("[TX] Destination MAC: FF:FF:FF:FF:FF:FF [broadcast]");
  Serial.println("[TX] Ready - press GPIO0-3 buttons to send simple ESP-NOW color shots");
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

bool waitForStationStart(const char* context) {
  unsigned long start = millis();
  while (!WiFi.STA.started() && (millis() - start) < 2000) {
    delay(10);
  }

  if (!WiFi.STA.started()) {
    Serial.printf("[TX] WARNING: STA interface did not report started during %s\n", context);
    return false;
  }

  Serial.printf("[TX] STA interface started during %s\n", context);
  return true;
}

uint8_t getCurrentWiFiChannel() {
  uint8_t primaryChannel = 0;
  wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;

  if (esp_wifi_get_channel(&primaryChannel, &secondChannel) != ESP_OK) {
    return 0;
  }

  return primaryChannel;
}

bool lockWiFiChannel(const char* context) {
  if (esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.printf("[TX] WARNING: Failed to set WiFi channel to %u during %s\n",
                  (unsigned)ESPNOW_FIXED_CHANNEL, context);
    return false;
  }

  uint8_t currentChannel = getCurrentWiFiChannel();
  if (currentChannel != ESPNOW_FIXED_CHANNEL) {
    Serial.printf("[TX] WARNING: WiFi channel is %u during %s, expected %u\n",
                  (unsigned)currentChannel, context, (unsigned)ESPNOW_FIXED_CHANNEL);
    return false;
  }

  Serial.printf("[TX] WiFi channel locked to %u during %s\n",
                (unsigned)currentChannel, context);
  return true;
}

bool setWiFiChannelForEspNow(const char* context) {
  WiFi.setChannel(ESPNOW_FIXED_CHANNEL);

  uint8_t currentChannel = getCurrentWiFiChannel();
  if (currentChannel != ESPNOW_FIXED_CHANNEL) {
    Serial.printf("[TX] WARNING: WiFi.setChannel left channel at %u during %s, expected %u\n",
                  (unsigned)currentChannel, context, (unsigned)ESPNOW_FIXED_CHANNEL);
    return false;
  }

  Serial.printf("[TX] WiFi.setChannel locked channel to %u during %s\n",
                (unsigned)currentChannel, context);
  return true;
}

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void initWiFi() {
  // WIFI_AP_STA is required on ESP32-C3 SuperMini: pure STA disconnected mode
  // uses a broken RF calibration path where ESP-NOW TX is invisible to other boards.
  // AP_STA activates the AP-side RF path which has correct TX power.
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  // Start a minimal hidden soft-AP on the fixed channel to engage AP RF calibration
  WiFi.softAP("_p2", nullptr, ESPNOW_FIXED_CHANNEL, 1 /*hidden*/, 1 /*max_conn*/);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  waitForStationStart("WiFi init");
  lockWiFiChannel("WiFi init");

  // Set maximum TX power (84 = 21 dBm in 0.25 dBm units)
  int8_t powerBefore = 0, powerAfter = 0;
  esp_wifi_get_max_tx_power(&powerBefore);
  esp_wifi_set_max_tx_power(84);
  esp_wifi_get_max_tx_power(&powerAfter);

  delay(100);
  Serial.println("[TX] WiFi initialized in AP_STA mode (ESP32-C3 TX fix)");
  Serial.print("[TX] Local MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[TX] Fixed ESP-NOW channel: %u\n", (unsigned)ESPNOW_FIXED_CHANNEL);
  Serial.printf("[TX] TX power: %d -> %d (x0.25 dBm)\n", (int)powerBefore, (int)powerAfter);
}

// Forward declaration needed because initEspNow() references this before definition
static void onEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len);

bool initEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[TX] ERROR: Failed to initialize ESP-NOW");
    return false;
  }
  Serial.println("[TX] ESP-NOW initialized");
  lockWiFiChannel("ESP-NOW init");

  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
  peerInfo.channel = 0;  // Must be 0 for broadcast peer (use current WiFi channel)
  peerInfo.ifidx = WIFI_IF_AP;  // AP interface: properly calibrated in AP_STA mode
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[TX] ERROR: Failed to add broadcast peer");
    return false;
  }

  // Add controller as unicast peer - ACK on this send proves Player-2 radio TX works
  esp_now_peer_info_t unicastPeerInfo = {};
  memcpy(unicastPeerInfo.peer_addr, CONTROLLER_MAC, 6);
  unicastPeerInfo.channel = 0;
  unicastPeerInfo.ifidx = WIFI_IF_AP;  // AP interface: properly calibrated in AP_STA mode
  unicastPeerInfo.encrypt = false;
  if (esp_now_add_peer(&unicastPeerInfo) == ESP_OK) {
    unicastPeerAdded = true;
    Serial.printf("[TX] Unicast diagnostic peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  CONTROLLER_MAC[0], CONTROLLER_MAC[1], CONTROLLER_MAC[2],
                  CONTROLLER_MAC[3], CONTROLLER_MAC[4], CONTROLLER_MAC[5]);
  } else {
    Serial.println("[TX] WARNING: Failed to add unicast diagnostic peer");
  }

  uint32_t ver = 0;
  esp_now_get_version(&ver);
  espNowVersion = (int)ver;

  esp_now_peer_num_t peerNum = {};
  esp_now_get_peer_num(&peerNum);
  espNowPeerCount = peerNum.total_num;

  Serial.printf("[TX] ESP-NOW broadcast peer registered (version %d, peers %d)\n",
                espNowVersion, espNowPeerCount);

  esp_now_register_recv_cb(onEspNowRecv);
  Serial.println("[TX] Receive callback registered - waiting for controller pings");
  return true;
}

static void onEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
  // Log every received frame - tells us if controller→Player-2 direction works
  Serial.printf("[RX] Received from %02X:%02X:%02X:%02X:%02X:%02X len=%d ch=%u",
                recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
                len, (unsigned)getCurrentWiFiChannel());
  if (len >= 4 && data[0] == 'P' && data[1] == 'I' && data[2] == 'N' && data[3] == 'G') {
    Serial.print(" [PING from controller]");
  }
  Serial.println();
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

  Serial.printf("[TX] Button %s -> simple code 0x%02X, seq %u\n",
                buttonNames[buttonIndex], buttonCode, sequenceCounter);

  for (uint8_t attempt = 0; attempt < SEND_BURST_COUNT; attempt++) {
    esp_err_t result = esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&packetBuffer), sizeof(packetBuffer));
    if (result != ESP_OK) {
      Serial.printf("[TX] ERROR: esp_now_send returned %d on attempt %u\n",
                    (int)result, (unsigned)(attempt + 1));
      break;
    }
    delay(SEND_BURST_DELAY_MS);
  }

  // Unicast diagnostic: SUCCESS here = ACK received = frame physically reached controller radio
  if (unicastPeerAdded) {
    esp_err_t result = esp_now_send(CONTROLLER_MAC,
                                    reinterpret_cast<const uint8_t*>(&packetBuffer),
                                    sizeof(packetBuffer));
    if (result != ESP_OK) {
      Serial.printf("[TX] Unicast esp_now_send err=%d\n", (int)result);
    }
    // onEspNowSent callback will print SUCCESS (ACK received) or FAILED (no ACK = not received)
  }

  sequenceCounter++;
}

void setupButtons() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    lastButtonState[i] = digitalRead(buttonPins[i]);
    lastDebounceAt[i] = 0;
  }

  Serial.println("[TX] Buttons initialized on GPIO0-3");
}

void setupButtonLeds() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttonLedPins[i], OUTPUT);
    digitalWrite(buttonLedPins[i], HIGH);
  }

  Serial.println("[TX] Button LEDs initialized on GPIO5-8");
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
    bool currentState = digitalRead(buttonPins[i]);
    bool pressedEdge = (lastButtonState[i] == HIGH && currentState == LOW);

    if (pressedEdge && (now - lastDebounceAt[i] >= LOCAL_DEBOUNCE_MS)) {
      lastDebounceAt[i] = now;
      sendButtonPress(i);
    }

    lastButtonState[i] = currentState;
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