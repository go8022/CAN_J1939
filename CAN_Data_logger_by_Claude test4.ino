/*
 * CAN J1939 Data Logger for Arduino
 * 
 * Hardware:
 * - Arduino (ESP32 or ESP8266)
 * - CAN shield (MCP2515)
 * - LCD button shield
 * - SD card module
 * 
 * Features:
 * - Logs CAN messages in ASC format
 * - Saves data every 30 minutes
 * - Logs specific PGNs: 0cf00300, 18ff335a, 18ff345a, 18ff355a, 18ff3b03, 18fef100, 18f0010b, 0cf00203, 0cf00503, 18fe4a03, 0c010305
 * - Creates filename based on vehicle startup date/time
 * - Records data every 200ms
 * - Email functionality when connected to WiFi
 */

#include <SPI.h>
#include <mcp_can.h>
#include <SD.h>
#include <LiquidCrystal.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266Mail.h>

// Constants
#define CAN_CS_PIN 10          // CAN Controller CS pin
#define SD_CS_PIN 4            // SD Card CS pin
#define CAN_INT_PIN 2          // CAN Controller INT pin
#define LOG_INTERVAL 200       // Log interval in milliseconds
#define SAVE_INTERVAL 1800000  // Save interval in milliseconds (30 minutes)

// LCD Pin configuration
#define LCD_RS 8
#define LCD_EN 9
#define LCD_D4 4
#define LCD_D5 5
#define LCD_D6 6
#define LCD_D7 7
#define LCD_BUTTON_PIN A0

// WiFi Configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Email configuration
const char* emailRecipient = "go8022@gmail.com";
const char* emailSender = "your_sender@email.com";
const char* emailPassword = "your_email_password";
const char* smtpServer = "smtp.gmail.com";
const int smtpPort = 465;

// Initialize objects
MCP_CAN CAN(CAN_CS_PIN);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
File logFile;
ESP8266Mail mail;

// Variables
unsigned long lastLogTime = 0;
unsigned long lastSaveTime = 0;
unsigned long fileStartTime = 0;
bool isLogging = false;
bool engineRunning = false;
String currentFileName = "";
char timeStamp[32];
char dateStamp[32];

// Array of PGNs to record
const unsigned long watchPGNs[] = {
  0x0CF00300,
  0x18FF335A,
  0x18FF345A,
  0x18FF355A,
  0x18FF3B03,
  0x18FEF100,
  0x18F0010B,
  0x0CF00203,
  0x0CF00503,
  0x18FE4A03,
  0x0C010305
};
const int numWatchPGNs = sizeof(watchPGNs) / sizeof(watchPGNs[0]);

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.print("CAN Logger Init");
  
  // Initialize CAN Bus
  while (CAN.begin(CAN_250KBPS) != CAN_OK) {
    Serial.println("CAN BUS initialization failed. Retrying...");
    lcd.setCursor(0, 1);
    lcd.print("CAN Init Failed");
    delay(1000);
  }
  Serial.println("CAN BUS initialized successfully");
  lcd.setCursor(0, 1);
  lcd.print("CAN Init OK    ");
  
  // Initialize SD card
  pinMode(SD_CS_PIN, OUTPUT);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed");
    lcd.setCursor(0, 1);
    lcd.print("SD Init Failed ");
    while (1);
  }
  Serial.println("SD card initialized");
  lcd.setCursor(0, 1);
  lcd.print("SD Init OK     ");
  
  // Set up CAN interrupt
  pinMode(CAN_INT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(CAN_INT_PIN), canReceiveISR, FALLING);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting");
  
  // Try to connect to WiFi for a limited time (10 seconds)
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Connected ");
  } else {
    Serial.println("WiFi connection failed");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Failed    ");
  }
  
  delay(2000);
  lcd.clear();
  lcd.print("Logger Ready");
}

void loop() {
  // Check if engine is running (detect via CAN messages)
  if (engineRunning && !isLogging) {
    startLogging();
  } else if (!engineRunning && isLogging) {
    stopLogging();
  }
  
  // Log data at regular intervals
  if (isLogging && millis() - lastLogTime >= LOG_INTERVAL) {
    logCANData();
    lastLogTime = millis();
  }
  
  // Save file at regular intervals
  if (isLogging && millis() - lastSaveTime >= SAVE_INTERVAL) {
    saveLogFile();
    lastSaveTime = millis();
  }
  
  // Check LCD buttons and handle user interface
  handleLCDButtons();
  
  // Update LCD display
  updateLCD();
}

