#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

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
static const uint8_t ESPNOW_PACKET_LEN = 13;

// Default to broadcast for bring-up. Replace with receiver MAC later if desired.
static const uint8_t DESTINATION_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t packetBuffer[ESPNOW_PACKET_LEN] = {0};
uint8_t sequenceCounter = 0;
bool lastButtonState[BUTTON_COUNT] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceAt[BUTTON_COUNT] = {0, 0, 0, 0};

bool isBroadcastDestination() {
  for (uint8_t i = 0; i < 6; i++) {
    if (DESTINATION_MAC[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool addEspNowPeer() {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, DESTINATION_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    Serial.printf("[TX] ERROR: esp_now_add_peer failed (%d)\n", (int)result);
    return false;
  }

  Serial.print("[TX] ESP-NOW peer added: ");
  printMac(DESTINATION_MAC);
  if (isBroadcastDestination()) {
    Serial.print(" [broadcast]");
  }
  Serial.println();
  return true;
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);
  delay(100);

  Serial.println("[TX] WiFi initialized in Station mode");
  Serial.print("[TX] Local MAC: ");
  Serial.println(WiFi.macAddress());
}

bool initEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[TX] ERROR: Failed to initialize ESP-NOW");
    return false;
  }

  Serial.println("[TX] ESP-NOW initialized successfully");
  return addEspNowPeer();
}

void preparePacket(uint8_t buttonCode) {
  // Match the receiver-visible WIZ packet layout.
  packetBuffer[0] = 0x81;
  packetBuffer[1] = sequenceCounter;
  packetBuffer[2] = 0x00;
  packetBuffer[3] = 0x00;
  packetBuffer[4] = 0x00;
  packetBuffer[5] = 0x20;
  packetBuffer[6] = buttonCode;
  packetBuffer[7] = 0x01;
  packetBuffer[8] = 0x64;
  packetBuffer[9] = 0x00;
  packetBuffer[10] = 0x00;
  packetBuffer[11] = 0x00;
  packetBuffer[12] = 0x00;
}

void sendButtonPress(uint8_t buttonIndex) {
  uint8_t buttonCode = buttonCodes[buttonIndex];
  preparePacket(buttonCode);

  Serial.printf("[TX] Button %s -> code 0x%02X, seq %u\n",
                buttonNames[buttonIndex], buttonCode, sequenceCounter);

  for (uint8_t attempt = 0; attempt < SEND_BURST_COUNT; attempt++) {
    esp_err_t result = esp_now_send(DESTINATION_MAC, packetBuffer, ESPNOW_PACKET_LEN);
    if (result != ESP_OK) {
      Serial.printf("[TX] ERROR: esp_now_send failed on attempt %u (%d)\n",
                    (unsigned)(attempt + 1), (int)result);
      break;
    }
    delay(SEND_BURST_DELAY_MS);
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
  delay(300);

  Serial.println();
  Serial.println("========================================");
  Serial.println("RGB Guardian Player-2");
  Serial.println("========================================");

  setupButtons();
  setupButtonLeds();
  initWiFi();

  if (!initEspNow()) {
    Serial.println("[TX] Startup failed - check ESP-NOW configuration");
    return;
  }

  Serial.print("[TX] Destination MAC: ");
  printMac(DESTINATION_MAC);
  if (isBroadcastDestination()) {
    Serial.print(" [broadcast]");
  }
  Serial.println();
  Serial.println("[TX] Ready - press GPIO0-3 buttons to send color shots");
}

void loop() {
  pollButtons();
  delay(1);
}