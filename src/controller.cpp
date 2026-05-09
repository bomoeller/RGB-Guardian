#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "player2_espnow_packet.h"

// RGB Guardian - Controller
// LED controller and Player-1 interface firmware.

// ============================================
// HARDWARE CONFIGURATION - SELECT YOUR SETUP
// ============================================

// Uncomment ONE of these to select your LED strip configuration:
// #define LED_SETUP_WS2812B_30      // 30 LEDs, WS2812B strip
#define LED_SETUP_WS2815_288   // WS2815 profile: 300 max LEDs, default active length 288

// Configuration for WS2812B with 30 LEDs
#ifdef LED_SETUP_WS2812B_30
  #define LED_PIN     16   // D1 Mini32: GPIO16 (safe general-purpose, RMT-capable)
  #define NUM_LEDS    30
  #define DEFAULT_ACTIVE_LED_COUNT 30
  #define LED_TYPE    WS2812B
  #define COLOR_ORDER GRB
  #define BOSS_START_POS 29      // Last LED position
  // Speed settings optimized for 30 LEDs
  #define BOSS_INITIAL_SPEED 1500  // milliseconds per step (Level 1)
  #define BOSS_SPEED_DECREASE 150  // Speed increase per level (ms faster)
  #define BOSS_MIN_SPEED 250       // Fastest possible boss speed
  #define SHOT_SPEED 50            // milliseconds between shot movements
#endif

// Configuration for WS2815 with 300 max LEDs (default active length 288)
#ifdef LED_SETUP_WS2815_288
  #define LED_PIN     16   // D1 Mini32: GPIO16 (safe general-purpose, RMT-capable)
  #define NUM_LEDS    300
  #define DEFAULT_ACTIVE_LED_COUNT 288
  #define LED_TYPE    WS2815
  #define COLOR_ORDER RGB        // R and G swapped compared to WS2812B
  #define BOSS_START_POS 299     // Last LED position
  // Speed settings optimized for long WS2815 strips (default active length 288)
  #define BOSS_INITIAL_SPEED 300   // milliseconds per step (Level 1)
  #define BOSS_SPEED_DECREASE 30   // Speed increase per level (ms faster)
  #define BOSS_MIN_SPEED 50        // Fastest possible boss speed
  #define SHOT_SPEED 10            // milliseconds between shot movements
#endif

// Compile-time check to ensure a configuration is selected
#if !defined(LED_SETUP_WS2812B_30) && !defined(LED_SETUP_WS2815_288)
  #error "No LED configuration selected! Uncomment either LED_SETUP_WS2812B_30 or LED_SETUP_WS2815_288"
#endif

// D1 Mini32 pin assignments
// Buttons:     4-in-a-row on left header  D0,D5,D6,D7 = GPIO26,18,19,23
// Button LEDs: 4-in-a-row on bottom-right IO27,IO25,IO32,IO12
#define BTN1_PIN    26       // D0 — Red shot button
#define BTN2_PIN    18       // D5 — Green shot button
#define BTN3_PIN    19       // D6 — Blue shot button
#define BTN4_PIN    23       // D7 — White shot button (4-color mode)
#define BTN6_PIN     0       // IO0 — BOOT button (mode cycle)
#define NUM_BUTTONS 5

// Button LED pins — 4-in-a-row on bottom-right header (OUTPUT, active-LOW)
#define BTN_LED_RED_PIN    27  // IO27
#define BTN_LED_GREEN_PIN  25  // IO25
#define BTN_LED_BLUE_PIN   32  // IO32
#define BTN_LED_WHITE_PIN  12  // IO12 (output-safe: no ext. pull-up, WROOM flash already strapped)

// Future: Piezo speaker
#define PIEZO_PIN          17  // D3 — reserved, not yet used

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
const uint8_t buttonPins[NUM_BUTTONS] = {BTN1_PIN, BTN2_PIN, BTN3_PIN, BTN4_PIN, BTN6_PIN};
bool lastButtonState[NUM_BUTTONS] = {HIGH, HIGH, HIGH, HIGH, HIGH};

GameState gameState = STATE_PLAYING;
uint8_t currentLevel = 1;
uint8_t playerLives = 3;       // Player has 3 lives
int16_t bossPosition = BOSS_START_POS;  // Changed from int8_t to support longer strips
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

// Button LED control modes
enum ButtonLEDMode {
  LED_MODE_INVERTED = 0,    // LEDs on when not pressed, off when pressed
  LED_MODE_FOLLOW_ME,       // Light up next button to press (based on boss color)
  LED_MODE_MEMORY,          // Memory sequence mode
  LED_MODE_GHOST_BOSS,      // Ghost boss gameplay mode
  LED_MODE_DUEL,            // 2-player duel mode
  LED_MODE_COOP,            // 2-player cooperative boss mode
  LED_MODE_ALL_VS_ALL,      // 2-player all-vs-all (players vs each other and boss)
  LED_MODE_PONG_DUEL,       // 2-player pong duel on a single LED strip
  LED_MODE_COUNT            // Total number of modes
};

ButtonLEDMode buttonLEDMode = LED_MODE_INVERTED;  // Default mode

// Wired settings mode (Option C: double-chord safety)
bool settingsModeActive = false;
bool settingsWaitRelease = false;
unsigned long settingsChordStart = 0;
const uint16_t SETTINGS_ENTER_HOLD_MS = 1000;
const uint16_t SETTINGS_INACTIVITY_TIMEOUT_MS = 10000;
const uint16_t SETTINGS_RED_LONG_PRESS_MS = 3000;
ButtonLEDMode settingsSelectedMode = LED_MODE_INVERTED;
uint8_t settingsSelectedNumColors = 3;
unsigned long settingsLastInteraction = 0;
unsigned long settingsRedHoldStart = 0;
unsigned long lastAnyShotFiredAt = 0;
bool settingsLengthAdjustActive = false;
uint16_t activeLedCount = DEFAULT_ACTIVE_LED_COUNT;
uint16_t settingsSelectedLedCount = DEFAULT_ACTIVE_LED_COUNT;
unsigned long settingsWhiteHoldStart = 0;
unsigned long settingsGreenHoldStart = 0;
unsigned long settingsBlueHoldStart = 0;
unsigned long settingsGreenRepeatAt = 0;
unsigned long settingsBlueRepeatAt = 0;
const uint16_t SETTINGS_WHITE_LONG_PRESS_MS = 900;
const uint16_t SETTINGS_LENGTH_REPEAT_START_MS = 450;
const uint16_t SETTINGS_LENGTH_REPEAT_MS = 90;
const uint8_t SETTINGS_LENGTH_REPEAT_STEP = 5;
const uint16_t SETTINGS_LED_COUNT_MIN = (NUM_LEDS >= 60) ? 60 : NUM_LEDS;

// Idle pause screen
bool idlePauseActive = false;
bool idlePauseTargetActive = false;
unsigned long lastUserActivityAt = 0;
const uint32_t IDLE_PAUSE_TIMEOUT_MS = 20000;
const uint16_t IDLE_PAUSE_FADE_IN_MS = 1400;
const uint16_t IDLE_PAUSE_FADE_OUT_MS = 500;
uint8_t idlePauseBlend = 0;
unsigned long idlePauseLastBlendUpdate = 0;
CRGB idlePauseSnapshot[NUM_LEDS];
CRGB idlePauseFrame[NUM_LEDS];

// Mode change indicator on strip end
bool modeDotsActive = false;
unsigned long modeDotsStartTime = 0;
const uint16_t MODE_DOTS_DURATION_MS = 2000;
const CRGB MODE_DOT_COLOR = CRGB(200, 162, 255);  // Lilac

// Memory mode playback state
bool memoryPlaybackActive = false;
bool memoryPlaybackLedOn = false;
uint8_t memoryPlaybackStep = 0;
uint8_t memorySequenceLength = 0;
uint8_t memorySequenceColors[MAX_BOSS_PARTS] = {0};
unsigned long memoryPlaybackTimer = 0;
const uint16_t MEMORY_LED_ON_MS = 350;
const uint16_t MEMORY_LED_OFF_MS = 180;

// ============================================
// ESP-NOW REMOTE CONTROL VARIABLES
// ============================================
uint8_t ledBrightness = BRIGHTNESS;     // Current brightness (0-255)
const uint8_t BRIGHTNESS_STEP = 26;     // Brightness adjustment step (10% of 255)
const uint8_t ESPNOW_FIXED_CHANNEL = 1;
const uint8_t ALLOWED_REMOTE_SLOT_COUNT = 2;
const uint8_t REMOTE_COMMAND_QUEUE_SIZE = 8;

