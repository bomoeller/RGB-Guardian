#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================
// HARDWARE CONFIGURATION - SELECT YOUR SETUP
// ============================================

// Uncomment ONE of these to select your LED strip configuration:
// #define LED_SETUP_WS2812B_30      // 30 LEDs, WS2812B strip
#define LED_SETUP_WS2815_288   // 288 LEDs, WS2815 strip

// Configuration for WS2812B with 30 LEDs
#ifdef LED_SETUP_WS2812B_30
  #define LED_PIN     8
  #define NUM_LEDS    30
  #define LED_TYPE    WS2812B
  #define COLOR_ORDER GRB
  #define BOSS_START_POS 29      // Last LED position
  // Speed settings optimized for 30 LEDs
  #define BOSS_INITIAL_SPEED 1500  // milliseconds per step (Level 1)
  #define BOSS_SPEED_DECREASE 150  // Speed increase per level (ms faster)
  #define BOSS_MIN_SPEED 250       // Fastest possible boss speed
  #define SHOT_SPEED 50            // milliseconds between shot movements
#endif

// Configuration for WS2815 with 288 LEDs
#ifdef LED_SETUP_WS2815_288
  #define LED_PIN     8
  #define NUM_LEDS    288
  #define LED_TYPE    WS2815
  #define COLOR_ORDER RGB        // R and G swapped compared to WS2812B
  #define BOSS_START_POS 287     // Last LED position
  // Speed settings optimized for 288 LEDs (5x faster than 30 LED setup)
  #define BOSS_INITIAL_SPEED 300   // milliseconds per step (Level 1)
  #define BOSS_SPEED_DECREASE 30   // Speed increase per level (ms faster)
  #define BOSS_MIN_SPEED 50        // Fastest possible boss speed
  #define SHOT_SPEED 10            // milliseconds between shot movements
#endif

// Compile-time check to ensure a configuration is selected
#if !defined(LED_SETUP_WS2812B_30) && !defined(LED_SETUP_WS2815_288)
  #error "No LED configuration selected! Uncomment either LED_SETUP_WS2812B_30 or LED_SETUP_WS2815_288"
#endif

#define BTN1_PIN    0        // Red shot button
#define BTN2_PIN    1        // Green shot button
#define BTN3_PIN    2        // Blue shot button
#define BTN4_PIN    3        // Unused
#define BTN5_PIN    4        // Unused
#define BTN6_PIN    9        // BOOT button (unused in game)
#define NUM_BUTTONS 6

// ============================================
// GAME CONFIGURATION (Tweak these!)
// ============================================
#define BRIGHTNESS  50       // LED brightness (0-255)
#define FRAMES_PER_SECOND 60 // Update rate

// Player settings
#define PLAYER_SIZE 3        // Number of white LEDs for player
#define PLAYER_START 0       // Player position (LEDs 0-2)

// Boss settings
#define BOSS_DEFEAT_POS 2    // If boss reaches this position = game over
#define MAX_BOSS_PARTS 20    // Maximum boss parts (grows each level)

// Shot settings
#define MAX_SHOTS 15         // Maximum simultaneous shots

// Animation durations
#define WIN_ANIMATION_DURATION 500    // ms - quick celebration
#define LOSE_ANIMATION_DURATION 4000  // ms

// ============================================
// GAME STATE DATA STRUCTURES
// ============================================
enum GameState {
  STATE_PLAYING,
  STATE_WIN_ANIMATION,
  STATE_LOSE_ANIMATION
};

struct Shot {
  int16_t position;  // -1 = inactive, supports up to 32767 LEDs
  uint8_t color;     // 0=Red, 1=Green, 2=Blue
  CRGB rgbColor;
};

struct BossPart {
  uint8_t color;     // 0=Red, 1=Green, 2=Blue
  bool active;       // true if this part is still alive
};

// ============================================
// GLOBAL VARIABLES
// ============================================
CRGB leds[NUM_LEDS];
const uint8_t buttonPins[NUM_BUTTONS] = {BTN1_PIN, BTN2_PIN, BTN3_PIN, BTN4_PIN, BTN5_PIN, BTN6_PIN};
bool lastButtonState[NUM_BUTTONS] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

