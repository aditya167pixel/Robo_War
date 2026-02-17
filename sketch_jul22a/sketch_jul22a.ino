#include <GPxMatrix.h>
#include <WiFi.h>
#include <esp_now.h>
#include "ArcadeSumo.h" // Include the custom font header file

// Define pins for the matrix
#define P_A    23
#define P_B    22
#define P_C    5
#define P_D    17
#define P_E    32
#define P_CLK  16
#define P_LAT  4
#define P_OE   15

GPxMatrix matrix(P_A, P_B, P_C, P_D, P_E, P_CLK, P_LAT, P_OE, false, 128);

void setup() {
  Serial.begin(115200);

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register receive callback function
  esp_now_register_recv_cb(OnDataRecv);

  // Initialize the matrix display
  matrix.begin();

  // Display something on the matrix with Arcade Classic font
  displayMessage("ROBO");

  // Draw border around the edges
  drawBorder();
}

void loop() {
  // No need for code in the loop for this simple example
}

// Function to display a message on the matrix using Arcade Classic font
void displayMessage(const char *message) {
  matrix.fillScreen(0); // Clear the screen with black color
  matrix.setTextSize(1); // Set text size (typically 1 for custom fonts)
  matrix.setFont(&Crackman20pt7b); // Set custom Arcade Classic font Crackman20pt7b
  matrix.setTextColor(matrix.Color888(255, 255, 255)); // White text
  matrix.setCursor(26, 26);
  matrix.print("ROBO");
  matrix.setTextSize(1); // Set text size (typically 1 for custom fonts)
  matrix.setFont(&Crackman20pt7b); // Set custom Arcade Classic font Crackman20pt7b
  matrix.setTextColor(matrix.Color888(255, 255, 255)); // White text
  matrix.setCursor(26, 53);
  matrix.print("SUMO");
}

// Function to draw a border around the edges of the matrix
void drawBorder() {
  uint16_t borderColor = matrix.Color888(0, 255, 255); // Color of the border (red)

  // Draw the top border
  matrix.drawLine(0, 0, 127, 0, borderColor);

  // Draw the bottom border
  matrix.drawLine(0, 63, 127, 63, borderColor);

  // Draw the left border
  matrix.drawLine(0, 0, 0, 63, borderColor);

  // Draw the right border
  matrix.drawLine(127, 0, 127, 63, borderColor);

//  matrix.display(); // Update the display to show the border
}

// ESP-NOW receive callback function (not used in this simplified example)
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  // Implement your logic here if needed
}