// Allowed ESP-NOW sender MAC addresses.
// Slot 1 is the current remote. Leave empty slots as 00:00:00:00:00:00 until discovered.
const uint8_t ALLOWED_REMOTE_MACS[ALLOWED_REMOTE_SLOT_COUNT][6] = {
  {0x98, 0x77, 0xD5, 0x98, 0x33, 0x8A},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
volatile uint8_t remoteCommandQueue[REMOTE_COMMAND_QUEUE_SIZE] = {0};
volatile uint8_t remoteCommandQueueHead = 0;
volatile uint8_t remoteCommandQueueTail = 0;
volatile uint8_t remoteCommandQueueCount = 0;
volatile bool remoteCommandQueueOverflow = false;
volatile uint8_t lastRemoteSequenceBySlot[ALLOWED_REMOTE_SLOT_COUNT] = {0, 0};
volatile bool lastRemoteSequenceValidBySlot[ALLOWED_REMOTE_SLOT_COUNT] = {false, false};
uint8_t lastPlayer2SenderMac[6] = {0, 0, 0, 0, 0, 0};
bool lastPlayer2SenderMacValid = false;
uint8_t lastPlayer2Sequence = 0;
bool lastPlayer2SequenceValid = false;
volatile bool unknownRemoteReportPending = false;
volatile int unknownRemotePacketLength = 0;
volatile uint8_t unknownRemoteMac[6] = {0, 0, 0, 0, 0, 0};
uint8_t lastReportedUnknownRemoteMac[6] = {0, 0, 0, 0, 0, 0};
unsigned long lastUnknownRemoteReportAt = 0;
const uint16_t UNKNOWN_REMOTE_REPORT_SUPPRESS_MS = 1500;

// Ghost Boss mode settings
bool ghostBossModeEnabled = false;
bool ghostBossVisible = true;
unsigned long ghostBossModeStartTime = 0;
unsigned long ghostBossRevealUntil = 0;
unsigned long ghostBossLastRevealTrigger = 0;
bool ghostBossFadingOut = false;
unsigned long ghostBossFadeStart = 0;
const uint16_t GHOST_BOSS_INITIAL_VISIBLE_MS = 5000;
const uint16_t GHOST_BOSS_REVEAL_MS = 1000;
const uint16_t GHOST_BOSS_FALLBACK_REVEAL_INTERVAL_MS = 10000;
const uint16_t GHOST_BOSS_FADE_OUT_MS = 1200;
const uint8_t GHOST_BOSS_NEAR_PLAYER_VISIBLE_DISTANCE = 20;

// 2-player duel mode state
struct DuelShot {
  int16_t position;  // -1 = inactive
  uint8_t color;     // 0=Red, 1=Green, 2=Blue, 3=White
};

DuelShot player1Shots[MAX_SHOTS];
DuelShot player2Shots[MAX_SHOTS];
bool duelGameOver = false;
uint8_t duelWinner = 0;  // 0=none, 1=player1, 2=player2
unsigned long duelGameOverStart = 0;
const uint16_t DUEL_END_ANIMATION_MS = 2500;
const uint16_t DUEL_RUN_PERIOD_MS = 220;

// 2-player cooperative mode state
struct CoopBossPart {
  int16_t position;
  uint8_t color;
  bool active;
};

const uint8_t MAX_COOP_BOSS_PARTS = 60;
CoopBossPart coopBoss[MAX_COOP_BOSS_PARTS];
DuelShot coopPlayer1Shots[MAX_SHOTS];
DuelShot coopPlayer2Shots[MAX_SHOTS];
uint8_t coopRound = 1;
uint8_t coopBossPartsThisRound = 2;
uint8_t coopSpacingThisRound = 0;
int16_t coopBossLeftEdge = 0;
int16_t coopBossRightEdge = 0;
unsigned long coopLastBossMove = 0;
uint16_t coopBossSpeed = 650;
const uint16_t COOP_BOSS_INITIAL_SPEED = 650;
const uint16_t COOP_BOSS_SPEED_DECREASE = 35;
const uint16_t COOP_BOSS_MIN_SPEED = 140;
bool coopRoundOver = false;
bool coopRoundWon = false;
unsigned long coopRoundOverStart = 0;
const uint16_t COOP_ROUND_END_MS = 2300;
bool coopLossAnimationPlayed = false;

// 2-player all-vs-all mode state (players vs expanding boss)
DuelShot allVsAllPlayer1Shots[MAX_SHOTS];
DuelShot allVsAllPlayer2Shots[MAX_SHOTS];
int16_t allVsAllPrevP1Shots[MAX_SHOTS];
int16_t allVsAllPrevP2Shots[MAX_SHOTS];
bool allVsAllRoundOver = false;
bool allVsAllPlayersWin = false;
unsigned long allVsAllRoundOverStart = 0;
bool allVsAllLossAnimationPlayed = false;
uint8_t allVsAllWinner = 0;  // 0=none, 1=player1, 2=player2

// 2-player pong duel mode state
enum PongPhase {
  PONG_PHASE_SERVE_COUNTDOWN,
  PONG_PHASE_BALL_MOVING,
  PONG_PHASE_POINT_FLASH,
  PONG_PHASE_MATCH_OVER
};

PongPhase pongPhase = PONG_PHASE_SERVE_COUNTDOWN;
int16_t pongBallPosition = 0;
int8_t pongBallDirection = 1;
int8_t pongNextServeDirection = 1;
uint8_t pongPlayer1Score = 0;
uint8_t pongPlayer2Score = 0;
uint8_t pongPointWinner = 0;
uint8_t pongPointLoser = 0;
uint8_t pongMatchWinner = 0;
uint8_t pongHitFlashPlayer = 0;
unsigned long pongPhaseStart = 0;
unsigned long pongLastBallMove = 0;
unsigned long pongHitFlashUntil = 0;
uint16_t pongBallDelay = 0;
const uint8_t PONG_WIN_SCORE = 5;
const uint16_t PONG_SERVE_COUNTDOWN_MS = 3000;
const uint16_t PONG_HIT_FLASH_MS = 80;
const uint16_t PONG_POINT_FLASH_MS = 600;
const uint16_t PONG_MATCH_OVER_MS = 2200;
const CRGB PONG_PLAYER1_COLOR = CRGB(180, 20, 20);
const CRGB PONG_PLAYER2_COLOR = CRGB(30, 80, 210);
const CRGB PONG_BALL_COLOR = CRGB::White;
const CRGB PONG_SERVE_COLOR = CRGB(255, 200, 40);
const CRGB PONG_HIT_FLASH_COLOR = CRGB(255, 120, 0);
const CRGB PONG_MISS_FLASH_COLOR = CRGB(255, 0, 0);

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
void playLifeLostAnimation();
bool isBossDefeated();
void initWiFi();
void initESPNOW();
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
void processRemoteCommand();
bool enqueueRemoteCommand(uint8_t command);
bool dequeueRemoteCommand(uint8_t *command);
void flushRemoteCommandQueueOverflowReport();
bool handlePlayer2EspNowPacket(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
bool isMacConfigured(const uint8_t *mac);
int8_t findAllowedRemoteIndex(const uint8_t *mac);
void queueUnknownRemoteReport(const uint8_t *mac, int len);
void flushUnknownRemoteReport();
void printMacToSerial(const uint8_t *mac);
void updateButtonLEDs();
int8_t getNextColorToShoot();
void startMemoryPlaybackSequence();
void cycleButtonLEDMode();
void updateModeDotsIndicator();
void renderModeDotsIndicator();
const char* getButtonLEDModeName(ButtonLEDMode mode);
void updateGhostBossVisibility();
void notifyGhostBossHit();
void initDuelMode();
void fireDuelShotPlayer1(uint8_t color);
void fireDuelShotPlayer2(uint8_t color);
void updateDuelGame();
void renderDuelGame();
void renderDuelEndAnimation();
void initCoopMode();
void spawnCoopBossRound();
void distributeCoopBossPositions();
int16_t findVisibleCoopBossIndexAtPosition(int16_t position);
void fireCoopShotPlayer1(uint8_t color);
void fireCoopShotPlayer2(uint8_t color);
void updateCoopGame();
void renderCoopGame();
void resolveCoopShotCollisions();
void initAllVsAllMode();
void spawnAllVsAllRound();
void updateAllVsAllGame();
void renderAllVsAllGame();
void resolveAllVsAllShotVsBossCollisions();
void resolveAllVsAllShotVsShotCollisions();
void fireAllVsAllShotPlayer1(uint8_t color);
void fireAllVsAllShotPlayer2(uint8_t color);
void initPongDuelMode();
void handlePongHitPlayer1();
void handlePongHitPlayer2();
void updatePongDuelGame();
void renderPongDuelGame();
void startPongServeCountdown();
void scorePongPoint(uint8_t winner, const char* reason);
uint8_t getPongBaseZoneSize();
uint8_t getPongMinZoneSize();
uint8_t getPongZoneShrinkPerPoint();
uint8_t getPongZoneSizeForPlayer(uint8_t player);
int16_t getPongPlayer1ZoneEnd();
int16_t getPongPlayer2ZoneStart();
int16_t getPongBallStepSize();
uint16_t getPongInitialBallDelay();
uint16_t getPongMinimumBallDelay();
uint16_t getPongSpeedupPerReturn();
uint16_t getPongEarlyHitMaxBonus();
bool isPongHitWindowForPlayer(uint8_t player);
uint16_t getPongEarlyHitBonus(uint8_t player);
void renderSharedWinSparkle(unsigned long elapsed);
void startModeSelectionIndicator();
void applySettingsSelectionAndExit();
void renderSettingsOverlay();
void restartCurrentGameMode();
void updateIdlePauseState();
void updateIdlePauseBlend();
void renderIdlePauseScreen(CRGB* out);
void adjustSettingsLedCount(int16_t delta);
void blackOutInactiveLeds();

// ============================================
// SETUP
// ============================================

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  lastAnyShotFiredAt = millis();
  lastUserActivityAt = millis();
  idlePauseLastBlendUpdate = millis();
  
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
  Serial.println("[INFO] RGB Guardian - Wemos D1 Mini32");
  Serial.println("=================================");
  
  // Display active LED configuration
  #ifdef LED_SETUP_WS2812B_30
    Serial.println("[INFO] Configuration: WS2812B (30 LEDs)");
  #endif
  #ifdef LED_SETUP_WS2815_288
    Serial.println("[INFO] Configuration: WS2815 (300 max LEDs)");
  #endif
  Serial.printf("[INFO] LED Max initialized: %d, Boss Start Max: %d\n", NUM_LEDS, BOSS_START_POS);
  Serial.printf("[INFO] LED Active length (default): %d\n", activeLedCount);
  
  // Initialize buttons
  Serial.println("[INFO] Initializing buttons...");
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  Serial.println("[OK] Buttons ready");
  
  // Initialize button LEDs
  Serial.println("[INFO] Initializing button LEDs...");
  pinMode(BTN_LED_RED_PIN, OUTPUT);
  pinMode(BTN_LED_GREEN_PIN, OUTPUT);
  pinMode(BTN_LED_BLUE_PIN, OUTPUT);
  pinMode(BTN_LED_WHITE_PIN, OUTPUT);
  digitalWrite(BTN_LED_RED_PIN, LOW);
  digitalWrite(BTN_LED_GREEN_PIN, LOW);
  digitalWrite(BTN_LED_BLUE_PIN, LOW);
  digitalWrite(BTN_LED_WHITE_PIN, LOW);
  Serial.println("[OK] Button LEDs ready");
  
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
  Serial.println("[INFO]   Remote: Off=Cycle Game Mode");
  Serial.println("[INFO]   Wired settings: hold Red+White for 1s (only if no shots for 1s)");
  Serial.println("[INFO]   Settings keys: Green=Mode+, Blue=Mode-, White=Toggle 4th color, Red=Save/Exit");
  Serial.println("[INFO] Game Modes:");
  Serial.println("[INFO]   1. INVERTED - LEDs on when not pressed");
  Serial.println("[INFO]   2. FOLLOW-ME - Next button to press lights up");
  Serial.println("[INFO]   3. MEMORY - Sequence mode");
  Serial.println("[INFO]   4. GHOST BOSS - Boss flickers visible/invisible");
  Serial.println("[INFO]   5. DUEL - 2-player shots from both ends");
  Serial.println("[INFO]   6. CO-PLAY - 2-player cooperative expanding boss");
  Serial.println("[INFO]   7. ALL-VS-ALL - 2 players vs expanding boss");
  Serial.println("[INFO]   8. PONG DUEL - 2-player rally with timing zones");
  Serial.printf("[INFO] Current LED Mode: INVERTED (Mode 1)\n");
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
  flushUnknownRemoteReport();
  flushRemoteCommandQueueOverflowReport();
  processRemoteCommand();  // Handle remote control inputs
  updateModeDotsIndicator();
  updateIdlePauseState();
  updateIdlePauseBlend();
  if (!idlePauseActive) {
    updateGhostBossVisibility();
  }
  updateButtonLEDs();      // Update button LED states based on mode
  
  switch (gameState) {
    case STATE_PLAYING:
      if (idlePauseBlend > 0) {
        renderIdlePauseScreen(idlePauseFrame);

        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = blend(idlePauseSnapshot[i], idlePauseFrame[i], idlePauseBlend);
        }
      } else if (buttonLEDMode == LED_MODE_DUEL) {
        if (!modeDotsActive && !settingsModeActive) {
          updateDuelGame();
        }
        renderDuelGame();
      } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
        if (!modeDotsActive && !settingsModeActive) {
          updateAllVsAllGame();
        }
        renderAllVsAllGame();
      } else if (buttonLEDMode == LED_MODE_PONG_DUEL) {
        if (!modeDotsActive && !settingsModeActive) {
          updatePongDuelGame();
        }
        renderPongDuelGame();
      } else if (buttonLEDMode == LED_MODE_COOP) {
        if (!modeDotsActive && !settingsModeActive) {
          updateCoopGame();
        }
        renderCoopGame();
      } else {
        if (!modeDotsActive && !settingsModeActive) {
          updateGame();
        }
        renderGame();
      }
      break;
      
    case STATE_WIN_ANIMATION:
      playWinAnimation();
      break;
      
    case STATE_LOSE_ANIMATION:
      playLoseAnimation();
      break;
  }

  renderModeDotsIndicator();
  renderSettingsOverlay();
  blackOutInactiveLeds();
  
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
  idlePauseActive = false;
  idlePauseTargetActive = false;
  idlePauseBlend = 0;
  lastUserActivityAt = millis();
  
  // Clear all shots
  for (int i = 0; i < MAX_SHOTS; i++) {
    shots[i].position = -1;
  }
  
  // Initialize boss
  spawnBoss();
      Serial.println("[INFO] Showing startup mode dots for 2s...");
      modeDotsActive = true;
      modeDotsStartTime = millis();
  
  Serial.printf("[INFO] Level %d started! Lives: %d, Active LEDs: %d (Max %d)\n", currentLevel, playerLives, activeLedCount, NUM_LEDS);
}

void spawnBoss() {
  lastBossMove = millis();

  // Reset ghost visibility timers on each new boss spawn.
  ghostBossVisible = true;
  ghostBossModeStartTime = millis();
  ghostBossRevealUntil = 0;
  ghostBossLastRevealTrigger = ghostBossModeStartTime;
  ghostBossFadingOut = false;
  ghostBossFadeStart = 0;
  
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
  bossPosition = ((int16_t)activeLedCount - 1) - (numParts - 1);
  
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

  // In memory mode, play the sequence when a new boss is initialized.
  startMemoryPlaybackSequence();
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
  bossPosition = (int16_t)activeLedCount - 1;  // Far end of active LED range
  lastBossMove = millis();

  // Reset ghost visibility timers on respawn.
  ghostBossVisible = true;
  ghostBossModeStartTime = millis();
  ghostBossRevealUntil = 0;
  ghostBossLastRevealTrigger = ghostBossModeStartTime;
  ghostBossFadingOut = false;
  ghostBossFadeStart = 0;
  
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

  // In memory mode, replay sequence after respawn.
  startMemoryPlaybackSequence();
}

