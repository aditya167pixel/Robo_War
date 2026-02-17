#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

#define PIN 4
#define PIN1 5

bool flag1 = false;
bool flag2 = false;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(69, PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip1 = Adafruit_NeoPixel(69, PIN1, NEO_GRB + NEO_KHZ800);

#define I2C_SLAVE_ADDR 0x04


// --- VOLATILE FLAGS FOR SAFE ISR/CALLBACK PROCESSING ---
volatile char receivedChar = 0;
volatile bool newDataReceived = false;
volatile char espNowCommand = 0;

// FIX: Use different pins for SoftwareSerial to avoid conflicts
const int RX_PIN = 18;
const int TX_PIN = 19;
#define BUTTON_1_PIN 32
#define BUTTON_2_PIN 33
#define BUTTON_3_PIN 35
#define DEBOUNCE_DELAY 50

#define RELAY_PIN_1 15
#define RELAY_PIN_2 13

// FIX: Create SoftwareSerial objects with proper initialization
SoftwareSerial seeedSerial;
SoftwareSerial dfPlayerSerial;

DFRobotDFPlayerMini myDFPlayer;

// FIX: Verify this MAC address - it should match your screen ESP
uint8_t peerAddress[] = { 0xC8, 0xC9, 0xA3, 0xC7, 0x89, 0x30 };

bool relay1Active = false;
bool relay2Active = false;
bool button3Pressed = false;
bool button1Pressed = false;
bool button2Pressed = false;

bool initialButton1State = HIGH;
bool initialButton2State = HIGH;
bool initialButton3State = HIGH;

bool song1Playing = false;
bool song2Playing = false;
bool song3Playing = false;
bool song4Playing = false;
bool song5Playing = false;
bool song6Playing = false;
bool playing = false;

unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
unsigned long lastDebounceTime3 = 0;

// Function prototypes
void receiveEvent(int bytes);
void sendCommand(char command);
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void resetState();
void p1win();
void p2win();
void draw();
void setSolidColor(uint32_t color);
void setTwoColors(uint32_t color1, uint32_t color2);
void blinkTwoColors(uint16_t delayTime);
void halfredhalfgreen1(uint16_t delayTime);
void halfredhalfgreen2(uint16_t delayTime);
void steadyGreenAndBlinkRed(uint16_t delayTime);
void blinkRedAndSteadyGreen(uint16_t delayTime);
void rainbowCycle(uint8_t wait);
void rainbowCycle2(uint8_t wait);
uint32_t Wheel(byte WheelPos);

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("\n\nESP32 Main Starting...");
  Serial.flush();
  
  // Add MAC address discovery for debugging
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("Main ESP MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("Screen ESP should have this as hostMACAddress");
  
  strip.begin();
  strip1.begin();
  strip.setBrightness(255);
  strip1.setBrightness(255);
  strip.show();
  strip1.show();

  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
  
  // Initialize SoftwareSerial with pins
  dfPlayerSerial.begin(9600, SWSERIAL_8N1, 16, 17);
  delay(100);
  
  Serial.println("Initializing DFPlayer...");
  if (!myDFPlayer.begin(dfPlayerSerial)) {
    Serial.println("DFPlayer Mini failed to initialize!");
    Serial.println("Check connections and SD card");
  } else {
    Serial.println("DFPlayer Mini online.");
    myDFPlayer.volume(30);
    myDFPlayer.play(5);
  }

  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);
  pinMode(BUTTON_3_PIN, INPUT_PULLUP);

  delay(50);
  initialButton1State = digitalRead(BUTTON_1_PIN);
  initialButton2State = digitalRead(BUTTON_2_PIN);
  initialButton3State = digitalRead(BUTTON_3_PIN);

  Serial.println("Initializing WiFi...");
  WiFi.mode(WIFI_STA);
  
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  WiFi.disconnect();
  delay(100);

  Serial.println("Initializing ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
  } else {
    Serial.println("Peer added successfully");
  }

  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);

  digitalWrite(RELAY_PIN_1, HIGH);
  digitalWrite(RELAY_PIN_2, HIGH);
  
  Serial.println("Setup complete!");
}