GameState gameState = STATE_PLAYING;
uint8_t currentLevel = 1;
uint8_t playerLives = 3;       // Player has 3 lives
int16_t bossPosition = BOSS_START_POS;  // Changed from int8_t to support 288 LEDs
BossPart boss[MAX_BOSS_PARTS]; // Boss grows each level
Shot shots[MAX_SHOTS];
unsigned long lastBossMove = 0;
unsigned long lastShotMove = 0;
unsigned long animationStart = 0;
uint16_t currentBossSpeed = BOSS_INITIAL_SPEED;

const CRGB colorTable[4] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};

// ============================================
// GAME MODE SETTINGS
// ============================================
uint8_t numColors = 3;  // 3 or 4 color mode (default: 3)

// ============================================
// ESP-NOW REMOTE CONTROL VARIABLES
// ============================================
volatile uint8_t remoteCommand = 0xFF;  // 0xFF = no command
volatile uint8_t lastRemoteSequence = 0;
uint8_t ledBrightness = BRIGHTNESS;     // Current brightness (0-255)
const uint8_t BRIGHTNESS_STEP = 26;     // Brightness adjustment step (10% of 255)

// ============================================
// FUNCTION DECLARATIONS
// ============================================
void initGame();
void spawnBoss();
void respawnBossAfterLifeLoss();
void updateGame();
void updateBoss();
void updateShots();
void handleButtons();
void fireShot(uint8_t color);
void checkCollisions();
void renderGame();
void playWinAnimation();
void playLoseAnimation();
bool isBossDefeated();
void initWiFi();
void initESPNOW();
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
void processRemoteCommand();

// ============================================
// SETUP
// ============================================

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  
  // Initialize FastLED first for visual feedback
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  
  // Wait for serial monitor with LED countdown (3 seconds)
  for (int i = 3; i > 0; i--) {
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(500);
    leds[0] = CRGB::Black;
    FastLED.show();
    delay(500);
  }
  
  Serial.println("\n\n=================================");
  Serial.println("[INFO] RGB Guardian - ESP32-C3 SuperMini");
  Serial.println("=================================");
  
  // Display active LED configuration
  #ifdef LED_SETUP_WS2812B_30
    Serial.println("[INFO] Configuration: WS2812B (30 LEDs)");
  #endif
  #ifdef LED_SETUP_WS2815_288
    Serial.println("[INFO] Configuration: WS2815 (288 LEDs)");
  #endif
  Serial.printf("[INFO] LED Count: %d, Boss Start: %d\n", NUM_LEDS, BOSS_START_POS);
  
  // Initialize buttons
  Serial.println("[INFO] Initializing buttons...");
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  Serial.println("[OK] Buttons ready");
  
  // Initialize WiFi and ESP-NOW for remote control
  Serial.println("[INFO] Initializing ESP-NOW remote control...");
  initWiFi();
  initESPNOW();
  Serial.println("[OK] ESP-NOW ready");
  
  // Initialize game
  Serial.println("[INFO] Initializing game...");
  initGame();
  Serial.println("[OK] Game ready!");
  Serial.println("[INFO] Controls:");
  Serial.println("[INFO]   Physical: Button 1=Red, 2=Green, 3=Blue, 4=White (4-color mode)");
  Serial.println("[INFO]   Remote: Button 1=Red, 2=Green, 3=Blue, 4=White (4-color mode)");
  Serial.println("[INFO]   Remote: Sleep=Toggle 3/4 colors, On=Reset, Higher/Lower=Brightness");
  Serial.println("=================================\n");
  
  // Startup complete
  leds[0] = CRGB::Green;
  FastLED.show();
  delay(500);
  FastLED.clear();
  FastLED.show();
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  handleButtons();
  processRemoteCommand();  // Handle remote control inputs
  
  switch (gameState) {
    case STATE_PLAYING:
      updateGame();
      renderGame();
      break;
      
    case STATE_WIN_ANIMATION:
      playWinAnimation();
      break;
      
    case STATE_LOSE_ANIMATION:
      playLoseAnimation();
      break;
  }
  
  FastLED.show();
  delay(1000 / FRAMES_PER_SECOND);
}

