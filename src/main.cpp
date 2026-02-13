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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <MFRC522.h>
#include <MPU6500_WE.h>

// Mainly for time synchronization
#include <WiFi.h>
#include <ezTime.h>

// Persistant storage
#include <Preferences.h>

//WIFI_SSID, SSID_PSK
#include "secrets.h"

// ==========================================
// Pins Configuration (ESP32-C3-Zero)
// ==========================================
#define SPI_MISO 2
#define SPI_MOSI 7
#define SPI_SCLK 6

// Chip Select Pins
#define TFT_CS_PIN 5
#define RFID_CS_PIN 0

// Display Control Pins
#define TFT_DC_PIN 4
#define TFT_RST_PIN 3

// RFID Control Pins
#define RFID_RST_PIN 1

// I2C Pins for MPU6500
#define I2C_SDA 8
#define I2C_SCL 9
#define MPU6500_ADDR 0x68

// NeoPixel (onboard Adafruit LED) pin
#define NEOPIXEL_PIN 10

// Countdown defines
#define TRIGGER_ANGLE 60.0  // Degrees of tilt to start countdown

// ==========================================
// Types
// ==========================================
typedef enum CountdownState {
  COUNTDOWN_ACTIVE,
  COUNTDOWN_STOPPED
} countdownState_t;


// ==========================================
// Device Instances
// ==========================================
// Adafruit ST7735 instance (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

// NeoPixel instance (single LED)
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// MFRC522 (SPI with CS on GPIO0)
MFRC522 mfrc522(RFID_CS_PIN, RFID_RST_PIN);

// MPU6500 (I2C on GPIO8/GPIO9)
MPU6500_WE imu = MPU6500_WE(MPU6500_ADDR);

Preferences prefs;

// ==========================================
// Global Variables
// ==========================================
static uint32_t lastUpdateTime = 0;
static int32_t countdownValue = 600;  // Start at 600 seconds (10 minutes)
static uint32_t lastDayOfYear = 0;

static countdownState_t countdownState = COUNTDOWN_STOPPED;

// ==========================================
// Function Declarations
// ==========================================
void initializeSPI();
void initializeDisplay();
void initializeRFID();
void initializeIMU();
void updateCountdownDisplay();
void handleRFID();
void handleMPU();
void handleTimeChange();
void countdownActive();
void countdownStopped();

// ==========================================
// Setup Function
// ==========================================
void setup()
{
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize
  while(!Serial);
  
  Serial.println("\n\nDigital Hourglass Starting...");

  // Initialize NeoPixel
  pixels.begin();
  pixels.fill(pixels.Color(255, 0, 0), 0, 1);
  pixels.show();
    
  // Initialize SPI Bus
  initializeSPI();
  
  // Initialize Display
  initializeDisplay(); 
  pixels.fill(pixels.Color(0, 0, 255), 0, 1);
  pixels.show(); 

  // Initialize RFID
  initializeRFID();
  pixels.fill(pixels.Color(255, 255, 0), 0, 1);
  pixels.show(); 

  // Initialize IMU
  initializeIMU();
  pixels.fill(pixels.Color(0, 255, 0), 0, 1);
  pixels.show(); 
  
  Serial.println("All devices initialized successfully!");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, SSID_PSK);
  setDebug(INFO);
  waitForSync();

	Serial.println();
	Serial.println("UTC:             " + UTC.dateTime());
  Serial.println("Day of Year:     " + String(dayOfYear()));

  // Setup persistent storage
  prefs.begin("hourglass", false);
  countdownValue = prefs.getInt("countdown", 600);  // Default to 600 seconds if not set
  lastDayOfYear = prefs.getUInt("dayOfYear", dayOfYear());
  updateCountdownDisplay();
  
  // Set initial display update time
  lastUpdateTime = millis();

  // Set NeoPixel to Off
  pixels.fill(pixels.Color(0, 0, 0), 0, 1);
  pixels.show();
}

// ==========================================
// Main Loop
// ==========================================
void loop()
{

  switch (countdownState)
  {
    case COUNTDOWN_STOPPED:
      countdownStopped();
      break;
    case COUNTDOWN_ACTIVE:
      // Countdown logic handled below
      countdownActive();
      break;
    default:
      countdownState = COUNTDOWN_STOPPED;
      break;
  }
   
  //check RFID Tags
  handleRFID();

  //Check MPU
  handleMPU();

  //handle Time change
  handleTimeChange();
  
  delay(100);
}

// ==========================================
// Initialization Functions
// ==========================================

void initializeSPI()
{
  Serial.println("Initializing SPI Bus (FSPI)...");
  
  // Configure SPI bus (using FSPI)
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, RFID_CS_PIN);
  
  // Set up chip select pins as outputs
  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(RFID_CS_PIN, OUTPUT);
  
  // Ensure all CS pins are high (inactive)
  digitalWrite(TFT_CS_PIN, HIGH);
  digitalWrite(RFID_CS_PIN, HIGH);
  
  Serial.println("SPI Bus initialized");
}

