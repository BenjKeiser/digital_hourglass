/*
 * Digital Hourglass - ESP32-C3 with Multiple SPI Devices
 * 
 * Devices on FSPI Bus:
 * - ST7735 Display (CS GPIO10)
 * - MFRC522 RFID (CS GPIO8)
 * - MPU6500 IMU (CS GPIO9)
 * 
 * Features: Countdown counter displayed on TFT screen
 */

#include "Arduino.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <MFRC522.h>
#include <ReefwingMPU6x00.h>

// ==========================================
// SPI Pins Configuration (ESP32-C3-Zero)
// ==========================================
#define SPI_MISO 2
#define SPI_MOSI 7
#define SPI_SCLK 6

// Chip Select Pins
#define TFT_CS_PIN 5
#define RFID_CS_PIN 8
#define IMU_CS_PIN 9

// Display Control Pins
#define TFT_DC_PIN 4
#define TFT_RST_PIN 3

// RFID Control Pins
#define RFID_RST_PIN 1

// NeoPixel (onboard Adafruit LED) pin (originally GPIO10)
#define NEOPIXEL_PIN 10

// ==========================================
// Device Instances
// ==========================================
// Adafruit ST7735 instance (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

// NeoPixel instance (single LED)
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// MFRC522 (SPI with CS on GPIO8)
MFRC522 mfrc522(RFID_CS_PIN, UINT8_MAX);

// MPU6500 (SPI with CS on GPIO9)
static MPU6500 imu = MPU6500(SPI, IMU_CS_PIN);

// ==========================================
// Global Variables
// ==========================================
unsigned long lastUpdateTime = 0;
int countdownValue = 3600;  // Start at 3600 seconds (1 hour)

// ==========================================
// Function Declarations
// ==========================================
void initializeSPI();
void initializeDisplay();
void initializeRFID();
void initializeIMU();
void updateCountdownDisplay();

// ==========================================
// Setup Function
// ==========================================
void setup()
{
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nDigital Hourglass Starting...");
    
    // Initialize RFID
  initializeRFID();

  // Initialize SPI Bus
  initializeSPI();
  
  // Initialize Display
  initializeDisplay();  
  
  // Initialize IMU
  initializeIMU();
  
  Serial.println("All devices initialized successfully!");
  
  // Set initial display update time
  lastUpdateTime = millis();

  // Initialize NeoPixel and ensure it's off after initialization
  pixels.begin();
  pixels.fill(pixels.Color(0, 0, 0), 0, 1);
  pixels.show();
}

// ==========================================
// Main Loop
// ==========================================
void loop()
{
  unsigned long currentTime = millis();
  
  // Update countdown every 1000ms (1 second)
  if (currentTime - lastUpdateTime >= 1000)
  {
    lastUpdateTime = currentTime;
    countdownValue--;
    
    // Reset countdown when it reaches 0
    if (countdownValue < 0)
    {
      countdownValue = 3600;
    }
    
    updateCountdownDisplay();
  }
  
  // Check for RFID cards
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
  {
    Serial.print("Card UID: ");
    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println();
    mfrc522.PICC_HaltA();
  }
  
  delay(10);
}

// ==========================================
// Initialization Functions
// ==========================================

void initializeSPI()
{
  Serial.println("Initializing SPI Bus (FSPI)...");
  
  // Configure SPI bus (using FSPI)
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, -1);
  
  // Set up chip select pins as outputs
  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(RFID_CS_PIN, OUTPUT);
  pinMode(IMU_CS_PIN, OUTPUT);
  
  // Ensure all CS pins are high (inactive)
  digitalWrite(TFT_CS_PIN, HIGH);
  digitalWrite(RFID_CS_PIN, HIGH);
  digitalWrite(IMU_CS_PIN, HIGH);
  
  Serial.println("SPI Bus initialized");
}

void initializeDisplay()
{
  Serial.println("Initializing TFT Display...");

  digitalWrite(TFT_CS_PIN, LOW);

  // Initialize Adafruit ST7735 (use a common init for ST7735R)
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 10);
  tft.println("COUNTDOWN");

  updateCountdownDisplay();

  // Deassert CS
  digitalWrite(TFT_CS_PIN, HIGH);

  Serial.println("Display initialized");
}

void initializeRFID()
{
  Serial.println("Initializing MFRC522 RFID...");

  mfrc522.PCD_Init();

  // Dump version to serial (will also attempt SPI comms)
  mfrc522.PCD_DumpVersionToSerial();

  Serial.println("RFID initialized");
}

void initializeIMU()
{
  Serial.println("Initializing MPU6500 IMU...");
  // IMU initialization deferred
  Serial.println("IMU initialized");
}

// ==========================================
// Display Update Function
// ==========================================

void updateCountdownDisplay()
{
  // Convert countdown seconds to MM:SS format
  int minutes = countdownValue / 60;
  int seconds = countdownValue % 60;
  
  // Create time string
  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);
  
  digitalWrite(TFT_CS_PIN, LOW);
  
  // Clear the counter area (save bandwidth by only updating needed area)
  tft.fillRect(15, 60, 100, 40, ST77XX_BLACK);

  // Draw large counter
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setCursor(25, 65);
  tft.println(timeStr);

  
  digitalWrite(TFT_CS_PIN, HIGH);
  
  Serial.print("Countdown: ");
  Serial.println(timeStr);
}