void canReceiveISR() {
  // Empty ISR - just needed to wake up the main loop when CAN message is received
}

void startLogging() {
  // Get current time from J1939 message (this is simplified, actual implementation would parse from CAN)
  time_t currentTime = getCurrentTimeFromJ1939();
  
  // Format timestamp for the filename
  sprintf(dateStamp, "%04d-%02d-%02d", year(currentTime), month(currentTime), day(currentTime));
  sprintf(timeStamp, "%02d-%02d-%02d", hour(currentTime), minute(currentTime), second(currentTime));
  
  // Create filename with date and time
  currentFileName = String(dateStamp) + "_" + String(timeStamp) + ".asc";
  
  // Create and initialize the file
  logFile = SD.open(currentFileName, FILE_WRITE);
  if (logFile) {
    fileStartTime = millis();
    
    // Write ASC file header
    logFile.print("date ");
    logFile.print(dateStamp);
    logFile.print(" ");
    logFile.println(timeStamp);
    logFile.println("base hex  timestamps absolute");
    logFile.println("internal events logged");
    logFile.println("// version 8.0.0");
    logFile.print("Begin Triggerblock ");
    logFile.print(dateStamp);
    logFile.print(" ");
    logFile.println(timeStamp);
    logFile.println("   0.000000 Start of measurement");
    
    logFile.flush();
    isLogging = true;
    lastLogTime = millis();
    lastSaveTime = millis();
    
    Serial.println("Logging started to file: " + currentFileName);
  } else {
    Serial.println("Error opening log file");
  }
}

void stopLogging() {
  if (logFile) {
    // Write ASC file footer
    logFile.print("End Triggerblock ");
    
    time_t stopTime = getCurrentTimeFromJ1939();
    sprintf(dateStamp, "%04d-%02d-%02d", year(stopTime), month(stopTime), day(stopTime));
    sprintf(timeStamp, "%02d-%02d-%02d", hour(stopTime), minute(stopTime), second(stopTime));
    
    logFile.print(dateStamp);
    logFile.print(" ");
    logFile.println(timeStamp);
    
    logFile.flush();
    logFile.close();
    isLogging = false;
    
    Serial.println("Logging stopped");
    
    // Send email with the log file if connected to WiFi
    if (WiFi.status() == WL_CONNECTED) {
      sendLogFileByEmail();
    }
  }
}

void saveLogFile() {
  if (logFile) {
    // Close current file and open a new one
    logFile.flush();
    logFile.close();
    
    // Send email with the log file if connected to WiFi
    if (WiFi.status() == WL_CONNECTED) {
      sendLogFileByEmail();
    }
    
    // Start a new file with the current time
    time_t currentTime = getCurrentTimeFromJ1939();
    sprintf(dateStamp, "%04d-%02d-%02d", year(currentTime), month(currentTime), day(currentTime));
    sprintf(timeStamp, "%02d-%02d-%02d", hour(currentTime), minute(currentTime), second(currentTime));
    
    // Create new filename with date and time
    currentFileName = String(dateStamp) + "_" + String(timeStamp) + ".asc";
    
    // Create and initialize the new file
    logFile = SD.open(currentFileName, FILE_WRITE);
    if (logFile) {
      fileStartTime = millis();
      
      // Write ASC file header
      logFile.print("date ");
      logFile.print(dateStamp);
      logFile.print(" ");
      logFile.println(timeStamp);
      logFile.println("base hex  timestamps absolute");
      logFile.println("internal events logged");
      logFile.println("// version 8.0.0");
      logFile.print("Begin Triggerblock ");
      logFile.print(dateStamp);
      logFile.print(" ");
      logFile.println(timeStamp);
      logFile.println("   0.000000 Start of measurement");
      
      logFile.flush();
      
      Serial.println("New log file created: " + currentFileName);
    } else {
      Serial.println("Error opening new log file");
      isLogging = false;
    }
    
    lastSaveTime = millis();
  }
}

