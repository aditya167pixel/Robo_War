#include <GPxMatrix.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Ticker.h>
#include "ArcadeSumo.h"

#define P_A    23
#define P_B    22
#define P_C    5
#define P_D    17
#define P_E    32
#define P_CLK  16
#define P_LAT  4
#define P_OE   15

// FIX: Verify this MAC address matches your main ESP
uint8_t hostMACAddress[] = { 0x14, 0x33, 0x5C, 0x03, 0x00, 0xA4 };

int score_1 = 0;
int score_2 = 0;
int minutes = 3;
int seconds = 0;

bool paused = false;
bool timerFinished = false;
bool gameStarted = false;
bool screenStarted = false; 
bool winnerScreen = false; 
unsigned long pauseStartTime = 0;
unsigned long screenBlankStartTime = 0;
bool screenBlank = false;

// --- VOLATILE FLAGS FOR SAFE CALLBACK PROCESSING ---
volatile char incomingCommand = 0;
volatile bool tickOccurred = false;

GPxMatrix matrix(P_A, P_B, P_C, P_D, P_E, P_CLK, P_LAT, P_OE, false, 128);
Ticker timerTicker;
int blinkInterval = 500; 

// Function Prototypes
void displayIdleScreen();
void displayWelcomeScreen();
void displayCountdown(int number, int blinkInterval);
void startGame();
void decrementTimer();
void updateScores();
void updateTime();
void displayWinner();
void clearScreen();
void clearScreen1();
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);

// Safe ticker callback
void onTick() {
  tickOccurred = true;
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("\n\n=================================");
  Serial.println("SCREEN ESP STARTING...");
  Serial.println("=================================");

  // FIXED: Use WiFi.macAddress() instead of esp_read_mac for core 3.3.7
  Serial.println("\n=== MAC ADDRESSES ===");
  Serial.print("This Screen ESP MAC: ");
  Serial.println(WiFi.macAddress());
  
  Serial.print("Target Main ESP MAC: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", 
                hostMACAddress[0], hostMACAddress[1], hostMACAddress[2], 
                hostMACAddress[3], hostMACAddress[4], hostMACAddress[5]);
  Serial.println("=====================\n");

  // Initialize WiFi explicitly to channel 1
  Serial.println("Initializing WiFi...");
  WiFi.mode(WIFI_STA);
  
  // Set WiFi channel
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  Serial.println("WiFi Channel set to 1");
  
  WiFi.disconnect();
  delay(100);

  Serial.println("Initializing ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW initialization failed!");
    return;
  }
  Serial.println("ESP-NOW initialized successfully");
  
  // Register receive callback - this signature is correct for core 3.3.7
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW receive callback registered");

  Serial.println("Initializing Matrix...");
  matrix.begin();
  displayIdleScreen();
  Serial.println("Matrix initialized - ROBOWARS displayed");
  
  timerTicker.attach(1, onTick); 
  Serial.println("Timer ticker attached (1 second)");
  
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, hostMACAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  Serial.print("Adding peer with MAC: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", 
                hostMACAddress[0], hostMACAddress[1], hostMACAddress[2], 
                hostMACAddress[3], hostMACAddress[4], hostMACAddress[5]);

  esp_err_t add_result = esp_now_add_peer(&peerInfo);
  if (add_result == ESP_OK) {
    Serial.println("✓ Peer added successfully");
    
    // Verify peer was added
    if (esp_now_is_peer_exist(hostMACAddress)) {
      Serial.println("✓ Peer verified in ESP-NOW peer list");
    } else {
      Serial.println("✗ ERROR: Peer not found after add!");
    }
  } else {
    Serial.print("✗ Failed to add peer. Error code: ");
    Serial.println(add_result);
  }
  
  Serial.println("\n=== SCREEN READY ===");
  Serial.println("Waiting for commands from Main ESP...");
  Serial.println("Commands: '3'=Start, '1'=P1 Goal, '2'=P2 Goal\n");
}