// ============================================
// BUTTON HANDLING
// ============================================
void handleButtons() {
  bool redPressed = (digitalRead(BTN1_PIN) == LOW);
  bool greenPressed = (digitalRead(BTN2_PIN) == LOW);
  bool bluePressed = (digitalRead(BTN3_PIN) == LOW);
  bool whitePressed = (digitalRead(BTN4_PIN) == LOW);

  if (redPressed || greenPressed || bluePressed || whitePressed) {
    lastUserActivityAt = millis();
    idlePauseTargetActive = false;
    idlePauseActive = false;
  }

  // Option C: enter settings only when Red+White held for 1s and no shots fired for 1s.
  if (!settingsModeActive && gameState == STATE_PLAYING) {
    if (redPressed && whitePressed) {
      if (settingsChordStart == 0) {
        settingsChordStart = millis();
      } else if ((millis() - settingsChordStart >= SETTINGS_ENTER_HOLD_MS) &&
                 (millis() - lastAnyShotFiredAt >= SETTINGS_ENTER_HOLD_MS)) {
        settingsModeActive = true;
        settingsWaitRelease = true;
        settingsSelectedMode = buttonLEDMode;
        settingsSelectedNumColors = numColors;
        settingsSelectedLedCount = activeLedCount;
        settingsLengthAdjustActive = false;
        settingsLastInteraction = millis();
        settingsRedHoldStart = 0;
        settingsWhiteHoldStart = 0;
        settingsGreenHoldStart = 0;
        settingsBlueHoldStart = 0;
        settingsGreenRepeatAt = 0;
        settingsBlueRepeatAt = 0;
        settingsChordStart = 0;
        Serial.println("[SETTINGS] Entered wired settings mode");
        Serial.printf("[SETTINGS] Mode %d, Colors %d, LEDs %d\n", (uint8_t)settingsSelectedMode + 1, settingsSelectedNumColors, settingsSelectedLedCount);
      }
    } else {
      settingsChordStart = 0;
    }
  }

  if (settingsModeActive) {
    bool currentState[4] = {redPressed, greenPressed, bluePressed, whitePressed};

    if (settingsWaitRelease) {
      if (!redPressed && !greenPressed && !bluePressed && !whitePressed) {
        settingsWaitRelease = false;
      }
      for (int i = 0; i < 4; i++) {
        lastButtonState[i] = currentState[i] ? LOW : HIGH;
      }
      return;
    }

    // Auto-exit after inactivity timeout (save current settings selection).
    if (millis() - settingsLastInteraction >= SETTINGS_INACTIVITY_TIMEOUT_MS) {
      Serial.println("[SETTINGS] Auto-exit after inactivity");
      applySettingsSelectionAndExit();
      for (int i = 0; i < 4; i++) {
        lastButtonState[i] = currentState[i] ? LOW : HIGH;
      }
      return;
    }

    // Red long-press: restart current active mode without applying pending settings selection.
    if (currentState[0]) {
      if (settingsRedHoldStart == 0) {
        settingsRedHoldStart = millis();
        settingsLastInteraction = millis();
      } else if (millis() - settingsRedHoldStart >= SETTINGS_RED_LONG_PRESS_MS) {
        Serial.println("[SETTINGS] Red long-press -> restart current game mode");
        settingsModeActive = false;
        settingsWaitRelease = true;
        settingsRedHoldStart = 0;
        restartCurrentGameMode();
        for (int i = 0; i < 4; i++) {
          lastButtonState[i] = currentState[i] ? LOW : HIGH;
        }
        return;
      }
    } else {
      // Red short press: save and exit settings.
      if (settingsRedHoldStart != 0) {
        unsigned long redHeld = millis() - settingsRedHoldStart;
        settingsRedHoldStart = 0;
        if (redHeld < SETTINGS_RED_LONG_PRESS_MS) {
          settingsLastInteraction = millis();
          applySettingsSelectionAndExit();
          for (int i = 0; i < 4; i++) {
            lastButtonState[i] = currentState[i] ? LOW : HIGH;
          }
          return;
        }
      }
    }

    // Green: mode up
    if (settingsLengthAdjustActive) {
      // White press exits LED length adjust sub-mode.
      if (lastButtonState[3] == HIGH && currentState[3]) {
        settingsLengthAdjustActive = false;
        settingsWhiteHoldStart = 0;
        settingsGreenHoldStart = 0;
        settingsBlueHoldStart = 0;
        settingsGreenRepeatAt = 0;
        settingsBlueRepeatAt = 0;
        settingsLastInteraction = millis();
        Serial.println("[SETTINGS] LED length adjust OFF");
      }

      // Green increase (press + long-press repeat in larger steps).
      if (lastButtonState[1] == HIGH && currentState[1]) {
        adjustSettingsLedCount(1);
        settingsGreenHoldStart = millis();
        settingsGreenRepeatAt = settingsGreenHoldStart + SETTINGS_LENGTH_REPEAT_START_MS;
      }
      if (currentState[1] && settingsGreenHoldStart != 0 && millis() >= settingsGreenRepeatAt) {
        adjustSettingsLedCount(SETTINGS_LENGTH_REPEAT_STEP);
        settingsGreenRepeatAt = millis() + SETTINGS_LENGTH_REPEAT_MS;
      }
      if (!currentState[1]) {
        settingsGreenHoldStart = 0;
        settingsGreenRepeatAt = 0;
      }

      // Blue decrease (press + long-press repeat in larger steps).
      if (lastButtonState[2] == HIGH && currentState[2]) {
        adjustSettingsLedCount(-1);
        settingsBlueHoldStart = millis();
        settingsBlueRepeatAt = settingsBlueHoldStart + SETTINGS_LENGTH_REPEAT_START_MS;
      }
      if (currentState[2] && settingsBlueHoldStart != 0 && millis() >= settingsBlueRepeatAt) {
        adjustSettingsLedCount(-SETTINGS_LENGTH_REPEAT_STEP);
        settingsBlueRepeatAt = millis() + SETTINGS_LENGTH_REPEAT_MS;
      }
      if (!currentState[2]) {
        settingsBlueHoldStart = 0;
        settingsBlueRepeatAt = 0;
      }
    } else {
      // Green: mode up
      if (lastButtonState[1] == HIGH && currentState[1]) {
        settingsSelectedMode = (ButtonLEDMode)((settingsSelectedMode + 1) % LED_MODE_COUNT);
        settingsLastInteraction = millis();
        Serial.printf("[SETTINGS] Mode -> %d (%s)\n", (uint8_t)settingsSelectedMode + 1, getButtonLEDModeName(settingsSelectedMode));
      }

      // Blue: mode down
      if (lastButtonState[2] == HIGH && currentState[2]) {
        settingsSelectedMode = (ButtonLEDMode)((settingsSelectedMode + LED_MODE_COUNT - 1) % LED_MODE_COUNT);
        settingsLastInteraction = millis();
        Serial.printf("[SETTINGS] Mode -> %d (%s)\n", (uint8_t)settingsSelectedMode + 1, getButtonLEDModeName(settingsSelectedMode));
      }

      // White short press: toggle 3/4 colors. White long press: enter LED length adjust.
      if (currentState[3]) {
        if (settingsWhiteHoldStart == 0) {
          settingsWhiteHoldStart = millis();
        } else if (millis() - settingsWhiteHoldStart >= SETTINGS_WHITE_LONG_PRESS_MS) {
          settingsLengthAdjustActive = true;
          settingsWhiteHoldStart = 0;
          settingsLastInteraction = millis();
          Serial.printf("[SETTINGS] LED length adjust ON (Green/Blue change, current %d)\n", settingsSelectedLedCount);
        }
      } else {
        if (settingsWhiteHoldStart != 0) {
          unsigned long held = millis() - settingsWhiteHoldStart;
          settingsWhiteHoldStart = 0;
          if (held < SETTINGS_WHITE_LONG_PRESS_MS) {
            settingsSelectedNumColors = (settingsSelectedNumColors == 3) ? 4 : 3;
            settingsLastInteraction = millis();
            Serial.printf("[SETTINGS] Colors -> %d\n", settingsSelectedNumColors);
          }
        }
      }
    }

    for (int i = 0; i < 4; i++) {
      lastButtonState[i] = currentState[i] ? LOW : HIGH;
    }
    return;
  }

  // Check buttons based on color mode
  int buttonsToCheck = (numColors == 4) ? 4 : 3;
  
  for (int i = 0; i < buttonsToCheck; i++) {
    bool currentState = digitalRead(buttonPins[i]);
    
    // Detect button press (HIGH to LOW transition)
    if (lastButtonState[i] == HIGH && currentState == LOW && gameState == STATE_PLAYING) {
      // Block input while mode indicator is being shown.
      if (modeDotsActive) {
        lastButtonState[i] = currentState;
        continue;
      }

      // In memory mode, only allow presses after playback has finished.
      if (buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive) {
        lastButtonState[i] = currentState;
        continue;
      }

      // Direct mapping: Button 1=0(Red), 2=1(Green), 3=2(Blue), 4=3(White)
      if (buttonLEDMode == LED_MODE_PONG_DUEL) {
        if (i == 0) {
          handlePongHitPlayer1();
        } else if (i == 3) {
          handlePongHitPlayer2();
        }
      } else if (buttonLEDMode == LED_MODE_DUEL) {
        fireDuelShotPlayer1(i);
      } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
        fireAllVsAllShotPlayer1(i);
      } else if (buttonLEDMode == LED_MODE_COOP) {
        fireCoopShotPlayer1(i);
      } else {
        fireShot(i);
      }
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
      lastAnyShotFiredAt = millis();
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
          // Play sad 3-step life-loss animation (da-da-daaaa)
          playLifeLostAnimation();
          
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
        if (shots[i].position >= activeLedCount) {
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
      notifyGhostBossHit();

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
  
  // Draw boss (hidden during Ghost Boss hidden phase)
  if (!ghostBossModeEnabled || ghostBossVisible) {
    uint8_t fadeScale = 255;
    bool blackoutNow = false;

    if (ghostBossModeEnabled && ghostBossFadingOut) {
      uint16_t fadeElapsed = (uint16_t)(millis() - ghostBossFadeStart);
      if (fadeElapsed > GHOST_BOSS_FADE_OUT_MS) {
        fadeElapsed = GHOST_BOSS_FADE_OUT_MS;
      }

      fadeScale = (uint8_t)(((uint32_t)(GHOST_BOSS_FADE_OUT_MS - fadeElapsed) * 255) / GHOST_BOSS_FADE_OUT_MS);

      // Brief blackout flickers during fade-out.
      if ((fadeElapsed > 220 && fadeElapsed < 280) ||
          (fadeElapsed > 560 && fadeElapsed < 630) ||
          (fadeElapsed > 900 && fadeElapsed < 980)) {
        blackoutNow = true;
      }
    }

    for (int i = 0; i < MAX_BOSS_PARTS; i++) {
      if (boss[i].active) {
        int pos = bossPosition + i;
        if (pos >= 0 && pos < activeLedCount) {
          if (!blackoutNow) {
            CRGB bossColor = colorTable[boss[i].color];
            if (ghostBossModeEnabled && ghostBossFadingOut) {
              bossColor.nscale8_video(fadeScale);
            }
            leds[pos] = bossColor;
          }
        }
      }
    }
  }
  
  // Draw shots
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (shots[i].position >= 0 && shots[i].position < activeLedCount) {
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
    renderSharedWinSparkle(elapsed);
  } else {
    // Animation done - next level
    currentLevel++;
    gameState = STATE_PLAYING;
    spawnBoss();
  }
}

void renderSharedWinSparkle(unsigned long elapsed) {
  (void)elapsed;

  // Sparkle burst - random white/gold flashes (fireworks effect)
  FastLED.clear();

  // Create 10-15 random sparkles each frame
  int numSparkles = random(10, 16);
  for (int i = 0; i < numSparkles; i++) {
    int pos = random(activeLedCount);
    // Alternate between white and gold
    if (random(2) == 0) {
      leds[pos] = CRGB::White;
    } else {
      leds[pos] = CRGB(255, 215, 0);  // Gold
    }
  }
}

void playLoseAnimation() {
  unsigned long elapsed = millis() - animationStart;
  
  if (elapsed < LOSE_ANIMATION_DURATION) {
    // Explosion pattern - red flashing
    if ((elapsed / 200) % 2 == 0) {
      for (int i = 0; i < activeLedCount; i++) {
        leds[i] = CRGB::Red;
      }
    } else {
      for (int i = 0; i < activeLedCount; i++) {
        leds[i] = CRGB::Orange;
      }
    }
  } else {
    // Animation done - restart game
    Serial.println("[INFO] Game Over - Restarting...\n");
    initGame();
  }
}

void playLifeLostAnimation() {
  const uint8_t FAST_FADE_STEPS = 16;
  const uint8_t SLOW_FADE_STEPS = 48;
  const uint16_t FAST_FADE_STEP_MS = 14;
  const uint16_t SLOW_FADE_STEP_MS = 20;

  // Pulse 1: short "da"
  fill_solid(leds, activeLedCount, CRGB::Red);
  blackOutInactiveLeds();
  FastLED.show();
  delay(70);
  for (uint8_t step = 0; step <= FAST_FADE_STEPS; step++) {
    uint8_t level = (uint8_t)(255 - ((uint32_t)step * 255 / FAST_FADE_STEPS));
    CRGB c = CRGB::Red;
    c.nscale8_video(level);
    fill_solid(leds, activeLedCount, c);
    blackOutInactiveLeds();
    FastLED.show();
    delay(FAST_FADE_STEP_MS);
  }
  delay(60);

  // Pulse 2: short "da"
  fill_solid(leds, activeLedCount, CRGB::Red);
  blackOutInactiveLeds();
  FastLED.show();
  delay(70);
  for (uint8_t step = 0; step <= FAST_FADE_STEPS; step++) {
    uint8_t level = (uint8_t)(255 - ((uint32_t)step * 255 / FAST_FADE_STEPS));
    CRGB c = CRGB::Red;
    c.nscale8_video(level);
    fill_solid(leds, activeLedCount, c);
    blackOutInactiveLeds();
    FastLED.show();
    delay(FAST_FADE_STEP_MS);
  }
  delay(60);

  // Pulse 3: long "daaaaa"
  fill_solid(leds, activeLedCount, CRGB::Red);
  blackOutInactiveLeds();
  FastLED.show();
  delay(180);
  for (uint8_t step = 0; step <= SLOW_FADE_STEPS; step++) {
    uint8_t level = (uint8_t)(255 - ((uint32_t)step * 255 / SLOW_FADE_STEPS));
    CRGB c = CRGB::Red;
    c.nscale8_video(level);
    fill_solid(leds, activeLedCount, c);
    blackOutInactiveLeds();
    FastLED.show();
    delay(SLOW_FADE_STEP_MS);
  }

  fill_solid(leds, activeLedCount, CRGB::Black);
  blackOutInactiveLeds();
  FastLED.show();
  delay(120);
}

// ============================================
// ESP-NOW REMOTE CONTROL FUNCTIONS
// ============================================
bool isMacConfigured(const uint8_t *mac) {
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] != 0x00) {
      return true;
    }
  }
  return false;
}

int8_t findAllowedRemoteIndex(const uint8_t *mac) {
  for (uint8_t slot = 0; slot < ALLOWED_REMOTE_SLOT_COUNT; slot++) {
    if (!isMacConfigured(ALLOWED_REMOTE_MACS[slot])) {
      continue;
    }

    bool matches = true;
    for (uint8_t i = 0; i < 6; i++) {
      if (mac[i] != ALLOWED_REMOTE_MACS[slot][i]) {
        matches = false;
        break;
      }
    }

    if (matches) {
      return slot;
    }
  }

  return -1;
}

void queueUnknownRemoteReport(const uint8_t *mac, int len) {
  if (unknownRemoteReportPending) {
    return;
  }

  for (uint8_t i = 0; i < 6; i++) {
    unknownRemoteMac[i] = mac[i];
  }
  unknownRemotePacketLength = len;
  unknownRemoteReportPending = true;
}

void printMacToSerial(const uint8_t *mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint8_t getCurrentWiFiChannel() {
  uint8_t primaryChannel = 0;
  wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;

  if (esp_wifi_get_channel(&primaryChannel, &secondChannel) != ESP_OK) {
    return 0;
  }

  return primaryChannel;
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(100);

  Serial.println("[ESPNOW] WiFi initialized in Station mode");
  Serial.print("[ESPNOW] D1 Mini32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[ESPNOW] ESP-NOW channel: %u\n", (unsigned)ESPNOW_FIXED_CHANNEL);
}

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] ERROR: Failed to initialize ESP-NOW");
    return;
  }

  // Set channel AFTER esp_now_init
  esp_wifi_set_channel(ESPNOW_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[ESPNOW] ESP-NOW initialized on channel %u\n", (unsigned)getCurrentWiFiChannel());

  Serial.println("[ESPNOW] Allowed sender MACs:");
  for (uint8_t slot = 0; slot < ALLOWED_REMOTE_SLOT_COUNT; slot++) {
    Serial.printf("[ESPNOW]   Slot %u: ", (unsigned)(slot + 1));
    if (isMacConfigured(ALLOWED_REMOTE_MACS[slot])) {
      printMacToSerial(ALLOWED_REMOTE_MACS[slot]);
      Serial.println();
    } else {
      Serial.println("<empty>");
    }
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("[ESPNOW] Receive callback registered");
  Serial.println("[ESPNOW] Waiting for remote commands...");
}

void flushUnknownRemoteReport() {
  if (!unknownRemoteReportPending) {
    return;
  }

  uint8_t mac[6];
  int packetLen;

  noInterrupts();
  for (uint8_t i = 0; i < 6; i++) {
    mac[i] = unknownRemoteMac[i];
  }
  packetLen = unknownRemotePacketLength;
  unknownRemoteReportPending = false;
  interrupts();

  bool sameAsLast = true;
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] != lastReportedUnknownRemoteMac[i]) {
      sameAsLast = false;
      break;
    }
  }

  unsigned long now = millis();
  if (sameAsLast && (now - lastUnknownRemoteReportAt) < UNKNOWN_REMOTE_REPORT_SUPPRESS_MS) {
    return;
  }

  for (uint8_t i = 0; i < 6; i++) {
    lastReportedUnknownRemoteMac[i] = mac[i];
  }
  lastUnknownRemoteReportAt = now;

  Serial.print("[ESPNOW] Unknown sender MAC detected: ");
  printMacToSerial(mac);
  Serial.printf(" (len: %d)\n", packetLen);
  Serial.println("[ESPNOW] Add this MAC to ALLOWED_REMOTE_MACS in src/controller.cpp to authorize it.");
}