// ============================================
// GAME INITIALIZATION
// ============================================
void initGame() {
  currentLevel = 1;
  playerLives = 3;  // Reset lives
  gameState = STATE_PLAYING;
  
  // Clear all shots
  for (int i = 0; i < MAX_SHOTS; i++) {
    shots[i].position = -1;
  }
  
  // Initialize boss
  spawnBoss();
  
  Serial.printf("[INFO] Level %d started! Lives: %d\n", currentLevel, playerLives);
}

void spawnBoss() {
  lastBossMove = millis();
  
  // Clear all shots from previous stage
  for (int i = 0; i < MAX_SHOTS; i++) {
    shots[i].position = -1;
  }
  
  // Calculate boss speed for current level (use signed math to prevent underflow)
  int32_t calculatedSpeed = BOSS_INITIAL_SPEED - ((currentLevel - 1) * BOSS_SPEED_DECREASE);
  if (calculatedSpeed < BOSS_MIN_SPEED) {
    currentBossSpeed = BOSS_MIN_SPEED;
  } else {
    currentBossSpeed = (uint16_t)calculatedSpeed;
  }
  
  // Determine number of parts based on level
  uint8_t numParts = currentLevel;
  if (numParts > MAX_BOSS_PARTS) numParts = MAX_BOSS_PARTS;
  
  // Position boss so all parts are visible from start
  // Last part should be at BOSS_START_POS, first part at BOSS_START_POS - (numParts - 1)
  bossPosition = BOSS_START_POS - (numParts - 1);
  
  // Generate random colors for boss parts
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (i < numParts) {
      boss[i].color = random(numColors);  // Random color from current palette
      boss[i].active = true;
    } else {
      boss[i].active = false;
    }
  }
  
  Serial.printf("[INFO] Boss spawned with %d part(s), speed: %dms, position: %d\n", numParts, currentBossSpeed, bossPosition);
  
  // Print boss colors
  Serial.print("[DEBUG] Boss colors (front to back): ");
  for (int i = 0; i < numParts; i++) {
    const char* colorName = boss[i].color == 0 ? "RED" : 
                            boss[i].color == 1 ? "GREEN" : 
                            boss[i].color == 2 ? "BLUE" : "WHITE";
    Serial.print(colorName);
    if (i < numParts - 1) Serial.print(", ");
  }
  Serial.println();
}

void respawnBossAfterLifeLoss() {
  // Clear all shots
  for (int i = 0; i < MAX_SHOTS; i++) {
    shots[i].position = -1;
  }
  
  // Reset boss speed to level default (use signed math to prevent underflow)
  int32_t calculatedSpeed = BOSS_INITIAL_SPEED - ((currentLevel - 1) * BOSS_SPEED_DECREASE);
  if (calculatedSpeed < BOSS_MIN_SPEED) {
    currentBossSpeed = BOSS_MIN_SPEED;
  } else {
    currentBossSpeed = (uint16_t)calculatedSpeed;
  }
  
  // Reset boss to far end - single part
  bossPosition = BOSS_START_POS;  // Far end of LED strip
  lastBossMove = millis();
  
  // Only 1 part with random color
  boss[0].color = random(numColors);
  boss[0].active = true;
  
  // Deactivate all other parts
  for (int i = 1; i < MAX_BOSS_PARTS; i++) {
    boss[i].active = false;
  }
  
  Serial.printf("[INFO] Boss respawned at position %d (1 part), speed reset to: %dms\n", bossPosition, currentBossSpeed);
  const char* colorName = boss[0].color == 0 ? "RED" : 
                          boss[0].color == 1 ? "GREEN" : 
                          boss[0].color == 2 ? "BLUE" : "WHITE";
  Serial.printf("[DEBUG] Boss color: %s\n", colorName);
}

// ============================================
// BUTTON HANDLING
// ============================================
void handleButtons() {
  // Check buttons based on color mode
  int buttonsToCheck = (numColors == 4) ? 4 : 3;
  
  for (int i = 0; i < buttonsToCheck; i++) {
    bool currentState = digitalRead(buttonPins[i]);
    
    // Detect button press (HIGH to LOW transition)
    if (lastButtonState[i] == HIGH && currentState == LOW && gameState == STATE_PLAYING) {
      // Direct mapping: Button 1=0(Red), 2=1(Green), 3=2(Blue), 4=3(Yellow)
      fireShot(i);
    }
    
    lastButtonState[i] = currentState;
  }
}