void logCANData() {
  unsigned long canId;
  unsigned char len = 0;
  unsigned char buf[8];
  
  // Check if there's a CAN message to read
  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    CAN.readMsgBuf(&len, buf);
    canId = CAN.getCanId();
    
    // Check if this CAN ID is in our watch list (PGNs we want to log)
    bool shouldLog = false;
    for (int i = 0; i < numWatchPGNs; i++) {
      if ((canId & 0x1FFFFFF0) == watchPGNs[i]) {
        shouldLog = true;
        break;
      }
    }
    
    // If this is a PGN we're interested in, log it
    if (shouldLog) {
      // Calculate timestamp
      float timestamp = (millis() - fileStartTime) / 1000.0;
      
      // Format the CAN message in ASC format
      char msgBuf[128];
      char dataBuf[64] = "";
      
      // Format data bytes
      for (int i = 0; i < len; i++) {
        char tmp[4];
        sprintf(tmp, "%02X ", buf[i]);
        strcat(dataBuf, tmp);
      }
      
      // Format the full message
      sprintf(msgBuf, "   %.6f 1  %08lXx             Rx   d %d %s", 
              timestamp, canId, len, dataBuf);
      
      // Write to log file
      if (logFile) {
        logFile.println(msgBuf);
        
        // Check if this is J1939 date/time PGN and update engine status
        if ((canId & 0x1FFFFFF0) == 0x18FEF100) {
          // Update engine status based on J1939 message
          // Usually RPM > 0 means engine is running
          engineRunning = (buf[3] > 0 || buf[4] > 0);
        }
      }
    }
  }
}

void handleLCDButtons() {
  int buttonValue = analogRead(LCD_BUTTON_PIN);
  
  // Map analog value to button
  // (Values might need adjustment for your specific shield)
  if (buttonValue < 50) {
    // Right button - Force save current log
    if (isLogging) {
      saveLogFile();
      lcd.clear();
      lcd.print("File Saved!");
      delay(1000);
    }
  } else if (buttonValue < 250) {
    // Up button - Toggle logging
    if (!isLogging) {
      engineRunning = true;  // Force engine status for manual start
      startLogging();
    } else {
      engineRunning = false;
      stopLogging();
    }
    delay(300);  // Debounce
  }
  // Add more button handlers as needed
}

void updateLCD() {
  // Update LCD with status information
  lcd.setCursor(0, 0);
  if (isLogging) {
    lcd.print("Logging: ON     ");
    
    // Display the time since last save
    lcd.setCursor(0, 1);
    unsigned long saveTimeRemaining = SAVE_INTERVAL - (millis() - lastSaveTime);
    lcd.print("Next save: ");
    lcd.print(saveTimeRemaining / 60000);  // Minutes
    lcd.print("m    ");
  } else {
    lcd.print("Logging: OFF    ");
    lcd.setCursor(0, 1);
    if (WiFi.status() == WL_CONNECTED) {
      lcd.print("WiFi: Connected ");
    } else {
      lcd.print("WiFi: OFF       ");
    }
  }
}

time_t getCurrentTimeFromJ1939() {
  // This function would parse J1939 messages to get the current time
  // For this example, we'll just use a simplified implementation
  
  static time_t lastTime = 0;
  
  // If we have valid time data from CAN, use it
  // Otherwise return the last known time or a default
  if (lastTime == 0) {
    // Default time if we haven't received time data yet
    // Dec 17, 2024 at 15:44:44
    tmElements_t tm;
    tm.Year = 2024 - 1970;  // Years since 1970
    tm.Month = 12;
    tm.Day = 17;
    tm.Hour = 15;
    tm.Minute = 44;
    tm.Second = 44;
    lastTime = makeTime(tm);
  }
  
  return lastTime;
}

void sendLogFileByEmail() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot send email - WiFi not connected");
    return;
  }
  
  Serial.println("Preparing to send email with log file...");
  
  ESP8266EmailClient emailClient;
  ESP8266EmailSSLClient secureClient(emailClient);
  
  EmailMessage message;
  message.sender.name = "CAN Data Logger";
  message.sender.email = emailSender;
  message.subject = "CAN Log File: " + currentFileName;
  message.addRecipient("Recipient", emailRecipient);
  message.text.content = "Attached is the CAN data log file.";
  
  // Attach the log file
  if (SD.exists(currentFileName)) {
    File file = SD.open(currentFileName, FILE_READ);
    if (file) {
      message.addAttachment(file, currentFileName.c_str());
      file.close();
    }
  }
  
  // Connect to email server
  secureClient.connect(smtpServer, smtpPort);
  
  // Send the email
  if (!emailClient.sendMail(message)) {
    Serial.println("Failed to send email");
    Serial.println(emailClient.errorReason());
  } else {
    Serial.println("Email sent successfully");
  }
}