bool enqueueRemoteCommand(uint8_t command) {
  if (remoteCommandQueueCount >= REMOTE_COMMAND_QUEUE_SIZE) {
    remoteCommandQueueOverflow = true;
    return false;
  }

  remoteCommandQueue[remoteCommandQueueTail] = command;
  remoteCommandQueueTail = (remoteCommandQueueTail + 1) % REMOTE_COMMAND_QUEUE_SIZE;
  remoteCommandQueueCount = remoteCommandQueueCount + 1;
  return true;
}

bool dequeueRemoteCommand(uint8_t *command) {
  noInterrupts();
  if (remoteCommandQueueCount == 0) {
    interrupts();
    return false;
  }

  *command = remoteCommandQueue[remoteCommandQueueHead];
  remoteCommandQueueHead = (remoteCommandQueueHead + 1) % REMOTE_COMMAND_QUEUE_SIZE;
  remoteCommandQueueCount = remoteCommandQueueCount - 1;
  interrupts();
  return true;
}

void flushRemoteCommandQueueOverflowReport() {
  if (!remoteCommandQueueOverflow) {
    return;
  }

  noInterrupts();
  remoteCommandQueueOverflow = false;
  interrupts();
  Serial.println("[ESPNOW] WARNING: Remote command queue overflow; some inputs were dropped.");
}

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  // This runs in interrupt context - keep it SHORT!

  if (handlePlayer2EspNowPacket(recv_info, data, len)) {
    return;
  }

  int8_t allowedRemoteIndex = findAllowedRemoteIndex(recv_info->src_addr);
  if (allowedRemoteIndex < 0) {
    queueUnknownRemoteReport(recv_info->src_addr, len);
    return;
  }

  // Check packet length (WIZ-remote sends 13 bytes)
  if (len == 13) {
    uint8_t sequence = data[1];
    uint8_t buttonCode = data[6];

    // Debounce per sender slot so multiple remotes do not suppress each other.
    if (!lastRemoteSequenceValidBySlot[allowedRemoteIndex] ||
        sequence != lastRemoteSequenceBySlot[allowedRemoteIndex]) {
      lastRemoteSequenceBySlot[allowedRemoteIndex] = sequence;
      lastRemoteSequenceValidBySlot[allowedRemoteIndex] = true;
      enqueueRemoteCommand(buttonCode);

      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
               recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
      Serial.printf("[ESPNOW] Remote slot %d: %s, Seq: %d, Button: 0x%02X\n",
                    allowedRemoteIndex + 1, macStr, sequence, buttonCode);
    }
  }
}

bool handlePlayer2EspNowPacket(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  char macStr[18];
  bool senderChanged;
  const Player2EspNowPacket *packet;

  if (!isPlayer2EspNowPacket(data, len)) {
    return false;
  }

  packet = reinterpret_cast<const Player2EspNowPacket*>(data);
  senderChanged = !lastPlayer2SenderMacValid || memcmp(recv_info->src_addr, lastPlayer2SenderMac, 6) != 0;

  if (senderChanged) {
    memcpy(lastPlayer2SenderMac, recv_info->src_addr, 6);
    lastPlayer2SenderMacValid = true;
    lastPlayer2SequenceValid = false;
  }

  if (lastPlayer2SequenceValid && packet->sequence == lastPlayer2Sequence) {
    return true;
  }

  lastPlayer2Sequence = packet->sequence;
  lastPlayer2SequenceValid = true;
  enqueueRemoteCommand(packet->buttonCode);

  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);

  Serial.printf("[ESPNOW] Player-2 packet: %s, Seq: %u, Button: 0x%02X%s\n",
                macStr,
                packet->sequence,
                packet->buttonCode,
                senderChanged ? " [new sender]" : "");
  return true;
}

void processRemoteCommand() {
  // Process pending remote command (runs in main loop, not interrupt)
  uint8_t cmd;
  if (!dequeueRemoteCommand(&cmd)) return;  // No command pending

  lastUserActivityAt = millis();
  idlePauseTargetActive = false;
  idlePauseActive = false;

  if (settingsModeActive) {
    return;
  }
  
  // Only process commands during gameplay
  if (gameState == STATE_PLAYING) {
    switch (cmd) {
      case 0x10:  // Button 1 - Fire RED shot
        Serial.println("[REMOTE] Button 1 - Fire RED");
        if (!modeDotsActive && !(buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive)) {
          if (buttonLEDMode == LED_MODE_PONG_DUEL) {
            handlePongHitPlayer2();
          } else if (buttonLEDMode == LED_MODE_DUEL) {
            fireDuelShotPlayer2(0);
          } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
            fireAllVsAllShotPlayer2(0);
          } else if (buttonLEDMode == LED_MODE_COOP) {
            fireCoopShotPlayer2(0);
          } else {
            fireShot(0);
          }
        }
        break;
        
      case 0x11:  // Button 2 - Fire GREEN shot
        Serial.println("[REMOTE] Button 2 - Fire GREEN");
        if (!modeDotsActive && !(buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive)) {
          if (buttonLEDMode == LED_MODE_PONG_DUEL) {
            handlePongHitPlayer2();
          } else if (buttonLEDMode == LED_MODE_DUEL) {
            fireDuelShotPlayer2(1);
          } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
            fireAllVsAllShotPlayer2(1);
          } else if (buttonLEDMode == LED_MODE_COOP) {
            fireCoopShotPlayer2(1);
          } else {
            fireShot(1);
          }
        }
        break;
        
      case 0x12:  // Button 3 - Fire BLUE (always)
        Serial.println("[REMOTE] Button 3 - Fire BLUE");
        if (!modeDotsActive && !(buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive)) {
          if (buttonLEDMode == LED_MODE_PONG_DUEL) {
            handlePongHitPlayer2();
          } else if (buttonLEDMode == LED_MODE_DUEL) {
            fireDuelShotPlayer2(2);
          } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
            fireAllVsAllShotPlayer2(2);
          } else if (buttonLEDMode == LED_MODE_COOP) {
            fireCoopShotPlayer2(2);
          } else {
            fireShot(2);  // Blue
          }
        }
        break;
        
      case 0x13:  // Button 4 - Fire WHITE (only in 4-color mode)
        if (buttonLEDMode == LED_MODE_PONG_DUEL) {
          Serial.println("[REMOTE] Button 4 - Pong HIT");
          if (!modeDotsActive && !(buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive)) {
            handlePongHitPlayer2();
          }
        } else if (numColors == 4) {
          Serial.println("[REMOTE] Button 4 - Fire WHITE");
          if (!modeDotsActive && !(buttonLEDMode == LED_MODE_MEMORY && memoryPlaybackActive)) {
            if (buttonLEDMode == LED_MODE_DUEL) {
              fireDuelShotPlayer2(3);
            } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
              fireAllVsAllShotPlayer2(3);
            } else if (buttonLEDMode == LED_MODE_COOP) {
              fireCoopShotPlayer2(3);
            } else {
              fireShot(3);  // White
            }
          }
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
        restartCurrentGameMode();
        break;
        
      case 0x02:  // Off - Cycle button LED mode
        cycleButtonLEDMode();
        break;
        
      default:
        Serial.printf("[REMOTE] Unmapped button: 0x%02X\n", cmd);
        break;
    }
  } else {
    // During animations, allow reset, color mode toggle, and LED mode change
    if (cmd == 0x01) {  // On button
      Serial.println("[REMOTE] ON - Resetting game");
      restartCurrentGameMode();
    } else if (cmd == 0x03) {  // Sleep - Toggle color mode
      if (numColors == 3) {
        numColors = 4;
        Serial.println("[REMOTE] Sleep - Switched to 4-COLOR mode (Red/Green/Blue/White)");
      } else {
        numColors = 3;
        Serial.println("[REMOTE] Sleep - Switched to 3-COLOR mode (Red/Green/Blue)");
      }
      Serial.printf("[INFO] Color mode: %d colors\n", numColors);
    } else if (cmd == 0x02) {  // Off - Cycle button LED mode
      cycleButtonLEDMode();
    }
  }
}

const char* getButtonLEDModeName(ButtonLEDMode mode) {
  switch (mode) {
    case LED_MODE_INVERTED: return "INVERTED";
    case LED_MODE_FOLLOW_ME: return "FOLLOW-ME";
    case LED_MODE_MEMORY: return "MEMORY";
    case LED_MODE_GHOST_BOSS: return "GHOST BOSS";
    case LED_MODE_DUEL: return "DUEL";
    case LED_MODE_COOP: return "CO-PLAY";
    case LED_MODE_ALL_VS_ALL: return "ALL-VS-ALL";
    case LED_MODE_PONG_DUEL: return "PONG DUEL";
    default: return "UNKNOWN";
  }
}

void cycleButtonLEDMode() {
  buttonLEDMode = (ButtonLEDMode)((buttonLEDMode + 1) % LED_MODE_COUNT);

  // Show mode number indicator before activating the selected mode behavior.
  modeDotsActive = true;
  modeDotsStartTime = millis();

  // Pause memory playback until indicator phase finishes.
  memoryPlaybackActive = false;

  // Deactivate ghost behavior during indicator; it will activate after timeout for mode 5.
  ghostBossModeEnabled = false;
  ghostBossVisible = true;

  uint8_t modeNumber = (uint8_t)buttonLEDMode + 1;
  Serial.printf("[REMOTE] OFF - Game Mode %d: %s\n", modeNumber, getButtonLEDModeName(buttonLEDMode));
  Serial.println("[INFO] Showing mode dots for 2s...");
}

void startModeSelectionIndicator() {
  modeDotsActive = true;
  modeDotsStartTime = millis();

  // Pause mode-specific effects until indicator timeout.
  memoryPlaybackActive = false;
  ghostBossModeEnabled = false;
  ghostBossVisible = true;
}

void applySettingsSelectionAndExit() {
  settingsModeActive = false;
  settingsWaitRelease = false;
  settingsRedHoldStart = 0;

  bool ledLengthChanged = (activeLedCount != settingsSelectedLedCount);

  buttonLEDMode = settingsSelectedMode;
  numColors = settingsSelectedNumColors;
  activeLedCount = settingsSelectedLedCount;
  settingsLengthAdjustActive = false;

  if (ledLengthChanged) {
    Serial.printf("[SETTINGS] Applied LED length %d -> restarting game mode\n", activeLedCount);
    restartCurrentGameMode();
    return;
  }

  startModeSelectionIndicator();

  Serial.printf("[SETTINGS] Applied: Mode %d (%s), Colors %d, LEDs %d\n",
                (uint8_t)buttonLEDMode + 1,
                getButtonLEDModeName(buttonLEDMode),
                numColors,
                activeLedCount);
}

void restartCurrentGameMode() {
  if (buttonLEDMode == LED_MODE_DUEL) {
    initDuelMode();
  } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
    initAllVsAllMode();
  } else if (buttonLEDMode == LED_MODE_COOP) {
    initCoopMode();
  } else if (buttonLEDMode == LED_MODE_PONG_DUEL) {
    initPongDuelMode();
  } else {
    initGame();
  }
}

void updateModeDotsIndicator() {
  if (!modeDotsActive) return;

  if (millis() - modeDotsStartTime < MODE_DOTS_DURATION_MS) {
    return;
  }

  modeDotsActive = false;

  // Start mode-specific behavior after indicator timeout.
  ghostBossModeEnabled = (buttonLEDMode == LED_MODE_GHOST_BOSS);
  ghostBossVisible = true;
  ghostBossModeStartTime = millis();
  ghostBossRevealUntil = 0;
  ghostBossLastRevealTrigger = ghostBossModeStartTime;
  ghostBossFadingOut = false;
  ghostBossFadeStart = 0;

  if (buttonLEDMode == LED_MODE_MEMORY) {
    startMemoryPlaybackSequence();
  } else if (buttonLEDMode == LED_MODE_DUEL) {
    initDuelMode();
  } else if (buttonLEDMode == LED_MODE_ALL_VS_ALL) {
    initAllVsAllMode();
  } else if (buttonLEDMode == LED_MODE_COOP) {
    initCoopMode();
  } else if (buttonLEDMode == LED_MODE_PONG_DUEL) {
    initPongDuelMode();
  }

  Serial.printf("[INFO] Mode %d active: %s\n", (uint8_t)buttonLEDMode + 1, getButtonLEDModeName(buttonLEDMode));
}

void renderSettingsOverlay() {
  if (!settingsModeActive) return;

  uint16_t previewLedCount = settingsSelectedLedCount;

  if (settingsLengthAdjustActive) {
    // LED length preview: active region dim blue, endpoint white, inactive region black.
    for (int i = 0; i < NUM_LEDS; i++) {
      if (i < previewLedCount) {
        leds[i] = CRGB(0, 0, 20);
      } else {
        leds[i] = CRGB::Black;
      }
    }
    int endPos = previewLedCount - 1;
    if (endPos >= 0 && endPos < NUM_LEDS) {
      leds[endPos] = CRGB::White;
    }
    return;
  }

  // Show selected mode as lilac dots at far end.
  uint8_t modeNumber = (uint8_t)settingsSelectedMode + 1;
  for (uint8_t dot = 0; dot < modeNumber; dot++) {
    int dotEndPos = ((int)previewLedCount - 1) - (dot * 4);
    int dotStartPos = dotEndPos - 1;

    if (dotStartPos >= 0 && dotStartPos < previewLedCount) {
      leds[dotStartPos] = MODE_DOT_COLOR;
    }
    if (dotEndPos >= 0 && dotEndPos < previewLedCount) {
      leds[dotEndPos] = MODE_DOT_COLOR;
    }
  }

  // Show selected color mode near player 1 side.
  if (settingsSelectedNumColors == 4) {
    leds[0] = CRGB::White;
  } else {
    leds[0] = CRGB::Blue;
  }
}

void updateIdlePauseState() {
  if (gameState != STATE_PLAYING) {
    idlePauseTargetActive = false;
    idlePauseActive = false;
    return;
  }

  if (settingsModeActive || modeDotsActive) {
    idlePauseTargetActive = false;
    idlePauseActive = false;
    return;
  }

  unsigned long now = millis();
  bool shouldPause = (now - lastUserActivityAt >= IDLE_PAUSE_TIMEOUT_MS);

  if (shouldPause && !idlePauseTargetActive) {
    idlePauseTargetActive = true;

    // Snapshot current frame for smooth fade transition.
    for (int i = 0; i < NUM_LEDS; i++) {
      idlePauseSnapshot[i] = leds[i];
    }

    Serial.println("[INFO] Idle pause screen active");
  } else if (!shouldPause && idlePauseTargetActive) {
    idlePauseTargetActive = false;
    Serial.println("[INFO] Idle pause screen leaving");
  }

  idlePauseActive = (idlePauseBlend == 255);
}