void initializeDisplay()
{
  Serial.println("Initializing TFT Display...");

  // Initialize Adafruit ST7735 (use a common init for ST7735R)
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  // Clear the counter area (save bandwidth by only updating needed area)
  tft.fillRect(15, 60, 100, 40, ST77XX_BLACK);

  // Draw large counter
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setCursor(25, 50);
  tft.println("--:--");

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
  Serial.println("Initializing MPU6500 IMU (I2C)...");

  // Initialize I2C bus BEFORE device initialization
  Wire.begin(I2C_SDA, I2C_SCL, 400000);  // Start I2C with 400kHz clock
  delay(100);

  // Initialize MPU9250
  if (!imu.init())
  {
    Serial.println("ERROR: MPU6500 initialization failed!");
  }
  else
  {
    Serial.println("MPU6500 initialization successful");
    
    Serial.println("Position you MPU6500 flat and don't move it - calibrating...");
    delay(1000);
    imu.autoOffsets();
    Serial.println("Done!");

    imu.enableGyrDLPF();
    imu.setGyrDLPF(MPU6500_DLPF_6);
    imu.setSampleRateDivider(5);
    imu.setGyrRange(MPU6500_GYRO_RANGE_250);
    imu.setAccRange(MPU6500_ACC_RANGE_2G);
    imu.enableAccDLPF(true);
    imu.setAccDLPF(MPU6500_DLPF_6);
    delay(200);
  }

  Serial.println("IMU initialized");
}

// ==========================================
// Display Update Function
// ==========================================

void updateCountdownDisplay()
{
  // Convert countdown seconds to MM:SS format
  int32_t minutes = countdownValue / 60;
  int32_t seconds = countdownValue % 60;
  
  // Create time string
  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);
  
  // Clear the counter area (save bandwidth by only updating needed area)
  tft.fillRect(15, 60, 100, 40, ST77XX_BLACK);

  // Draw large counter
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setCursor(25, 50);
  tft.println(timeStr);
}

// ==========================================
// Handle RFID Tags
// ==========================================
void handleRFID() {
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
}

// ==========================================
// Handle MPU
// ==========================================
void handleMPU() {
  static const int32_t mpuPeriod = 1000;
  static uint32_t previousMillis = 0;
  // Read sensor data from MPU9250
  xyzFloat angles = imu.getAngles();          // Get angles in degrees
      
  float mpuTemp = imu.getTemperature();

  // Display sensor data every mpuPeriod, non-blocking
  if (millis() - previousMillis >= mpuPeriod) {
    Serial.print("Angles X: ");
    Serial.print(angles.x);
    Serial.print("\tAngles Y: ");
    Serial.print(angles.y);
    Serial.print("\tAngles Z: ");
    Serial.print(angles.z);
    Serial.println(" Degrees");

    Serial.print("Temp: ");
    Serial.print(mpuTemp);
    Serial.println(" C\n");

    previousMillis = millis();

    if (angles.y >= TRIGGER_ANGLE) {
      if (countdownState != COUNTDOWN_ACTIVE)
      {
        Serial.println("Device tilted forward - starting countdown");
        countdownState = COUNTDOWN_ACTIVE;
        pixels.fill(pixels.Color(255, 255, 0), 0, 1);
        pixels.show();
      }    
    }
    else {
      if (countdownState != COUNTDOWN_STOPPED)
      {
        Serial.println("Device tilted backward - stopping countdown");
        countdownState = COUNTDOWN_STOPPED;
        pixels.fill(pixels.Color(0, 0, 0), 0, 1);
        pixels.show();
      }
    }
  }
}

// ==========================================
// Handle Time Change
// ==========================================
void handleTimeChange() {
  static uint32_t lastCheckedTime = 0;
  uint32_t currentTime = millis();
  if (currentTime - lastCheckedTime > 3600000)
  {
    lastCheckedTime = currentTime;
    uint32_t currentDayOfYear = dayOfYear();
    if (currentDayOfYear != lastDayOfYear) 
    {
      Serial.println("Day of year changed - resetting countdown");
      countdownValue = 600; // Reset countdown to 10 minutes
      updateCountdownDisplay();
      lastDayOfYear = currentDayOfYear;
      prefs.putInt("countdown", countdownValue);
      prefs.putUInt("dayOfYear", lastDayOfYear);
    }
  }
}

// ==========================================
// Countdown Functions
// ==========================================
void countdownActive() {
  static uint32_t lastSavedTime = 0;
  uint32_t currentTime = millis();
    // Update countdown every 1000ms (1 second)
  if (currentTime - lastUpdateTime >= 1000)
  {
    countdownValue -= (currentTime - lastUpdateTime) / 1000;
    lastUpdateTime = currentTime;
    
    // Leave Countdown at 0, set LED to red
    if (countdownValue < 0)
    {
      countdownValue = 0;
      pixels.fill(pixels.Color(255, 0, 0), 0, 1);
      pixels.show();
    }
    else 
    {
      pixels.fill(pixels.Color(0, 255, 0), 0, 1);
      pixels.show();
    }
    
    updateCountdownDisplay();

    if (currentTime - lastSavedTime >= 60000) // Save every 60 seconds
    {
      prefs.putInt("countdown", countdownValue);
      lastSavedTime = currentTime;
    }
  }
}

void countdownStopped() {
  static int32_t lastCountdownValue = countdownValue;
  if (countdownValue != lastCountdownValue) 
  {
    lastCountdownValue = countdownValue;
    updateCountdownDisplay();

    prefs.putInt("countdown", countdownValue);
    prefs.putUInt("dayOfYear", dayOfYear());
  }
  lastUpdateTime = millis();
}