void loop() {
  static unsigned long lastSerialTest = 0;
  if (millis() - lastSerialTest > 5000) {
    lastSerialTest = millis();
    Serial.println("Loop running...");
  }

  // 1. Check for incoming ESP-NOW Win/Reset conditions safely
  if (espNowCommand != 0) {
    char cmd = espNowCommand;
    espNowCommand = 0;
    Serial.print("Received ESP-NOW command: ");
    Serial.println(cmd);
    
    if (cmd == '6') { resetState(); } 
    else if (cmd == '7') { playing = false; draw(); } 
    else if (cmd == '8') { playing = false; p1win(); } 
    else if (cmd == '9') { playing = false; p2win(); }
  }

  // 2. Check for incoming I2C data
  if (newDataReceived) {
    char command = receivedChar;
    newDataReceived = false;
    
    Serial.print("Received I2C command: ");
    Serial.println(command);
    
    if (command == '1' || command == '2' || command == '3') {
      sendCommand(command);
      
      if (!song1Playing && !playing) {
        myDFPlayer.play(3);
        song1Playing = true;
        song2Playing = song3Playing = false;
        Serial.println("Playing song 3");
      }
      
      if (playing) {
        if (command == '1') {
          digitalWrite(RELAY_PIN_1, HIGH);
          myDFPlayer.pause();
          myDFPlayer.play(7);
          song4Playing = true;
          song2Playing = song3Playing = song1Playing = song5Playing = song6Playing = false;
          playing = false;
          strip.fill(strip.Color(255, 0, 0), 0, 52);
          strip.fill(strip.Color(0, 255, 0), 52, 53);
          strip.show();
          strip1.fill(strip.Color(255, 0, 0), 0, 52);
          strip1.fill(strip.Color(0, 255, 0), 52, 53);
          strip1.show();
          delay(10000);

          if (song4Playing) {
            myDFPlayer.pause();
            myDFPlayer.play(9);
            song2Playing = true;
            song5Playing = song3Playing = song1Playing = song4Playing = song6Playing = false;
          }
          digitalWrite(RELAY_PIN_1, LOW);
          rainbowCycle2(20);
          playing = true;
        } else if (command == '2') {
          digitalWrite(RELAY_PIN_2, HIGH);
          myDFPlayer.pause();
          myDFPlayer.play(7);
          song4Playing = true;
          song2Playing = song3Playing = song1Playing = song5Playing = song6Playing = false;
          playing = false;
          strip.fill(strip.Color(255, 0, 0), 0, 52);
          strip.fill(strip.Color(0, 0, 255), 52, 53);
          strip.show();
          strip1.fill(strip.Color(255, 0, 0), 0, 52);
          strip1.fill(strip.Color(0, 0, 255), 52, 53);
          strip1.show();
          delay(10000);

          if (song4Playing) {
            myDFPlayer.pause();
            myDFPlayer.play(9);
            song2Playing = true;
            song5Playing = song3Playing = song1Playing = song4Playing = song6Playing = false;
          }
          digitalWrite(RELAY_PIN_2, LOW);
          rainbowCycle2(20);
          playing = true;
        }
      }
    }
  }

  // Button 3 handling with debounce
  bool currentButton3State = digitalRead(BUTTON_3_PIN);
  if (currentButton3State == LOW && !button3Pressed) {
    if ((millis() - lastDebounceTime3) > DEBOUNCE_DELAY) {
      lastDebounceTime3 = millis();
      if (digitalRead(BUTTON_3_PIN) == LOW) {
        button3Pressed = true;
        Serial.println("Button 3 pressed. Activating Button 1 and Button 2.");
        setSolidColor(strip1.Color(255, 0, 0));
        myDFPlayer.pause();
        song1Playing = song2Playing = song3Playing = false;
      }
    }
  }

  // Button 1 handling with debounce
  bool currentButton1State = digitalRead(BUTTON_1_PIN);
  if (button3Pressed && currentButton1State == LOW && !button1Pressed) {
    if ((millis() - lastDebounceTime1) > DEBOUNCE_DELAY) {
      lastDebounceTime1 = millis();
      if (digitalRead(BUTTON_1_PIN) == LOW) {
        button1Pressed = true;
        Serial.println("Button 1 pressed.");
        strip.fill(strip.Color(0, 0, 255), 57, 12);
        strip.fill(strip.Color(0, 0, 255), 0, 23);
        strip.show();
        strip1.fill(strip.Color(0, 0, 255), 57, 12);
        strip1.fill(strip.Color(0, 0, 255), 0, 23);
        strip1.show();
      }
    }
  }

  // Button 2 handling with debounce
  bool currentButton2State = digitalRead(BUTTON_2_PIN);
  if (button3Pressed && currentButton2State == LOW && !button2Pressed) {
    if ((millis() - lastDebounceTime2) > DEBOUNCE_DELAY) {
      lastDebounceTime2 = millis();
      if (digitalRead(BUTTON_2_PIN) == LOW) {
        button2Pressed = true;
        Serial.println("Button 2 pressed.");
        strip.fill(strip.Color(0, 0, 255), 23, 32);
        strip.show();
        strip1.fill(strip.Color(0, 0, 255), 23, 32);
        strip1.show();
      }
    }
  }

  // Both buttons pressed handling
  if (button1Pressed && button2Pressed) {
    Serial.println("Button 1 and Button 2 both pressed. Sending command 3.");
    sendCommand('3');

    strip.fill(strip.Color(0, 255, 0), 0, 52);
    strip.fill(strip.Color(0, 255, 0), 52, 53);
    strip.show();
    strip1.fill(strip.Color(0, 255, 0), 0, 52);
    strip1.fill(strip.Color(0, 255, 0), 52, 53);
    strip1.show();
    
    myDFPlayer.play(1);
    delay(3500);
    
    song3Playing = true;
    song1Playing = song2Playing = false;
    
    for (int i = 0; i < 3; i++) {
      strip.fill(strip.Color(0, 255, 0), 0, 52);
      strip.fill(strip.Color(0, 255, 0), 52, 53);
      strip.show();
      strip1.fill(strip.Color(0, 255, 0), 0, 52);
      strip1.fill(strip.Color(0, 255, 0), 52, 53);
      strip1.show();
      delay(500);
      
      strip.fill(strip.Color(0, 0, 0), 0, 52);
      strip.fill(strip.Color(0, 0, 0), 52, 53);
      strip.show();
      strip1.fill(strip.Color(0, 0, 0), 0, 52);
      strip1.fill(strip.Color(0, 0, 0), 52, 53);
      strip1.show();
      delay(500);
    }
    
    strip.fill(strip.Color(0, 255, 0), 0, 52);
    strip.fill(strip.Color(0, 255, 0), 52, 53);
    strip.show();
    strip1.fill(strip.Color(0, 255, 0), 0, 52);
    strip1.fill(strip.Color(0, 255, 0), 52, 53);
    strip1.show();
    delay(1000);
    
    digitalWrite(RELAY_PIN_1, LOW);
    digitalWrite(RELAY_PIN_2, LOW);
    playing = true;

    rainbowCycle2(20);

    if (song3Playing) {
      myDFPlayer.play(3);
      song2Playing = true;
      song1Playing = song3Playing = false;
    }

    button1Pressed = false;
    button2Pressed = false;
    button3Pressed = false;
  }
  
  delay(10);
}