void updateIdlePauseBlend() {
  unsigned long now = millis();
  unsigned long delta = now - idlePauseLastBlendUpdate;
  if (delta == 0) {
    return;
  }
  idlePauseLastBlendUpdate = now;

  uint16_t fadeMs = idlePauseTargetActive ? IDLE_PAUSE_FADE_IN_MS : IDLE_PAUSE_FADE_OUT_MS;
  uint8_t step = (uint8_t)(((uint32_t)delta * 255) / fadeMs);
  if (step == 0) {
    step = 1;
  }

  if (idlePauseTargetActive) {
    if (idlePauseBlend + step < idlePauseBlend) {
      idlePauseBlend = 255;
    } else {
      uint16_t next = (uint16_t)idlePauseBlend + step;
      idlePauseBlend = (next > 255) ? 255 : (uint8_t)next;
    }
  } else {
    if (idlePauseBlend > step) {
      idlePauseBlend -= step;
    } else {
      idlePauseBlend = 0;
    }
  }

  idlePauseActive = (idlePauseBlend == 255);
}

void renderIdlePauseScreen(CRGB* out) {
  // Slow mesmerizing aurora-like wave.
  uint32_t t = millis();
  for (int i = 0; i < activeLedCount; i++) {
    uint8_t waveA = sin8((uint8_t)((i * 3) + (t / 14)));
    uint8_t waveB = sin8((uint8_t)((i * 5) - (t / 19)));
    uint8_t hue = (uint8_t)((i * 2) + (t / 26) + (waveB / 5));
    uint8_t sat = 170;
    uint8_t val = (uint8_t)(28 + (waveA / 3));
    out[i] = CHSV(hue, sat, val);
  }
  for (int i = activeLedCount; i < NUM_LEDS; i++) {
    out[i] = CRGB::Black;
  }

  // Gentle symmetric breathing highlights from both ends.
  uint8_t breath = (uint8_t)(16 + (sin8((uint8_t)(t / 20)) / 6));
  int glowDepth = activeLedCount / 10;
  if (glowDepth < 2) glowDepth = 2;
  for (int d = 0; d < glowDepth; d++) {
    uint8_t scale = (uint8_t)(255 - ((uint16_t)d * 180 / glowDepth));
    CRGB edge = CRGB(breath, breath, breath);
    edge.nscale8_video(scale);
    if (d < activeLedCount) {
      out[d] += edge;
      out[activeLedCount - 1 - d] += edge;
    }
  }
}

void renderModeDotsIndicator() {
  if (!modeDotsActive) return;

  uint8_t modeNumber = (uint8_t)buttonLEDMode + 1;
  for (uint8_t dot = 0; dot < modeNumber; dot++) {
    int dotEndPos = ((int)activeLedCount - 1) - (dot * 4);
    int dotStartPos = dotEndPos - 1;

    if (dotStartPos >= 0 && dotStartPos < activeLedCount) {
      leds[dotStartPos] = MODE_DOT_COLOR;
    }
    if (dotEndPos >= 0 && dotEndPos < activeLedCount) {
      leds[dotEndPos] = MODE_DOT_COLOR;
    }
  }
}

void updateGhostBossVisibility() {
  if (!ghostBossModeEnabled || gameState != STATE_PLAYING) {
    ghostBossVisible = true;
    ghostBossFadingOut = false;
    return;
  }

  unsigned long now = millis();

  // Grace exception: keep boss fully visible when close to player.
  int frontPartIdx = -1;
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) {
      frontPartIdx = i;
      break;
    }
  }
  if (frontPartIdx != -1) {
    int frontPosition = bossPosition + frontPartIdx;
    int impactPosition = (playerLives > 0) ? (playerLives - 1) : 0;
    if (frontPosition - impactPosition <= GHOST_BOSS_NEAR_PLAYER_VISIBLE_DISTANCE) {
      ghostBossVisible = true;
      ghostBossFadingOut = false;
      return;
    }
  }

  // First phase: always visible for 5 seconds.
  if (now - ghostBossModeStartTime < GHOST_BOSS_INITIAL_VISIBLE_MS) {
    ghostBossVisible = true;
    ghostBossFadingOut = false;
    return;
  }

  // Reveal window keeps boss visible and cancels fade-out.
  if (now < ghostBossRevealUntil) {
    ghostBossVisible = true;
    ghostBossFadingOut = false;
    return;
  }

  // Transition to invisible with fade-out + blackout flickers.
  if (ghostBossFadingOut) {
    if (now - ghostBossFadeStart >= GHOST_BOSS_FADE_OUT_MS) {
      ghostBossFadingOut = false;
      ghostBossVisible = false;
    } else {
      ghostBossVisible = true;
    }
    return;
  }

  if (ghostBossVisible) {
    ghostBossFadingOut = true;
    ghostBossFadeStart = now;
    return;
  }

  // Fallback reveal: if no hit trigger for 10 seconds, reveal for 1 second.
  if (now - ghostBossLastRevealTrigger >= GHOST_BOSS_FALLBACK_REVEAL_INTERVAL_MS) {
    ghostBossRevealUntil = now + GHOST_BOSS_REVEAL_MS;
    ghostBossLastRevealTrigger = now;
    ghostBossVisible = true;
    ghostBossFadingOut = false;
  }
}

void notifyGhostBossHit() {
  if (!ghostBossModeEnabled || gameState != STATE_PLAYING) {
    return;
  }

  unsigned long now = millis();
  ghostBossRevealUntil = now + GHOST_BOSS_REVEAL_MS;
  ghostBossLastRevealTrigger = now;
  ghostBossFadingOut = false;
  ghostBossVisible = true;
}

void initDuelMode() {
  gameState = STATE_PLAYING;
  duelGameOver = false;
  duelWinner = 0;
  duelGameOverStart = 0;

  for (int i = 0; i < MAX_SHOTS; i++) {
    player1Shots[i].position = -1;
    player2Shots[i].position = -1;
  }

  Serial.println("[INFO] DUEL mode started");
  Serial.println("[INFO] P1: Physical buttons from lower end");
  Serial.println("[INFO] P2: Remote number buttons from far end");
  Serial.println("[INFO] Match colors to block incoming shots");
}

void fireDuelShotPlayer1(uint8_t color) {
  if (duelGameOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player1Shots[i].position == -1) {
      player1Shots[i].position = PLAYER_SIZE;
      player1Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      Serial.printf("[DUEL] P1 fired %d from pos %d\n", color, player1Shots[i].position);
      return;
    }
  }
}

void fireDuelShotPlayer2(uint8_t color) {
  if (duelGameOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player2Shots[i].position == -1) {
      player2Shots[i].position = activeLedCount - PLAYER_SIZE - 1;
      player2Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      Serial.printf("[DUEL] P2 fired %d from pos %d\n", color, player2Shots[i].position);
      return;
    }
  }
}

void updateDuelGame() {
  if (duelGameOver) {
    unsigned long now = millis();
    if (now - duelGameOverStart >= DUEL_END_ANIMATION_MS) {
      Serial.println("[DUEL] Restarting next round");
      initDuelMode();
    }
    return;
  }

  unsigned long now = millis();
  if (now - lastShotMove < SHOT_SPEED) return;
  lastShotMove = now;

  int16_t p1Prev[MAX_SHOTS];
  int16_t p2Prev[MAX_SHOTS];

  for (int i = 0; i < MAX_SHOTS; i++) {
    p1Prev[i] = player1Shots[i].position;
    p2Prev[i] = player2Shots[i].position;
  }

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player1Shots[i].position != -1) {
      player1Shots[i].position++;
      if (player1Shots[i].position >= activeLedCount) {
        player1Shots[i].position = -1;
      }
    }
    if (player2Shots[i].position != -1) {
      player2Shots[i].position--;
      if (player2Shots[i].position < 0) {
        player2Shots[i].position = -1;
      }
    }
  }

  // Blocking: matching colors cancel each other on collide or crossing paths.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player1Shots[i].position == -1) continue;

    for (int j = 0; j < MAX_SHOTS; j++) {
      if (player2Shots[j].position == -1) continue;

      bool samePosition = (player1Shots[i].position == player2Shots[j].position);
      bool crossed = (p1Prev[i] != -1 && p2Prev[j] != -1 &&
                      p1Prev[i] < p2Prev[j] &&
                      player1Shots[i].position > player2Shots[j].position);

      if ((samePosition || crossed) && player1Shots[i].color == player2Shots[j].color) {
        player1Shots[i].position = -1;
        player2Shots[j].position = -1;
        Serial.println("[DUEL] Block! Matching colors canceled shots");
        break;
      }
    }
  }

  // Win condition: hit opponent zone.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player1Shots[i].position >= activeLedCount - PLAYER_SIZE) {
      duelGameOver = true;
      duelWinner = 1;
      duelGameOverStart = millis();
      Serial.println("[DUEL] Player 1 wins");
      return;
    }
    if (player2Shots[i].position != -1 && player2Shots[i].position <= (PLAYER_SIZE - 1)) {
      duelGameOver = true;
      duelWinner = 2;
      duelGameOverStart = millis();
      Serial.println("[DUEL] Player 2 wins");
      return;
    }
  }
}

void renderDuelGame() {
  FastLED.clear();

  if (duelGameOver) {
    renderDuelEndAnimation();
    return;
  }

  // Player zones
  for (int i = 0; i < PLAYER_SIZE; i++) {
    leds[i] = CRGB::White;
  }
  for (int i = activeLedCount - PLAYER_SIZE; i < activeLedCount; i++) {
    leds[i] = CRGB::White;
  }

  // Shots from player 1
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player1Shots[i].position >= 0 && player1Shots[i].position < activeLedCount) {
      leds[player1Shots[i].position] = colorTable[player1Shots[i].color];
    }
  }

  // Shots from player 2
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (player2Shots[i].position >= 0 && player2Shots[i].position < activeLedCount) {
      leds[player2Shots[i].position] = colorTable[player2Shots[i].color];
    }
  }

}

void renderDuelEndAnimation() {
  unsigned long elapsed = millis() - duelGameOverStart;
  CRGB winnerColor = (duelWinner == 1) ? CRGB::Green : CRGB::Blue;
  CRGB accentColor = CRGB::White;
  CRGB loserColor = CRGB::DarkRed;

  fill_solid(leds, activeLedCount, CRGB::Black);

  int winnerStart = 0;
  int winnerEnd = activeLedCount / 3;
  int loserStart = (activeLedCount * 2) / 3;
  int loserEnd = activeLedCount - 1;

  if (duelWinner == 2) {
    winnerStart = (activeLedCount * 2) / 3;
    winnerEnd = activeLedCount - 1;
    loserStart = 0;
    loserEnd = activeLedCount / 3;
  }

  // WINNER ZONE: bright and energetic (rapid strobe + sparkles).
  bool winnerFlash = ((elapsed / 70) % 2) == 0;
  for (int i = winnerStart; i <= winnerEnd; i++) {
    if (i < 0 || i >= activeLedCount) continue;
    leds[i] = winnerFlash ? winnerColor : accentColor;
  }

  for (int s = 0; s < 14; s++) {
    if (random8() < 210) {
      int span = winnerEnd - winnerStart + 1;
      if (span < 1) span = 1;
      int pos = winnerStart + random(span);
      if (pos >= 0 && pos < activeLedCount) {
        leds[pos] = (random8() < 140) ? accentColor : winnerColor;
      }
    }
  }

  // LOSER ZONE: dim, slow, and striped (visually opposite of winner zone).
  uint8_t loserPulse = (uint8_t)(20 + ((elapsed / 8) % 50));
  for (int i = loserStart; i <= loserEnd; i++) {
    if (i < 0 || i >= activeLedCount) continue;
    if (((i + (elapsed / 180)) % 2) == 0) {
      CRGB c = loserColor;
      c.nscale8_video(loserPulse);
      leds[i] = c;
    }
  }

  // CENTER: directional victory sweep from winner side to loser side.
  int centerStart = winnerEnd + 1;
  int centerEnd = loserStart - 1;
  if (centerEnd >= centerStart) {
    int centerLen = centerEnd - centerStart + 1;
    uint16_t phase = elapsed % DUEL_RUN_PERIOD_MS;
    int offset = (int)((uint32_t)phase * centerLen / DUEL_RUN_PERIOD_MS);
    int head = (duelWinner == 1) ? (centerStart + offset) : (centerEnd - offset);

    for (int t = 0; t < 6; t++) {
      int pos = (duelWinner == 1) ? (head - t) : (head + t);
      if (pos >= centerStart && pos <= centerEnd && pos >= 0 && pos < activeLedCount) {
        leds[pos] = (t == 0) ? accentColor : winnerColor;
      }
    }
  }
}

uint8_t getPongBaseZoneSize() {
  int16_t suggested = activeLedCount / 6;
  int16_t maxAllowed = (activeLedCount / 2) - 2;

  if (maxAllowed < 5) {
    maxAllowed = 5;
  }
  if (suggested < 5) {
    suggested = 5;
  }
  if (suggested > maxAllowed) {
    suggested = maxAllowed;
  }

  return (uint8_t)suggested;
}

uint8_t getPongMinZoneSize() {
  uint8_t baseZoneSize = getPongBaseZoneSize();
  uint8_t minZoneSize = baseZoneSize / 4;

  if (minZoneSize < 5) {
    minZoneSize = 5;
  }
  if (minZoneSize > baseZoneSize) {
    minZoneSize = baseZoneSize;
  }

  return minZoneSize;
}

uint8_t getPongZoneShrinkPerPoint() {
  uint8_t baseZoneSize = getPongBaseZoneSize();
  uint8_t minZoneSize = getPongMinZoneSize();
  int16_t range = (int16_t)baseZoneSize - (int16_t)minZoneSize;

  if (range <= 0) {
    return 1;
  }

  int16_t shrinkPerPoint = range / (PONG_WIN_SCORE - 1);
  if ((range % (PONG_WIN_SCORE - 1)) != 0) {
    shrinkPerPoint++;
  }
  if (shrinkPerPoint < 1) {
    shrinkPerPoint = 1;
  }

  return (uint8_t)shrinkPerPoint;
}