void fireShot(uint8_t color) {
  // Find empty shot slot
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (shots[i].position == -1) {
      shots[i].position = PLAYER_SIZE;  // Start just after player
      shots[i].color = color;
      shots[i].rgbColor = colorTable[color];
      const char* colorName = color == 0 ? "RED" : 
                              color == 1 ? "GREEN" : 
                              color == 2 ? "BLUE" : "WHITE";
      Serial.printf("[DEBUG] Shot %d fired: %s at position %d\n", i, colorName, shots[i].position);
      return;
    }
  }
  Serial.println("[WARNING] Max shots reached, shot not fired!");
}

// ============================================
// GAME UPDATE LOGIC
// ============================================
void updateGame() {
  updateBoss();
  checkCollisions();  // Check collisions after boss moves
  updateShots();
  checkCollisions();  // Check collisions after shots move
  
  // Check win condition
  if (isBossDefeated()) {
    Serial.println("[INFO] Boss defeated!");
    gameState = STATE_WIN_ANIMATION;
    animationStart = millis();
    return;
  }
  
  // Check lose condition - find first active boss part
  int firstActivePart = -1;
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) {
      firstActivePart = i;
      break;
    }
  }
  
  if (firstActivePart >= 0) {
    int frontPosition = bossPosition + firstActivePart;
    
    // Check if boss hits any remaining life LED (0, 1, or 2 based on lives)
    for (int lifePos = 0; lifePos < playerLives; lifePos++) {
      if (frontPosition == lifePos) {
        // Count active boss parts for debug output
        int activeParts = 0;
        for (int i = 0; i < MAX_BOSS_PARTS; i++) {
          if (boss[i].active) activeParts++;
        }
        
        // Boss hit a life LED
        Serial.println("===========================================");
        Serial.printf("[CRITICAL] PLAYER LOST A LIFE!\n");
        Serial.printf("[CRITICAL] Boss front at position %d hit Life LED at position %d\n", frontPosition, lifePos);
        Serial.printf("[CRITICAL] Boss had %d active parts, starting at position %d\n", activeParts, bossPosition);
        playerLives--;
        Serial.printf("[CRITICAL] Remaining lives: %d\n", playerLives);
        Serial.println("===========================================");
        
        if (playerLives == 0) {
          Serial.println("[INFO] All lives lost - Game Over!");
          gameState = STATE_LOSE_ANIMATION;
          animationStart = millis();
        } else {
          // Show red flash animation for life loss
          fill_solid(leds, NUM_LEDS, CRGB::Red);
          FastLED.show();
          delay(400);  // Brief red flash
          
          // Clear the strip
          fill_solid(leds, NUM_LEDS, CRGB::Black);
          FastLED.show();
          delay(100);
          
          // Respawn boss as 1-part from far end
          respawnBossAfterLifeLoss();
        }
        break;
      }
    }
  }
}

void updateBoss() {
  unsigned long now = millis();
  
  // Move boss toward player
  if (now - lastBossMove >= currentBossSpeed) {
    bossPosition--;
    lastBossMove = now;
  }
}

void updateShots() {
  unsigned long now = millis();
  
  if (now - lastShotMove >= SHOT_SPEED) {
    for (int i = 0; i < MAX_SHOTS; i++) {
      if (shots[i].position >= 0) {
        shots[i].position++;
        
        // Remove shot if it goes off screen
        if (shots[i].position >= NUM_LEDS) {
          Serial.printf("[DEBUG] Shot %d reached end of strip (pos %d), removed\n", i, shots[i].position);
          shots[i].position = -1;
        }
      }
    }
    lastShotMove = now;
  }
}