void receiveEvent(int bytes) {
  while (Wire.available()) {
    receivedChar = Wire.read();
    newDataReceived = true;
    // Serial.print removed from I2C callback - can cause issues in interrupt context
  }
}

void sendCommand(char command) {
  Serial.print("Sending ESP-NOW command: ");
  Serial.println(command);
  esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&command, sizeof(command));
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.println("Error sending the data");
  }
}

// CRITICAL FIX: REMOVED Serial.println from callback
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len > 0) {
    espNowCommand = incomingData[0];
    // Serial.print REMOVED - NEVER use Serial in callbacks!
  }
}

void resetState() {
  Serial.println("Resetting...");
  delay(100);
  ESP.restart();
}

void p1win() {
  Serial.println("Player 1 wins!");
  setSolidColor(strip.Color(0, 0, 255));
  myDFPlayer.pause();
  myDFPlayer.play(11);
  blinkTwoColors(100);
}

void p2win() {
  Serial.println("Player 2 wins!");
  setSolidColor(strip.Color(0, 0, 255));
  myDFPlayer.pause();
  myDFPlayer.play(11);
  blinkTwoColors(100);
}

void draw() {
  Serial.println("Draw!");
  myDFPlayer.pause();
  myDFPlayer.play(5);
  setSolidColor(strip.Color(255, 255, 255));
}

// All your existing functions remain exactly the same below this line
void blinkTwoColors(uint16_t delayTime) {
  setSolidColor(strip.Color(0, 0, 255));
  delay(1000);
  
  uint32_t blue = strip.Color(0, 0, 255);
  uint32_t black = strip.Color(0, 0, 0);
  
  for (uint16_t i = 0; i < 20; i++) {
    setTwoColors(blue, blue);
    delay(delayTime);
    setSolidColor(black);
    delay(delayTime);
  }
}

void setTwoColors(uint32_t color1, uint32_t color2) {
  strip.fill(color1, 0, 52);
  strip.fill(color2, 52, 53);
  strip.show();
  strip1.fill(color1, 0, 52);
  strip1.fill(color2, 52, 53);
  strip1.show();
}

void setSolidColor(uint32_t color) {
  strip.fill(color, 0, strip.numPixels());
  strip1.fill(color, 0, strip1.numPixels());
  strip.show();
  strip1.show();
}

void rainbowCycle(uint8_t wait) {
  unsigned long startTime = millis();
  unsigned long duration = 30000; 
  
  while (millis() - startTime < duration) {
    for (uint16_t j = 0; j < 256 * 5; j++) {
      for (uint16_t i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
        strip1.setPixelColor(i, Wheel(((i * 256 / strip1.numPixels()) + j) & 255));
      }
      strip.show();
      strip1.show();
      delay(wait);
      
      if (digitalRead(BUTTON_3_PIN) == LOW) {
        delay(DEBOUNCE_DELAY);
        if (digitalRead(BUTTON_3_PIN) == LOW) {
          return;
        }
      }
      
      if (newDataReceived) {
        char command = receivedChar;
        if (command == '1' || command == '2' || command == '3') {
          myDFPlayer.pause();
          return;
        }
        newDataReceived = false;
      }
    }
  }
}

void rainbowCycle2(uint8_t wait) {
  myDFPlayer.play(3);
  song2Playing = true;
  song1Playing = song3Playing = false;
  
  for (uint16_t j = 0; j < 256 * 5; j++) {
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
      strip1.setPixelColor(i, Wheel(((i * 256 / strip1.numPixels()) + j) & 255));
    }
    strip.show();
    strip1.show();
    delay(wait);
    
    if (newDataReceived) {
      char command = receivedChar;
      if (command == '1' || command == '2' || command == '3') {
        myDFPlayer.pause();
        return;
      }
      newDataReceived = false;
    }
  }
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}