uint8_t getPongZoneSizeForPlayer(uint8_t player) {
  uint8_t baseZoneSize = getPongBaseZoneSize();
  uint8_t minZoneSize = getPongMinZoneSize();
  uint8_t shrinkPerPoint = getPongZoneShrinkPerPoint();
  uint8_t playerScore = (player == 1) ? pongPlayer1Score : pongPlayer2Score;
  int16_t zoneSize = (int16_t)baseZoneSize - ((int16_t)playerScore * shrinkPerPoint);

  if (zoneSize < minZoneSize) {
    zoneSize = minZoneSize;
  }

  return (uint8_t)zoneSize;
}

int16_t getPongPlayer1ZoneEnd() {
  return (int16_t)getPongZoneSizeForPlayer(1) - 1;
}

int16_t getPongPlayer2ZoneStart() {
  return (int16_t)activeLedCount - (int16_t)getPongZoneSizeForPlayer(2);
}

int16_t getPongBallStepSize() {
  int16_t stepSize = activeLedCount / 72;
  if (stepSize < 1) {
    stepSize = 1;
  }
  return stepSize;
}

uint16_t getPongInitialBallDelay() {
  int16_t stepSize = getPongBallStepSize();
  int16_t halfTravelPixels = activeLedCount / 2;
  int16_t travelTicks = halfTravelPixels / stepSize;
  int32_t delayMs;

  if (travelTicks < 1) {
    travelTicks = 1;
  }

  delayMs = 1600 / travelTicks;
  if (delayMs < 16) {
    delayMs = 16;
  }
  if (delayMs > 60) {
    delayMs = 60;
  }

  return (uint16_t)delayMs;
}

uint16_t getPongMinimumBallDelay() {
  uint16_t minimumDelay = getPongInitialBallDelay() / 5;
  if (minimumDelay < 6) {
    minimumDelay = 6;
  }
  return minimumDelay;
}

uint16_t getPongSpeedupPerReturn() {
  uint16_t speedup = getPongInitialBallDelay() / 14;
  if (speedup < 1) {
    speedup = 1;
  }
  return speedup;
}

uint16_t getPongEarlyHitMaxBonus() {
  uint16_t bonus = getPongInitialBallDelay() / 8;
  if (bonus < 1) {
    bonus = 1;
  }
  return bonus;
}

bool isPongHitWindowForPlayer(uint8_t player) {
  if (pongPhase != PONG_PHASE_BALL_MOVING) {
    return false;
  }

  if (player == 1) {
    return (pongBallDirection < 0 && pongBallPosition >= 0 && pongBallPosition <= getPongPlayer1ZoneEnd());
  }

  return (pongBallDirection > 0 && pongBallPosition >= getPongPlayer2ZoneStart() && pongBallPosition < activeLedCount);
}

uint16_t getPongEarlyHitBonus(uint8_t player) {
  uint16_t maxBonus = getPongEarlyHitMaxBonus();
  uint8_t zoneSize = getPongZoneSizeForPlayer(player);
  int16_t denominator = (int16_t)zoneSize - 1;
  int16_t numerator;

  if (denominator <= 0) {
    return maxBonus;
  }

  if (player == 1) {
    numerator = pongBallPosition;
    if (numerator < 0) {
      numerator = 0;
    }
    if (numerator > denominator) {
      numerator = denominator;
    }
  } else {
    numerator = (activeLedCount - 1) - pongBallPosition;
    if (numerator < 0) {
      numerator = 0;
    }
    if (numerator > denominator) {
      numerator = denominator;
    }
  }

  return (uint16_t)(((uint32_t)maxBonus * (uint32_t)numerator) / (uint32_t)denominator);
}

void startPongServeCountdown() {
  pongPhase = PONG_PHASE_SERVE_COUNTDOWN;
  pongPhaseStart = millis();
  pongBallDirection = pongNextServeDirection;
  pongBallDelay = getPongInitialBallDelay();
  pongBallPosition = activeLedCount / 2;
  if (pongBallPosition >= activeLedCount) {
    pongBallPosition = activeLedCount - 1;
  }
  pongLastBallMove = pongPhaseStart;
  pongHitFlashPlayer = 0;
  pongHitFlashUntil = 0;

  Serial.printf("[PONG] Serve countdown started toward %s\n",
                (pongBallDirection < 0) ? "Player 1" : "Player 2");
}

void initPongDuelMode() {
  gameState = STATE_PLAYING;
  pongPlayer1Score = 0;
  pongPlayer2Score = 0;
  pongPointWinner = 0;
  pongPointLoser = 0;
  pongMatchWinner = 0;
  pongNextServeDirection = ((millis() / 100) & 0x01) ? 1 : -1;
  idlePauseActive = false;
  idlePauseTargetActive = false;
  startPongServeCountdown();

  Serial.println("[PONG] PONG DUEL mode started");
  Serial.println("[PONG] P1: local RED button");
  Serial.println("[PONG] P2: wireless remote / Player-2, optional local WHITE fallback");
}

void scorePongPoint(uint8_t winner, const char* reason) {
  pongPointWinner = winner;
  pongPointLoser = (winner == 1) ? 2 : 1;
  pongMatchWinner = 0;

  if (winner == 1) {
    pongPlayer1Score++;
    if (pongPlayer1Score >= PONG_WIN_SCORE) {
      pongMatchWinner = 1;
    }
  } else {
    pongPlayer2Score++;
    if (pongPlayer2Score >= PONG_WIN_SCORE) {
      pongMatchWinner = 2;
    }
  }

  pongNextServeDirection = (pongPointLoser == 1) ? -1 : 1;
  pongPhase = PONG_PHASE_POINT_FLASH;
  pongPhaseStart = millis();
  pongHitFlashPlayer = 0;
  pongHitFlashUntil = 0;

  Serial.printf("[PONG] Point for P%d (%s) -> score %d:%d\n",
                winner,
                reason,
                pongPlayer1Score,
                pongPlayer2Score);
}

void handlePongHitPlayer1() {
  uint16_t newDelay;

  lastAnyShotFiredAt = millis();

  if (pongPhase != PONG_PHASE_BALL_MOVING) {
    return;
  }

  if (!isPongHitWindowForPlayer(1)) {
    scorePongPoint(2, "P1 mistimed hit");
    return;
  }

  newDelay = getPongSpeedupPerReturn() + getPongEarlyHitBonus(1);
  if (pongBallDelay > (getPongMinimumBallDelay() + newDelay)) {
    pongBallDelay -= newDelay;
  } else {
    pongBallDelay = getPongMinimumBallDelay();
  }

  pongBallDirection = 1;
  pongHitFlashPlayer = 1;
  pongHitFlashUntil = millis() + PONG_HIT_FLASH_MS;
  Serial.printf("[PONG] P1 returned ball, delay %d ms\n", pongBallDelay);
}

void handlePongHitPlayer2() {
  uint16_t newDelay;

  lastAnyShotFiredAt = millis();

  if (pongPhase != PONG_PHASE_BALL_MOVING) {
    return;
  }

  if (!isPongHitWindowForPlayer(2)) {
    scorePongPoint(1, "P2 mistimed hit");
    return;
  }

  newDelay = getPongSpeedupPerReturn() + getPongEarlyHitBonus(2);
  if (pongBallDelay > (getPongMinimumBallDelay() + newDelay)) {
    pongBallDelay -= newDelay;
  } else {
    pongBallDelay = getPongMinimumBallDelay();
  }

  pongBallDirection = -1;
  pongHitFlashPlayer = 2;
  pongHitFlashUntil = millis() + PONG_HIT_FLASH_MS;
  Serial.printf("[PONG] P2 returned ball, delay %d ms\n", pongBallDelay);
}

void updatePongDuelGame() {
  unsigned long now = millis();

  if (pongHitFlashPlayer != 0 && now >= pongHitFlashUntil) {
    pongHitFlashPlayer = 0;
    pongHitFlashUntil = 0;
  }

  switch (pongPhase) {
    case PONG_PHASE_SERVE_COUNTDOWN:
      if (now - pongPhaseStart >= PONG_SERVE_COUNTDOWN_MS) {
        pongPhase = PONG_PHASE_BALL_MOVING;
        pongBallPosition = activeLedCount / 2;
        if (pongBallPosition >= activeLedCount) {
          pongBallPosition = activeLedCount - 1;
        }
        pongLastBallMove = now;
        pongBallDelay = getPongInitialBallDelay();
      }
      return;

    case PONG_PHASE_POINT_FLASH:
      if (now - pongPhaseStart >= PONG_POINT_FLASH_MS) {
        if (pongMatchWinner != 0) {
          pongPhase = PONG_PHASE_MATCH_OVER;
          pongPhaseStart = now;
          Serial.printf("[PONG] Player %d wins the match\n", pongMatchWinner);
        } else {
          startPongServeCountdown();
        }
      }
      return;

    case PONG_PHASE_MATCH_OVER:
      if (now - pongPhaseStart >= PONG_MATCH_OVER_MS) {
        initPongDuelMode();
      }
      return;

    case PONG_PHASE_BALL_MOVING:
      break;
  }

  if (now - pongLastBallMove < pongBallDelay) {
    return;
  }

  pongLastBallMove = now;
  pongBallPosition += pongBallDirection * getPongBallStepSize();

  if (pongBallPosition < 0) {
    scorePongPoint(2, "ball escaped left");
    return;
  }
  if (pongBallPosition >= activeLedCount) {
    scorePongPoint(1, "ball escaped right");
  }
}

void renderPongDuelGame() {
  int16_t leftCenter = (activeLedCount - 1) / 2;
  int16_t rightCenter = activeLedCount / 2;
  int16_t player1ZoneEnd = getPongPlayer1ZoneEnd();
  int16_t player2ZoneStart = getPongPlayer2ZoneStart();
  unsigned long now = millis();
  CRGB leftZoneColor = PONG_PLAYER1_COLOR;
  CRGB rightZoneColor = PONG_PLAYER2_COLOR;

  FastLED.clear();

  leftZoneColor.nscale8_video(96);
  rightZoneColor.nscale8_video(96);

  if (pongHitFlashPlayer == 1) {
    leftZoneColor = PONG_HIT_FLASH_COLOR;
  } else if (pongHitFlashPlayer == 2) {
    rightZoneColor = PONG_HIT_FLASH_COLOR;
  }

  if (pongPhase == PONG_PHASE_POINT_FLASH) {
    unsigned long flashElapsed = now - pongPhaseStart;
    unsigned long flashCycle = flashElapsed % 200;
    bool showMissColor = (flashCycle < 120);

    if (showMissColor) {
      if (pongPointLoser == 1) {
        leftZoneColor = PONG_MISS_FLASH_COLOR;
      } else if (pongPointLoser == 2) {
        rightZoneColor = PONG_MISS_FLASH_COLOR;
      }
    }
  }

  if (pongPhase == PONG_PHASE_MATCH_OVER) {
    bool flashOn = ((now - pongPhaseStart) % 200) < 120;
    if (flashOn) {
      fill_solid(leds, activeLedCount, (pongMatchWinner == 1) ? PONG_PLAYER1_COLOR : PONG_PLAYER2_COLOR);
    }
    return;
  }

  for (int i = 0; i <= player1ZoneEnd && i < activeLedCount; i++) {
    leds[i] = leftZoneColor;
  }
  for (int i = player2ZoneStart; i < activeLedCount; i++) {
    if (i >= 0) {
      leds[i] = rightZoneColor;
    }
  }

  for (int i = 0; i < pongPlayer1Score; i++) {
    int16_t index = leftCenter - i;
    if (index >= 0 && index < activeLedCount) {
      leds[index] += CRGB(100, 0, 0);
    }
  }
  for (int i = 0; i < pongPlayer2Score; i++) {
    int16_t index = rightCenter + i;
    if (index >= 0 && index < activeLedCount) {
      leds[index] += CRGB(0, 0, 100);
    }
  }

  if (pongPhase == PONG_PHASE_SERVE_COUNTDOWN) {
    bool pulseOn = (((now - pongPhaseStart) / 180) % 2) == 0;
    if (pulseOn) {
      if (leftCenter >= 0 && leftCenter < activeLedCount) {
        leds[leftCenter] = PONG_SERVE_COLOR;
      }
      if (rightCenter >= 0 && rightCenter < activeLedCount) {
        leds[rightCenter] = PONG_SERVE_COLOR;
      }
    }
    return;
  }

  if (pongPhase == PONG_PHASE_BALL_MOVING && pongBallPosition >= 0 && pongBallPosition < activeLedCount) {
    leds[pongBallPosition] = PONG_BALL_COLOR;

    for (int trail = 1; trail <= 3; trail++) {
      int16_t trailPos = pongBallPosition - (pongBallDirection * trail);
      if (trailPos < 0 || trailPos >= activeLedCount) {
        continue;
      }

      if (trail == 1) {
        leds[trailPos] += CRGB(120, 120, 120);
      } else if (trail == 2) {
        leds[trailPos] += CRGB(50, 50, 50);
      } else {
        leds[trailPos] += CRGB(20, 20, 20);
      }
    }
  }
}

void initAllVsAllMode() {
  gameState = STATE_PLAYING;
  coopRound = 1;
  allVsAllRoundOver = false;
  allVsAllPlayersWin = false;
  allVsAllWinner = 0;
  allVsAllRoundOverStart = 0;
  allVsAllLossAnimationPlayed = false;

  for (int i = 0; i < MAX_SHOTS; i++) {
    allVsAllPlayer1Shots[i].position = -1;
    allVsAllPlayer2Shots[i].position = -1;
    allVsAllPrevP1Shots[i] = -1;
    allVsAllPrevP2Shots[i] = -1;
  }

  spawnAllVsAllRound();

  Serial.println("[INFO] ALL-VS-ALL mode started");
  Serial.println("[INFO] Boss expands from center like CO-PLAY");
  Serial.println("[INFO] Players shoot from both ends like DUEL");
}