void checkCollisions() {
  // Find first active boss part (front of boss, closest to player)
  int frontPartIdx = -1;
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) {
      frontPartIdx = i;
      break;
    }
  }
  
  // No active boss parts - nothing to hit
  if (frontPartIdx == -1) return;
  
  int frontPosition = bossPosition + frontPartIdx;
  
  // Check if any shot hits the front part
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (shots[i].position == -1) continue;
    
    if (shots[i].position == frontPosition) {
      // Shot hit the front part
      const char* shotColor = shots[i].color == 0 ? "RED" : 
                              shots[i].color == 1 ? "GREEN" : 
                              shots[i].color == 2 ? "BLUE" : "WHITE";
      const char* bossColor = boss[frontPartIdx].color == 0 ? "RED" : 
                              boss[frontPartIdx].color == 1 ? "GREEN" : 
                              boss[frontPartIdx].color == 2 ? "BLUE" : "WHITE";
      
      Serial.printf("[DEBUG] Shot %d (%s) hit boss front at pos %d (boss color: %s)\n", 
                    i, shotColor, frontPosition, bossColor);
      
      if (shots[i].color == boss[frontPartIdx].color) {
        // Correct color - destroy front part
        boss[frontPartIdx].active = false;
        Serial.printf("[INFO] Hit! Boss front part destroyed (was part %d)\n", frontPartIdx + 1);
      } else {
        // Wrong color - boss speeds up by 20%
        currentBossSpeed = (currentBossSpeed * 80) / 100;
        if (currentBossSpeed < BOSS_MIN_SPEED) {
          currentBossSpeed = BOSS_MIN_SPEED;
        }
        Serial.printf("[WARNING] Wrong color! Boss speed increased to %dms\n", currentBossSpeed);
      }
      // Remove shot
      shots[i].position = -1;
      Serial.printf("[DEBUG] Shot %d removed after collision\n", i);
    }
  }
}

bool isBossDefeated() {
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) return false;
  }
  return true;
}

// ============================================
// RENDERING
// ============================================
void renderGame() {
  FastLED.clear();
  
  // Draw player lives (white LEDs at start - only show remaining lives)
  for (int i = 0; i < playerLives; i++) {
    leds[i] = CRGB::White;
  }
  
  // Draw boss
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) {
      int pos = bossPosition + i;
      if (pos >= 0 && pos < NUM_LEDS) {
        leds[pos] = colorTable[boss[i].color];
      }
    }
  }
  
  // Draw shots
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (shots[i].position >= 0 && shots[i].position < NUM_LEDS) {
      leds[shots[i].position] = shots[i].rgbColor;
    }
  }
}

// ============================================
// ANIMATIONS
// ============================================
void playWinAnimation() {
  unsigned long elapsed = millis() - animationStart;
  
  if (elapsed < WIN_ANIMATION_DURATION) {
    // Sparkle burst - random white/gold flashes (fireworks effect)
    FastLED.clear();
    
    // Create 10-15 random sparkles each frame
    int numSparkles = random(10, 16);
    for (int i = 0; i < numSparkles; i++) {
      int pos = random(NUM_LEDS);
      // Alternate between white and gold
      if (random(2) == 0) {
        leds[pos] = CRGB::White;
      } else {
        leds[pos] = CRGB(255, 215, 0);  // Gold
      }
    }
  } else {
    // Animation done - next level
    currentLevel++;
    spawnBoss();
    gameState = STATE_PLAYING;
  }
}

void playLoseAnimation() {
  unsigned long elapsed = millis() - animationStart;
  
  if (elapsed < LOSE_ANIMATION_DURATION) {
    // Explosion pattern - red flashing
    if ((elapsed / 200) % 2 == 0) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Red;
      }
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Orange;
      }
    }
  } else {
    // Animation done - restart game
    Serial.println("[INFO] Game Over - Restarting...\n");
    initGame();
  }
}