void loop() {
  // Periodic status update (every 30 seconds to avoid spam)
  static unsigned long lastSerialTest = 0;
  if (millis() - lastSerialTest > 30000) {
    lastSerialTest = millis();
    Serial.println("Status: Screen loop running, waiting for commands...");
  }

  // 1. Check for Timer Tick Flags
  if (tickOccurred) {
    tickOccurred = false;
    decrementTimer();
  }

  // 2. Process ESP-NOW Commands Safely
  if (incomingCommand != 0) {
    char cmd = incomingCommand;
    incomingCommand = 0; // Reset flag immediately
    
    // Print received command with timestamp and state
    Serial.print("\n[");
    Serial.print(millis() / 1000);
    Serial.print("s] >>> RECEIVED COMMAND: '");
    Serial.print(cmd);
    Serial.print("' | State: screenStarted=");
    Serial.print(screenStarted);
    Serial.print(", gameStarted=");
    Serial.print(gameStarted);
    Serial.print(", paused=");
    Serial.print(paused);
    Serial.print(", timerFinished=");
    Serial.print(timerFinished);
    Serial.print(", winnerScreen=");
    Serial.println(winnerScreen);

    // Handle '3' command - Start game
    if (cmd == '3') {
      if (!screenStarted) {
        Serial.println("ACTION: Starting game (first '3' command)");
        screenStarted = true;
        gameStarted = true; // Make sure gameStarted is set
        displayWelcomeScreen(); 
        startGame(); 
        Serial.println("Game started - countdown beginning");
      } else {
        Serial.println("Game already started, ignoring duplicate '3' command");
      }
    }
    // Handle '1' and '2' commands - Goals
    else if (cmd == '1' || cmd == '2') {
      if (winnerScreen) {
        // If game is over, treat as reset command
        Serial.println("ACTION: Game over, returning to idle screen");
        clearScreen1();
        screenStarted = false;
        gameStarted = false;
        winnerScreen = false;
        paused = false;
        timerFinished = false;
        minutes = 3;
        seconds = 0;
        score_1 = 0;
        score_2 = 0;
        displayIdleScreen(); 
      }
      else if (gameStarted && !paused && !timerFinished) {
        // Goal scored during active game
        Serial.print("ACTION: GOAL scored by Player ");
        Serial.println(cmd);
        
        timerTicker.detach();
        Serial.println("Timer paused");
        
        paused = true;
        pauseStartTime = millis();
        
        clearScreen(); 
        screenBlank = true;
        matrix.setFont(&Crackman20pt7b);
        matrix.setCursor(26, 40);
        matrix.setTextSize(1);
        matrix.setTextColor(matrix.Color888(0, 255, 0));
        matrix.print("GOAL");
        matrix.setFont(nullptr); 

        screenBlankStartTime = millis(); 

        if (cmd == '1') {
          score_1++;
        }
        else if (cmd == '2') {
          score_2++;
        }
        
        Serial.print("Score - Player 1: ");
        Serial.print(score_1);
        Serial.print(", Player 2: ");
        Serial.println(score_2);
        Serial.println("Displaying GOAL for 10 seconds");
      } else {
        Serial.println("WARNING: Goal command ignored (invalid game state)");
      }
    } else {
      Serial.print("WARNING: Unknown command '");
      Serial.print(cmd);
      Serial.println("' received");
    }
  }

  // 3. Handle Timers & States
  if (timerFinished && !winnerScreen) {
    Serial.println("EVENT: Timer finished - game over");
    winnerScreen = true;
    displayWinner();
    timerTicker.detach(); 
  }

  if (paused) {
    if (millis() - pauseStartTime >= 10000) { 
      Serial.println("EVENT: Goal pause ended - resuming game");
      paused = false;
      timerTicker.attach(1, onTick); 
      updateScores(); // Refresh display
    }
    return; 
  }

  if (screenBlank) {
    if (millis() - screenBlankStartTime >= 6000) { 
      Serial.println("EVENT: GOAL display ended - returning to timer");
      screenBlank = false;
      updateScores(); 
      timerTicker.attach(1, onTick); 
    }
    return; 
  }
  
  delay(10);
}

// FAST CALLBACK - Only updates a flag - CORRECT signature for core 3.3.7
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len > 0) {
    incomingCommand = incomingData[0];
    // No Serial.print here - it's unsafe in callback
  }
}

void clearScreen() {
  matrix.fillScreen(0); 
}