void spawnAllVsAllRound() {
  allVsAllLossAnimationPlayed = false;
  allVsAllWinner = 0;

  coopBossPartsThisRound = coopRound * 2;
  if (coopBossPartsThisRound > MAX_COOP_BOSS_PARTS) {
    coopBossPartsThisRound = MAX_COOP_BOSS_PARTS;
  }

  coopSpacingThisRound = coopRound - 1;
  if (coopSpacingThisRound > 8) {
    coopSpacingThisRound = 8;
  }

  int32_t calculatedSpeed = COOP_BOSS_INITIAL_SPEED - ((coopRound - 1) * COOP_BOSS_SPEED_DECREASE);
  if (calculatedSpeed < COOP_BOSS_MIN_SPEED) {
    coopBossSpeed = COOP_BOSS_MIN_SPEED;
  } else {
    coopBossSpeed = (uint16_t)calculatedSpeed;
  }

  for (int i = 0; i < MAX_COOP_BOSS_PARTS; i++) {
    coopBoss[i].active = false;
    coopBoss[i].position = -1;
    coopBoss[i].color = 0;
  }

  int centerLeft = (activeLedCount - 1) / 2;
  int centerRight = activeLedCount / 2;
  coopBossLeftEdge = centerLeft;
  coopBossRightEdge = centerRight;

  for (int i = 0; i < coopBossPartsThisRound; i++) {
    coopBoss[i].color = random(numColors);
    coopBoss[i].active = true;
  }

  distributeCoopBossPositions();
  coopLastBossMove = millis();

  for (int i = 0; i < MAX_SHOTS; i++) {
    allVsAllPlayer1Shots[i].position = -1;
    allVsAllPlayer2Shots[i].position = -1;
    allVsAllPrevP1Shots[i] = -1;
    allVsAllPrevP2Shots[i] = -1;
  }

  Serial.printf("[INFO] ALL-VS-ALL round %d: %d boss LEDs, area [%d..%d], speed %dms\n",
                coopRound, coopBossPartsThisRound, coopBossLeftEdge, coopBossRightEdge, coopBossSpeed);
}

void fireAllVsAllShotPlayer1(uint8_t color) {
  if (allVsAllRoundOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer1Shots[i].position == -1) {
      allVsAllPlayer1Shots[i].position = PLAYER_SIZE;
      allVsAllPlayer1Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      return;
    }
  }
}

void fireAllVsAllShotPlayer2(uint8_t color) {
  if (allVsAllRoundOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer2Shots[i].position == -1) {
      allVsAllPlayer2Shots[i].position = activeLedCount - PLAYER_SIZE - 1;
      allVsAllPlayer2Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      return;
    }
  }
}

void resolveAllVsAllShotVsBossCollisions() {
  // Correct color destroys visible boss part and consumes shot.
  // Wrong color passes through boss.
  for (int s = 0; s < MAX_SHOTS; s++) {
    if (allVsAllPlayer1Shots[s].position != -1) {
      int16_t hitIndex = findVisibleCoopBossIndexAtPosition(allVsAllPlayer1Shots[s].position);
      if (hitIndex >= 0 && allVsAllPlayer1Shots[s].color == coopBoss[hitIndex].color) {
        coopBoss[hitIndex].active = false;
        allVsAllPlayer1Shots[s].position = -1;
      }
    }

    if (allVsAllPlayer2Shots[s].position != -1) {
      int16_t hitIndex = findVisibleCoopBossIndexAtPosition(allVsAllPlayer2Shots[s].position);
      if (hitIndex >= 0 && allVsAllPlayer2Shots[s].color == coopBoss[hitIndex].color) {
        coopBoss[hitIndex].active = false;
        allVsAllPlayer2Shots[s].position = -1;
      }
    }
  }
}

void resolveAllVsAllShotVsShotCollisions() {
  // Only same-color shots cancel on collide/crossing.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer1Shots[i].position == -1) continue;

    for (int j = 0; j < MAX_SHOTS; j++) {
      if (allVsAllPlayer2Shots[j].position == -1) continue;

      bool samePosition = (allVsAllPlayer1Shots[i].position == allVsAllPlayer2Shots[j].position);
      bool crossed = (allVsAllPrevP1Shots[i] != -1 && allVsAllPrevP2Shots[j] != -1 &&
                      allVsAllPrevP1Shots[i] < allVsAllPrevP2Shots[j] &&
                      allVsAllPlayer1Shots[i].position > allVsAllPlayer2Shots[j].position);

      if ((samePosition || crossed) && (allVsAllPlayer1Shots[i].color == allVsAllPlayer2Shots[j].color)) {
        allVsAllPlayer1Shots[i].position = -1;
        allVsAllPlayer2Shots[j].position = -1;
        break;
      }
    }
  }
}

void updateAllVsAllGame() {
  if (allVsAllRoundOver) {
    if (millis() - allVsAllRoundOverStart >= COOP_ROUND_END_MS) {
      if (allVsAllPlayersWin) {
        coopRound++;
      } else {
        coopRound = 1;
      }
      allVsAllRoundOver = false;
      allVsAllPlayersWin = false;
      spawnAllVsAllRound();
    }
    return;
  }

  unsigned long now = millis();

  // Resolve current overlap before movement.
  resolveAllVsAllShotVsBossCollisions();

  if (now - coopLastBossMove >= coopBossSpeed) {
    coopBossLeftEdge--;
    coopBossRightEdge++;
    distributeCoopBossPositions();
    coopLastBossMove = now;

    // Boss moved onto shots.
    resolveAllVsAllShotVsBossCollisions();
  }

  if (now - lastShotMove >= SHOT_SPEED) {
    for (int i = 0; i < MAX_SHOTS; i++) {
      allVsAllPrevP1Shots[i] = allVsAllPlayer1Shots[i].position;
      allVsAllPrevP2Shots[i] = allVsAllPlayer2Shots[i].position;
    }

    for (int i = 0; i < MAX_SHOTS; i++) {
      if (allVsAllPlayer1Shots[i].position != -1) {
        allVsAllPlayer1Shots[i].position++;
        if (allVsAllPlayer1Shots[i].position >= activeLedCount) {
          allVsAllPlayer1Shots[i].position = -1;
        }
      }

      if (allVsAllPlayer2Shots[i].position != -1) {
        allVsAllPlayer2Shots[i].position--;
        if (allVsAllPlayer2Shots[i].position < 0) {
          allVsAllPlayer2Shots[i].position = -1;
        }
      }
    }

    resolveAllVsAllShotVsShotCollisions();
    resolveAllVsAllShotVsBossCollisions();

    lastShotMove = now;
  }

  // Player kill detection: shots that reach opponent home zone eliminate that player.
  bool player1Hit = false;
  bool player2Hit = false;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer1Shots[i].position >= activeLedCount - PLAYER_SIZE) {
      player2Hit = true;
    }
    if (allVsAllPlayer2Shots[i].position != -1 && allVsAllPlayer2Shots[i].position <= (PLAYER_SIZE - 1)) {
      player1Hit = true;
    }
  }

  if (player1Hit || player2Hit) {
    allVsAllRoundOver = true;
    allVsAllPlayersWin = false;
    allVsAllRoundOverStart = now;
    allVsAllLossAnimationPlayed = false;

    if (player1Hit && !player2Hit) {
      allVsAllWinner = 2;
      Serial.println("[ALL-VS-ALL] Player 2 wins (Player 1 hit)");
    } else if (player2Hit && !player1Hit) {
      allVsAllWinner = 1;
      Serial.println("[ALL-VS-ALL] Player 1 wins (Player 2 hit)");
    } else {
      allVsAllWinner = 0;
      Serial.println("[ALL-VS-ALL] Both players hit in same tick");
    }
    return;
  }

  // Boss reaches either player side -> both players lose immediately.
  for (int b = 0; b < coopBossPartsThisRound; b++) {
    if (!coopBoss[b].active) continue;
    if (coopBoss[b].position <= (PLAYER_SIZE - 1) || coopBoss[b].position >= (activeLedCount - PLAYER_SIZE)) {
      allVsAllRoundOver = true;
      allVsAllPlayersWin = false;
      allVsAllWinner = 0;
      allVsAllRoundOverStart = now;
      allVsAllLossAnimationPlayed = false;
      return;
    }
  }

  bool anyActive = false;
  for (int b = 0; b < coopBossPartsThisRound; b++) {
    if (coopBoss[b].active) {
      anyActive = true;
      break;
    }
  }

  if (!anyActive) {
    allVsAllRoundOver = true;
    allVsAllPlayersWin = true;
    allVsAllWinner = 0;
    allVsAllRoundOverStart = now;
  }
}

void renderAllVsAllGame() {
  FastLED.clear();

  for (int i = 0; i < PLAYER_SIZE; i++) {
    leds[i] = CRGB::White;
  }
  for (int i = activeLedCount - PLAYER_SIZE; i < activeLedCount; i++) {
    leds[i] = CRGB::White;
  }

  int16_t bestOuterRank[NUM_LEDS];
  for (int i = 0; i < activeLedCount; i++) {
    bestOuterRank[i] = 32767;
  }

  for (int i = 0; i < coopBossPartsThisRound; i++) {
    if (!coopBoss[i].active) continue;
    int16_t pos = coopBoss[i].position;
    if (pos < 0 || pos >= activeLedCount) continue;

    int16_t leftDepth = i;
    int16_t rightDepth = (coopBossPartsThisRound - 1) - i;
    int16_t outerRank = (leftDepth < rightDepth) ? leftDepth : rightDepth;

    if (outerRank <= bestOuterRank[pos]) {
      bestOuterRank[pos] = outerRank;
      leds[pos] = colorTable[coopBoss[i].color];
    }
  }

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer1Shots[i].position >= 0 && allVsAllPlayer1Shots[i].position < activeLedCount) {
      leds[allVsAllPlayer1Shots[i].position] = colorTable[allVsAllPlayer1Shots[i].color];
    }
  }

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (allVsAllPlayer2Shots[i].position >= 0 && allVsAllPlayer2Shots[i].position < activeLedCount) {
      leds[allVsAllPlayer2Shots[i].position] = colorTable[allVsAllPlayer2Shots[i].color];
    }
  }

  if (allVsAllRoundOver) {
    if (allVsAllPlayersWin) {
      unsigned long elapsed = millis() - allVsAllRoundOverStart;
      renderSharedWinSparkle(elapsed);
    } else if (allVsAllWinner == 1 || allVsAllWinner == 2) {
      uint8_t previousDuelWinner = duelWinner;
      unsigned long previousDuelGameOverStart = duelGameOverStart;

      duelWinner = allVsAllWinner;
      duelGameOverStart = allVsAllRoundOverStart;
      renderDuelEndAnimation();

      duelWinner = previousDuelWinner;
      duelGameOverStart = previousDuelGameOverStart;
    } else {
      if (!allVsAllLossAnimationPlayed) {
        playLifeLostAnimation();
        allVsAllLossAnimationPlayed = true;
      }
    }
  }
}

void initCoopMode() {
  gameState = STATE_PLAYING;
  coopRound = 1;
  coopRoundOver = false;
  coopRoundWon = false;
  coopRoundOverStart = 0;
  coopLossAnimationPlayed = false;

  for (int i = 0; i < MAX_SHOTS; i++) {
    coopPlayer1Shots[i].position = -1;
    coopPlayer2Shots[i].position = -1;
  }

  spawnCoopBossRound();

  Serial.println("[INFO] CO-PLAY mode started");
  Serial.println("[INFO] Boss expands from center in both directions");
}

void spawnCoopBossRound() {
  coopLossAnimationPlayed = false;

  coopBossPartsThisRound = coopRound * 2;
  if (coopBossPartsThisRound > MAX_COOP_BOSS_PARTS) {
    coopBossPartsThisRound = MAX_COOP_BOSS_PARTS;
  }

  coopSpacingThisRound = coopRound - 1;
  if (coopSpacingThisRound > 8) {
    coopSpacingThisRound = 8;
  }

  int32_t calculatedSpeed = COOP_BOSS_INITIAL_SPEED - ((coopRound - 1) * COOP_BOSS_SPEED_DECREASE);
  if (calculatedSpeed < COOP_BOSS_MIN_SPEED) {
    coopBossSpeed = COOP_BOSS_MIN_SPEED;
  } else {
    coopBossSpeed = (uint16_t)calculatedSpeed;
  }

  for (int i = 0; i < MAX_COOP_BOSS_PARTS; i++) {
    coopBoss[i].active = false;
    coopBoss[i].position = -1;
    coopBoss[i].color = 0;
  }

  int centerLeft = (activeLedCount - 1) / 2;
  int centerRight = activeLedCount / 2;
  coopBossLeftEdge = centerLeft;
  coopBossRightEdge = centerRight;

  // Initialize boss LEDs/colors. Positions are distributed across the current boss area.
  for (int i = 0; i < coopBossPartsThisRound; i++) {
    coopBoss[i].color = random(numColors);
    coopBoss[i].active = true;
  }

  distributeCoopBossPositions();
  coopLastBossMove = millis();

  for (int i = 0; i < MAX_SHOTS; i++) {
    coopPlayer1Shots[i].position = -1;
    coopPlayer2Shots[i].position = -1;
  }

  Serial.printf("[INFO] CO-PLAY round %d: %d boss LEDs, area [%d..%d], speed %dms\n",
                coopRound, coopBossPartsThisRound, coopBossLeftEdge, coopBossRightEdge, coopBossSpeed);
}

void distributeCoopBossPositions() {
  if (coopBossPartsThisRound == 0) {
    return;
  }

  if (coopBossPartsThisRound == 1) {
    coopBoss[0].position = (coopBossLeftEdge + coopBossRightEdge) / 2;
    return;
  }

  int32_t span = coopBossRightEdge - coopBossLeftEdge;
  int32_t divisor = coopBossPartsThisRound - 1;

  for (int i = 0; i < coopBossPartsThisRound; i++) {
    int32_t numerator = (int32_t)i * span;
    // Integer rounding to nearest position.
    int16_t pos = (int16_t)(coopBossLeftEdge + (numerator + divisor / 2) / divisor);
    coopBoss[i].position = pos;
  }
}

int16_t findVisibleCoopBossIndexAtPosition(int16_t position) {
  int16_t bestIndex = -1;
  int16_t bestOuterRank = 32767;

  for (int i = 0; i < coopBossPartsThisRound; i++) {
    if (!coopBoss[i].active) continue;
    if (coopBoss[i].position != position) continue;

    // Lower outerRank means more outer (wins visibility/targeting).
    int16_t leftDepth = i;
    int16_t rightDepth = (coopBossPartsThisRound - 1) - i;
    int16_t outerRank = (leftDepth < rightDepth) ? leftDepth : rightDepth;

    if (outerRank <= bestOuterRank) {
      bestOuterRank = outerRank;
      bestIndex = i;
    }
  }

  return bestIndex;
}

void fireCoopShotPlayer1(uint8_t color) {
  if (coopRoundOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (coopPlayer1Shots[i].position == -1) {
      coopPlayer1Shots[i].position = PLAYER_SIZE;
      coopPlayer1Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      return;
    }
  }
}