// ============================================
// ESP-NOW REMOTE CONTROL FUNCTIONS
// ============================================
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("[ESPNOW] WiFi initialized in Station mode");
  Serial.print("[ESPNOW] ESP32-C3 MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] ERROR: Failed to initialize ESP-NOW");
    return;
  }
  Serial.println("[ESPNOW] ESP-NOW initialized successfully");
  
  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("[ESPNOW] Receive callback registered");
  Serial.println("[ESPNOW] Waiting for remote commands...");
}

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  // This runs in interrupt context - keep it SHORT!
  
  // Log sender MAC address
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
  
  // Check packet length (WIZ-remote sends 13 bytes)
  if (len == 13) {
    uint8_t sequence = data[1];
    uint8_t buttonCode = data[6];
    
    // Debounce: only process if sequence changed
    if (sequence != lastRemoteSequence) {
      lastRemoteSequence = sequence;
      remoteCommand = buttonCode;
      
      Serial.printf("[ESPNOW] Remote: %s, Seq: %d, Button: 0x%02X\n", macStr, sequence, buttonCode);
    }
  } else {
    Serial.printf("[ESPNOW] Unknown packet from %s (length: %d)\n", macStr, len);
  }
}

void processRemoteCommand() {
  // Process pending remote command (runs in main loop, not interrupt)
  if (remoteCommand == 0xFF) return;  // No command pending
  
  uint8_t cmd = remoteCommand;
  remoteCommand = 0xFF;  // Clear command
  
  // Only process commands during gameplay
  if (gameState == STATE_PLAYING) {
    switch (cmd) {
      case 0x10:  // Button 1 - Fire RED shot
        Serial.println("[REMOTE] Button 1 - Fire RED");
        fireShot(0);
        break;
        
      case 0x11:  // Button 2 - Fire GREEN shot
        Serial.println("[REMOTE] Button 2 - Fire GREEN");
        fireShot(1);
        break;
        
      case 0x12:  // Button 3 - Fire BLUE (always)
        Serial.println("[REMOTE] Button 3 - Fire BLUE");
        fireShot(2);  // Blue
        break;
        
      case 0x13:  // Button 4 - Fire WHITE (only in 4-color mode)
        if (numColors == 4) {
          Serial.println("[REMOTE] Button 4 - Fire WHITE");
          fireShot(3);  // White
        } else {
          Serial.println("[REMOTE] Button 4 - Not available in 3-color mode");
        }
        break;
        
      case 0x03:  // Sleep - Toggle 3/4 color mode
        if (numColors == 3) {
          numColors = 4;
          Serial.println("[REMOTE] Sleep - Switched to 4-COLOR mode (Red/Green/Blue/White)");
        } else {
          numColors = 3;
          Serial.println("[REMOTE] Sleep - Switched to 3-COLOR mode (Red/Green/Blue)");
        }
        Serial.printf("[INFO] Color mode: %d colors\n", numColors);
        break;
        
      case 0x09:  // Higher - Increase brightness
        if (ledBrightness < 255 - BRIGHTNESS_STEP) {
          ledBrightness += BRIGHTNESS_STEP;
        } else {
          ledBrightness = 255;
        }
        FastLED.setBrightness(ledBrightness);
        Serial.printf("[REMOTE] Brightness UP: %d\n", ledBrightness);
        break;
        
      case 0x08:  // Lower - Decrease brightness
        if (ledBrightness > BRIGHTNESS_STEP) {
          ledBrightness -= BRIGHTNESS_STEP;
        } else {
          ledBrightness = BRIGHTNESS_STEP;
        }
        FastLED.setBrightness(ledBrightness);
        Serial.printf("[REMOTE] Brightness DOWN: %d\n", ledBrightness);
        break;
        
      case 0x01:  // On - Reset game
        Serial.println("[REMOTE] ON - Resetting game");
        initGame();
        break;
        
      default:
        Serial.printf("[REMOTE] Unmapped button: 0x%02X\n", cmd);
        break;
    }
  } else {
    // During animations, allow reset and color mode toggle
    if (cmd == 0x01) {  // On button
      Serial.println("[REMOTE] ON - Resetting game");
      initGame();
    } else if (cmd == 0x03) {  // Sleep - Toggle color mode
      if (numColors == 3) {
        numColors = 4;
        Serial.println("[REMOTE] Sleep - Switched to 4-COLOR mode (Red/Green/Blue/White)");
      } else {
        numColors = 3;
        Serial.println("[REMOTE] Sleep - Switched to 3-COLOR mode (Red/Green/Blue)");
      }
      Serial.printf("[INFO] Color mode: %d colors\n", numColors);
    }
  }
}