void clearScreen1() {
  matrix.fillScreen(0); 
  matrix.setFont(&Crackman20pt7b);
  matrix.setCursor(26, 40);
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color888(0, 255, 0));
  matrix.print("GOAL");
  matrix.setFont(nullptr); 
}

void displayIdleScreen() {
  clearScreen();
  matrix.setFont(&Crackman20pt7b);
  matrix.setTextColor(matrix.Color888(255, 255, 255));
  matrix.setTextSize(1);
  matrix.setCursor(9, 34);
  matrix.print("ROBOWARS");
  matrix.setFont(nullptr); 
  Serial.println("Display updated: ROBOWARS");
}

void displayWelcomeScreen() {
  clearScreen();
  matrix.setFont(&Crackman20pt7b);
  matrix.setTextSize(1);
  matrix.setCursor(9, 34);
  matrix.setTextColor(matrix.Color888(255, 255, 255));
  matrix.print("GET READY");
  Serial.println("Display updated: GET READY");
  delay(3000);
  clearScreen();
  matrix.setFont(nullptr); 

  Serial.println("Starting countdown sequence...");
  displayCountdown(3, 500);  
  displayCountdown(2, 250);  
  displayCountdown(1, 100);  
  displayCountdown(0, 100);  
  Serial.println("Countdown complete - game started");
}

void displayCountdown(int number, int blinkInterval) {
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    matrix.setTextSize(6);
    matrix.setCursor(50, 12);
    if (number > 0) {
      matrix.print(number);
    } else {
      matrix.setCursor(32, 12);
      matrix.print("GO");
    }
    delay(blinkInterval);
    clearScreen(); 
    delay(blinkInterval);
  }
  if (number > 0) {
    Serial.print("Countdown: ");
    Serial.println(number);
  } else {
    Serial.println("Countdown: GO");
  }
}

void startGame() {
  gameStarted = true;
  updateScores();
  updateTime();
  Serial.println("Game started - timer running");
}

void decrementTimer() {
  if (!paused && gameStarted) {
    if (seconds == 0) {
      if (minutes == 0) {
        timerFinished = true;
        Serial.println("Timer reached zero");
        return;
      } else {
        minutes--;
        seconds = 59;
      }
    } else {
      seconds--;
    }
    updateTime();
    
    // Print timer every 10 seconds to avoid spam
    static int lastPrintSec = -1;
    if (seconds % 10 == 0 && seconds != lastPrintSec) {
      lastPrintSec = seconds;
      Serial.print("Timer: ");
      Serial.print(minutes);
      Serial.print(":");
      if (seconds < 10) Serial.print("0");
      Serial.println(seconds);
    }
  }
}

void updateScores() {
  updateTime();
}

void updateTime() {
  matrix.fillRect(20, 20, 100, 100, matrix.Color888(0, 0, 0)); 
  matrix.setTextSize(3);
  matrix.setCursor(20, 20);

  if (minutes == 0 && seconds < 10) {
    matrix.setTextColor(matrix.Color888(255, 0, 0));  
  } else {
    matrix.setTextColor(matrix.Color888(255, 255, 255));  
  }

  if (minutes < 10) { matrix.print("0"); }
  matrix.print(minutes);
  matrix.print(":");

  if (seconds < 10) { matrix.print("0"); }
  matrix.print(seconds);
}

void displayWinner() {
  matrix.fillScreen(0); 
  matrix.setFont(&Crackman20pt7b);
  matrix.setTextColor(matrix.Color888(255, 255, 255));
  matrix.setCursor(6, 35);
  matrix.setTextSize(1);
  matrix.print("GAME OVER!");
  Serial.println("Display updated: GAME OVER!");
  
  unsigned long waitTime = millis();
  while(millis() - waitTime < 5000) { 
    yield(); 
  }

  Serial.println("Sending '6' to Main ESP...");
  const char *message = "6";
  esp_err_t result = esp_now_send(hostMACAddress, (uint8_t *)message, strlen(message));
  if (result == ESP_OK) {
    Serial.println("✓ Message sent successfully");
  } else {
    Serial.println("✗ Error sending message");
  }

  Serial.println("Restarting Screen ESP...");
  delay(100);
  ESP.restart();
}