void fireCoopShotPlayer2(uint8_t color) {
  if (coopRoundOver) return;

  for (int i = 0; i < MAX_SHOTS; i++) {
    if (coopPlayer2Shots[i].position == -1) {
      coopPlayer2Shots[i].position = activeLedCount - PLAYER_SIZE - 1;
      coopPlayer2Shots[i].color = color;
      lastAnyShotFiredAt = millis();
      return;
    }
  }
}

void updateCoopGame() {
  if (coopRoundOver) {
    if (millis() - coopRoundOverStart >= COOP_ROUND_END_MS) {
      if (coopRoundWon) {
        coopRound++;
      } else {
        coopRound = 1;
      }
      coopRoundOver = false;
      coopRoundWon = false;
      spawnCoopBossRound();
    }
    return;
  }

  unsigned long now = millis();

  // Resolve collisions at current positions first so shots cannot pass through
  // boss parts when boss and shots move during the same frame.
  resolveCoopShotCollisions();

  if (now - coopLastBossMove >= coopBossSpeed) {
    // Expand boss area outward from center.
    coopBossLeftEdge--;
    coopBossRightEdge++;
    distributeCoopBossPositions();
    coopLastBossMove = now;

    // Boss moved: resolve immediate overlaps before shots move again.
    resolveCoopShotCollisions();
  }

  if (now - lastShotMove >= SHOT_SPEED) {
    for (int i = 0; i < MAX_SHOTS; i++) {
      if (coopPlayer1Shots[i].position != -1) {
        coopPlayer1Shots[i].position++;
        if (coopPlayer1Shots[i].position >= activeLedCount) {
          coopPlayer1Shots[i].position = -1;
        }
      }

      if (coopPlayer2Shots[i].position != -1) {
        coopPlayer2Shots[i].position--;
        if (coopPlayer2Shots[i].position < 0) {
          coopPlayer2Shots[i].position = -1;
        }
      }
    }
    lastShotMove = now;
  }

  // Resolve collisions again after movement.
  resolveCoopShotCollisions();

  // Lose if any active boss part reaches either player's home zone.
  for (int b = 0; b < coopBossPartsThisRound; b++) {
    if (!coopBoss[b].active) continue;
    if (coopBoss[b].position <= (PLAYER_SIZE - 1) || coopBoss[b].position >= (activeLedCount - PLAYER_SIZE)) {
      coopRoundOver = true;
      coopRoundWon = false;
      coopRoundOverStart = now;
      coopLossAnimationPlayed = false;
      return;
    }
  }

  // Win if all boss parts are inactive.
  bool anyActive = false;
  for (int b = 0; b < coopBossPartsThisRound; b++) {
    if (coopBoss[b].active) {
      anyActive = true;
      break;
    }
  }

  if (!anyActive) {
    coopRoundOver = true;
    coopRoundWon = true;
    coopRoundOverStart = now;
  }
}

void resolveCoopShotCollisions() {
  // Shots hit the currently visible boss LED at that position (outer-most wins).
  for (int s = 0; s < MAX_SHOTS; s++) {
    if (coopPlayer1Shots[s].position != -1) {
      int16_t hitIndex = findVisibleCoopBossIndexAtPosition(coopPlayer1Shots[s].position);
      if (hitIndex >= 0) {
        if (coopPlayer1Shots[s].color == coopBoss[hitIndex].color) {
          coopBoss[hitIndex].active = false;
        } else {
          coopBossSpeed = (coopBossSpeed * 80) / 100;
          if (coopBossSpeed < COOP_BOSS_MIN_SPEED) {
            coopBossSpeed = COOP_BOSS_MIN_SPEED;
          }
        }
        coopPlayer1Shots[s].position = -1;
      }
    }

    if (coopPlayer2Shots[s].position != -1) {
      int16_t hitIndex = findVisibleCoopBossIndexAtPosition(coopPlayer2Shots[s].position);
      if (hitIndex >= 0) {
        if (coopPlayer2Shots[s].color == coopBoss[hitIndex].color) {
          coopBoss[hitIndex].active = false;
        } else {
          coopBossSpeed = (coopBossSpeed * 80) / 100;
          if (coopBossSpeed < COOP_BOSS_MIN_SPEED) {
            coopBossSpeed = COOP_BOSS_MIN_SPEED;
          }
        }
        coopPlayer2Shots[s].position = -1;
      }
    }
  }
}

void renderCoopGame() {
  FastLED.clear();

  // Home zones for both players.
  for (int i = 0; i < PLAYER_SIZE; i++) {
    leds[i] = CRGB::White;
  }
  for (int i = activeLedCount - PLAYER_SIZE; i < activeLedCount; i++) {
    leds[i] = CRGB::White;
  }

  // Boss LEDs. If multiple overlap, inner are drawn first and outer-most color wins.
  int16_t bestOuterRank[NUM_LEDS];
  for (int i = 0; i < activeLedCount; i++) {
    bestOuterRank[i] = 32767;
  }

  for (int i = 0; i < coopBossPartsThisRound; i++) {
    if (!coopBoss[i].active) continue;
    int16_t pos = coopBoss[i].position;
    if (pos < 0 || pos >= activeLedCount) continue;

    int16_t leftDepth = i;
    int16_t rightDepth = (coopBossPartsThisRound - 1) - i;
    int16_t outerRank = (leftDepth < rightDepth) ? leftDepth : rightDepth;

    if (outerRank <= bestOuterRank[pos]) {
      bestOuterRank[pos] = outerRank;
      leds[pos] = colorTable[coopBoss[i].color];
    }
  }

  // Shots from player 1.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (coopPlayer1Shots[i].position >= 0 && coopPlayer1Shots[i].position < activeLedCount) {
      leds[coopPlayer1Shots[i].position] = colorTable[coopPlayer1Shots[i].color];
    }
  }

  // Shots from player 2.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (coopPlayer2Shots[i].position >= 0 && coopPlayer2Shots[i].position < activeLedCount) {
      leds[coopPlayer2Shots[i].position] = colorTable[coopPlayer2Shots[i].color];
    }
  }

  if (coopRoundOver) {
    if (coopRoundWon) {
      unsigned long elapsed = millis() - coopRoundOverStart;
      renderSharedWinSparkle(elapsed);
    } else {
      if (!coopLossAnimationPlayed) {
        playLifeLostAnimation();
        coopLossAnimationPlayed = true;
      }
    }
  }
}

// ============================================
// BUTTON LED CONTROL FUNCTIONS
// ============================================

// Get the next color to shoot for guidance.
// In Follow-Me mode, shots already in flight with the correct color are treated as success.
int8_t getNextColorToShoot() {
  uint8_t pendingShotsByColor[4] = {0, 0, 0, 0};

  // Count active shots by color so guidance can move ahead before collisions happen.
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (shots[i].position != -1 && shots[i].color < 4) {
      pendingShotsByColor[shots[i].color]++;
    }
  }

  // Walk boss from front to back and consume matching pending shots in order.
  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active) {
      uint8_t requiredColor = boss[i].color;

      if (requiredColor < 4 && pendingShotsByColor[requiredColor] > 0) {
        pendingShotsByColor[requiredColor]--;
        continue;
      }

      return requiredColor;
    }
  }
  return -1;  // No active boss parts
}

// Build and start memory playback sequence from active boss parts (front to back)
void startMemoryPlaybackSequence() {
  memorySequenceLength = 0;

  if (buttonLEDMode != LED_MODE_MEMORY || gameState != STATE_PLAYING) {
    memoryPlaybackActive = false;
    return;
  }

  for (int i = 0; i < MAX_BOSS_PARTS; i++) {
    if (boss[i].active && boss[i].color < numColors) {
      memorySequenceColors[memorySequenceLength++] = boss[i].color;
    }
  }

  if (memorySequenceLength == 0) {
    memoryPlaybackActive = false;
    return;
  }

  memoryPlaybackActive = true;
  memoryPlaybackLedOn = true;
  memoryPlaybackStep = 0;
  memoryPlaybackTimer = millis();

  Serial.printf("[INFO] MEMORY mode: Playing sequence with %d step(s)\n", memorySequenceLength);
}

// Update button LED states based on current mode
void updateButtonLEDs() {
  const uint8_t btnLEDPins[4] = {BTN_LED_RED_PIN, BTN_LED_GREEN_PIN, BTN_LED_BLUE_PIN, BTN_LED_WHITE_PIN};
  int buttonsToControl = (numColors == 4) ? 4 : 3;

  // During mode change indicator, keep button LEDs off.
  if (modeDotsActive) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(btnLEDPins[i], LOW);
    }
    return;
  }
  
  switch (buttonLEDMode) {
    case LED_MODE_INVERTED:
      // LEDs on when button NOT pressed, off when pressed
      for (int i = 0; i < buttonsToControl; i++) {
        bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
        digitalWrite(btnLEDPins[i], buttonPressed ? LOW : HIGH);
      }
      // Turn off unused button LED in 3-color mode
      if (numColors == 3) {
        digitalWrite(BTN_LED_WHITE_PIN, LOW);
      }
      break;
      
    case LED_MODE_FOLLOW_ME:
      // Light up the button that should be pressed next
      if (gameState == STATE_PLAYING) {
        int8_t nextColor = getNextColorToShoot();
        
        // Turn off all LEDs first
        for (int i = 0; i < 4; i++) {
          digitalWrite(btnLEDPins[i], LOW);
        }
        
        // Light up the next color to shoot (if valid and within active colors)
        if (nextColor >= 0 && nextColor < numColors) {
          digitalWrite(btnLEDPins[nextColor], HIGH);
        }
      } else {
        // Turn off all LEDs during animations
        for (int i = 0; i < 4; i++) {
          digitalWrite(btnLEDPins[i], LOW);
        }
      }
      break;
      
    case LED_MODE_MEMORY:
      // Memory mode: play sequence, then let user press from memory.
      for (int i = 0; i < 4; i++) {
        digitalWrite(btnLEDPins[i], LOW);
      }

      if (gameState != STATE_PLAYING) {
        memoryPlaybackActive = false;
        break;
      }

      if (memoryPlaybackActive && memoryPlaybackStep < memorySequenceLength) {
        unsigned long now = millis();
        uint8_t activeColor = memorySequenceColors[memoryPlaybackStep];

        if (memoryPlaybackLedOn) {
          if (activeColor < numColors) {
            digitalWrite(btnLEDPins[activeColor], HIGH);
          }

          if (now - memoryPlaybackTimer >= MEMORY_LED_ON_MS) {
            memoryPlaybackLedOn = false;
            memoryPlaybackTimer = now;
          }
        } else {
          if (now - memoryPlaybackTimer >= MEMORY_LED_OFF_MS) {
            memoryPlaybackStep++;

            if (memoryPlaybackStep >= memorySequenceLength) {
              memoryPlaybackActive = false;
              Serial.println("[INFO] MEMORY mode: Sequence complete, player input enabled");
            } else {
              memoryPlaybackLedOn = true;
              memoryPlaybackTimer = now;
            }
          }
        }
      }
      break;

    case LED_MODE_GHOST_BOSS:
      // In ghost mode, mirror Mode 1 behavior (on when not pressed).
      for (int i = 0; i < buttonsToControl; i++) {
        bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
        digitalWrite(btnLEDPins[i], buttonPressed ? LOW : HIGH);
      }
      if (numColors == 3) {
        digitalWrite(BTN_LED_WHITE_PIN, LOW);
      }
      break;

    case LED_MODE_DUEL:
      // In duel mode, mirror Mode 1 behavior for player 1 physical buttons.
      for (int i = 0; i < buttonsToControl; i++) {
        bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
        digitalWrite(btnLEDPins[i], buttonPressed ? LOW : HIGH);
      }
      if (numColors == 3) {
        digitalWrite(BTN_LED_WHITE_PIN, LOW);
      }
      break;

    case LED_MODE_COOP:
      // In co-play mode, mirror Mode 1 behavior for player 1 physical buttons.
      for (int i = 0; i < buttonsToControl; i++) {
        bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
        digitalWrite(btnLEDPins[i], buttonPressed ? LOW : HIGH);
      }
      if (numColors == 3) {
        digitalWrite(BTN_LED_WHITE_PIN, LOW);
      }
      break;

    case LED_MODE_ALL_VS_ALL:
      // In all-vs-all mode, mirror Mode 1 behavior for player 1 physical buttons.
      for (int i = 0; i < buttonsToControl; i++) {
        bool buttonPressed = (digitalRead(buttonPins[i]) == LOW);
        digitalWrite(btnLEDPins[i], buttonPressed ? LOW : HIGH);
      }
      if (numColors == 3) {
        digitalWrite(BTN_LED_WHITE_PIN, LOW);
      }
      break;

    case LED_MODE_PONG_DUEL: {
      bool player1Pressed = (digitalRead(BTN1_PIN) == LOW);
      bool player2Pressed = (digitalRead(BTN4_PIN) == LOW);

      digitalWrite(BTN_LED_GREEN_PIN, LOW);
      digitalWrite(BTN_LED_BLUE_PIN, LOW);

      if (pongPhase == PONG_PHASE_SERVE_COUNTDOWN) {
        bool pulseOn = (((millis() - pongPhaseStart) / 180) % 2) == 0;
        digitalWrite(BTN_LED_RED_PIN, pulseOn ? HIGH : LOW);
        digitalWrite(BTN_LED_WHITE_PIN, pulseOn ? HIGH : LOW);
      } else {
        digitalWrite(BTN_LED_RED_PIN, player1Pressed ? LOW : HIGH);
        digitalWrite(BTN_LED_WHITE_PIN, player2Pressed ? LOW : HIGH);
      }
      break;
    }
  }
}

void adjustSettingsLedCount(int16_t delta) {
  int32_t next = (int32_t)settingsSelectedLedCount + delta;
  bool clampedMin = false;
  bool clampedMax = false;
  if (next < SETTINGS_LED_COUNT_MIN) {
    next = SETTINGS_LED_COUNT_MIN;
    clampedMin = true;
  }
  if (next > NUM_LEDS) {
    next = NUM_LEDS;
    clampedMax = true;
  }

  if ((uint16_t)next != settingsSelectedLedCount) {
    settingsSelectedLedCount = (uint16_t)next;
    settingsLastInteraction = millis();
    Serial.printf("[SETTINGS] LED count -> %d\n", settingsSelectedLedCount);
  } else if (clampedMax) {
    Serial.printf("[SETTINGS] LED count at MAX (%d)\n", NUM_LEDS);
  } else if (clampedMin) {
    Serial.printf("[SETTINGS] LED count at MIN (%d)\n", SETTINGS_LED_COUNT_MIN);
  }
}

void blackOutInactiveLeds() {
  for (int i = activeLedCount; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
}
