#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  
#include <RTClib.h>


#define RFID_SS   5
#define RFID_RST  27
#define SD_CS     4

MFRC522 mfrc522(RFID_SS, RFID_RST);
LiquidCrystal_I2C lcd(0x27, 16, 2);  
RTC_DS3231 rtc;
File logFile;


unsigned long lastTimeUpdate = 0;
const unsigned long TIME_UPDATE_INTERVAL = 1000;  
bool isIdle = true;

struct Employee {
  String uid;
  String name;
  bool inside;              
  bool onBreak;             
  int breakStartHour;       
  int breakStartMinute;     
  bool isAdmin;             
};

Employee employees[] = {
  { "C3DAA2E4", "Arya", false, false, -1, -1, true },        
  { "D0ADE91B", "Shameem", false, false, -1, -1, false },
  { "1318A711", "Joy", false, false, -1, -1, false },
  { "F3505809", "Achyuth", false, false, -1, -1, false },
  { "AA1BE884", "Naveen", false, false, -1, -1, true }        
  
};

const int EMP_COUNT = sizeof(employees) / sizeof(Employee);


String getCardUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

String getTimestamp() {
  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

String getTimeString() {
  DateTime now = rtc.now();
  char buf[9];
  sprintf(buf, "%02d:%02d", now.hour(), now.minute());
  return String(buf);
}


int getBreakDurationMinutes(int startHour, int startMinute) {
  DateTime now = rtc.now();
  int endTimeInMinutes = now.hour() * 60 + now.minute();
  int startTimeInMinutes = startHour * 60 + startMinute;
  
  int duration = endTimeInMinutes - startTimeInMinutes;

  if (duration < 0) {
    duration += 24 * 60;
  }
  
  return duration;
}


String getBreakType(int durationMinutes) {
  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();
  
  
  if (durationMinutes > 225) {
    return "HALF DAY";
  }
  
  // Otherwise, identify by time of day
  int timeInMinutes = hour * 60 + minute;
  
  // Lunch break: 12:00-14:30
  int lunchStart = 12 * 60;
  int lunchEnd = 14 * 60 + 30;
  
  // Tea break: 16:30-18:00
  int teaStart = 16 * 60 + 30;
  int teaEnd = 18 * 60;
  
  if (timeInMinutes >= lunchStart && timeInMinutes < lunchEnd) {
    return "LUNCH BREAK";
  } else if (timeInMinutes >= teaStart && timeInMinutes < teaEnd) {
    return "TEA BREAK";
  } else {
    return "MISC BREAK";
  }
}

// Check if CSV header exists, if not create it
void initializeCSV() {
  if (!SD.exists("/attendance.csv")) {
    logFile = SD.open("/attendance.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("Date,Time,Employee,Action");
      logFile.close();
      Serial.println("CSV file created with headers");
    }
  }
}

void logToSD(String name, String status, String timestamp) {
  logFile = SD.open("/attendance.csv", FILE_APPEND);
  if (logFile) {
    // Parse timestamp: "2025-06-23 09:30:15"
    String date = timestamp.substring(0, 10);      // "2025-06-23"
    String time = timestamp.substring(11, 19);     // "09:30:15"
    
    // Write CSV row
    logFile.println(date + "," + time + "," + name + "," + status);
    logFile.close();
    Serial.println("Logged: " + name + " | " + status + " | " + timestamp);
  } else {
    Serial.println("Error opening attendance.csv");
    lcd.setCursor(0, 1);
    lcd.print("SD write error! ");
  }
}

// Display idle screen with welcome and current time
void displayIdleScreen() {
  DateTime now = rtc.now();
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  
  lcd.clear();
  lcd.print("ChargeMOD :D");
  lcd.setCursor(0, 1);
  lcd.print(timeStr);
}

// Count employee breaks from CSV logs
int countEmployeeBreaks(String name) {
  logFile = SD.open("/attendance.csv", FILE_READ);
  int breakCount = 0;
  
  if (logFile) {
    // Skip header line
    logFile.readStringUntil('\n');
    
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      
      // Check if this line contains the employee name
      if (line.indexOf(name) != -1) {
        // Count if it's a break (contains BREAK or HALF DAY)
        if (line.indexOf("BREAK") != -1 || line.indexOf("HALF DAY") != -1) {
          breakCount++;
        }
      }
    }
    logFile.close();
  }
  
  return breakCount;
}

// Display all employees and their break status
void displayAllEmployees(int pageNum) {
  // pageNum 0 = employees 0-1, pageNum 1 = employees 2-3, etc.
  
  int startIdx = pageNum * 2;
  
  if (startIdx >= EMP_COUNT) {
    lcd.clear();
    lcd.print("No more emps");
    return;
  }
  
  lcd.clear();
  
  // First employee
  lcd.print(employees[startIdx].name);
  int breaks1 = countEmployeeBreaks(employees[startIdx].name);
  lcd.setCursor(10, 0);
  lcd.print(breaks1);
  lcd.print("B");
  
  // Second employee (if exists)
  if (startIdx + 1 < EMP_COUNT) {
    lcd.setCursor(0, 1);
    lcd.print(employees[startIdx + 1].name);
    int breaks2 = countEmployeeBreaks(employees[startIdx + 1].name);
    lcd.setCursor(10, 1);
    lcd.print(breaks2);
    lcd.print("B");
  }
}

// Admin menu - cycles through employees
void adminMenu(String adminName) {
  int page = 0;
  int maxPages = (EMP_COUNT + 1) / 2;  // 2 employees per page
  unsigned long lastPress = millis();
  
  while (true) {
    displayAllEmployees(page);
    
    delay(3000);  // Show each page for 3 seconds
    page++;
    
    if (page >= maxPages) {
      page = 0;  // Loop back to first page
      break;  // Exit after one full cycle
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  // I2C devices
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.print("Starting...");

  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    lcd.setCursor(0, 1);
    lcd.print("RTC error");
  }

  // Run ONCE to set the clock, then comment out and re-upload:
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // SPI bus + devices  (order matters: SD first, then RFID)
  SPI.begin();   // SCK=18, MISO=19, MOSI=23 by default

  lcd.clear();
  lcd.print("Init SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed!");
    lcd.setCursor(0, 1);
    lcd.print("SD FAILED");
    delay(2000);
  } else {
    Serial.println("SD ready.");
  }

  mfrc522.PCD_Init();   // init RFID AFTER SD to avoid MISO lock-up
  delay(1000);

  // Initialize CSV file with headers if it doesn't exist
  initializeCSV();

  displayIdleScreen();
  isIdle = true;
}

// ---------- Main loop ----------
void loop() {
  // Update idle screen every second
  if (isIdle) {
    unsigned long currentTime = millis();
    if (currentTime - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
      displayIdleScreen();
      lastTimeUpdate = currentTime;
    }
    delay(100);  // Small delay to prevent hammering the loop
  }

  // Check for card scan
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  
  isIdle = false;

  String uid = getCardUID();
  Serial.println("UID scanned: " + uid);

  String name = "";
  bool found = false;
  int empIndex = -1;

  // Find employee
  for (int i = 0; i < EMP_COUNT; i++) {
    if (employees[i].uid.equalsIgnoreCase(uid)) {
      found = true;
      empIndex = i;
      name = employees[i].name;
      break;
    }
  }

  // Unknown card
  if (!found) {
    lcd.clear();
    lcd.print("Unknown card");
    delay(2000);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    isIdle = true;
    lastTimeUpdate = millis();
    displayIdleScreen();
    return;
  }

  // Card found - process state machine
  // (ALL employees log attendance, including admins)
  String ts = getTimestamp();
  String timeStr = getTimeString();  // "HH:MM" format for display

  // STATE 1: Not inside office yet (first entry)
  if (!employees[empIndex].inside && !employees[empIndex].onBreak) {
    employees[empIndex].inside = true;
    
    lcd.clear();
    lcd.print(name);
    lcd.setCursor(0, 1);
    lcd.print("Entered at " + timeStr);
    
    logToSD(name, "IN", ts);
    delay(2500);
  }
  
  // STATE 2: Inside office, not on break (starting a break)
  else if (employees[empIndex].inside && !employees[empIndex].onBreak) {
    employees[empIndex].onBreak = true;
    employees[empIndex].breakStartHour = rtc.now().hour();
    employees[empIndex].breakStartMinute = rtc.now().minute();
    
    lcd.clear();
    lcd.print(name);
    lcd.setCursor(0, 1);
    lcd.print("Break started");
    
    Serial.println(name + " started break at " + timeStr);
    delay(2500);
  }
  
  // STATE 3: On break (returning from break)
  else if (employees[empIndex].inside && employees[empIndex].onBreak) {
    // Calculate break duration
    int durationMinutes = getBreakDurationMinutes(
      employees[empIndex].breakStartHour,
      employees[empIndex].breakStartMinute
    );
    
    String breakType = getBreakType(durationMinutes);
    employees[empIndex].onBreak = false;
    
    // Display break type and duration
    int hours = durationMinutes / 60;
    int mins = durationMinutes % 60;
    
    lcd.clear();
    lcd.print(breakType);
    lcd.setCursor(0, 1);
    char durationStr[16];
    sprintf(durationStr, "%dh %dm", hours, mins);
    lcd.print(durationStr);
    
    // Log break type
    logToSD(name, breakType, ts);
    
    Serial.println(name + " break: " + breakType + " (" + String(hours) + "h " + String(mins) + "m)");
    delay(2500);
  }

  // If this employee is an admin, show them the admin panel
  if (employees[empIndex].isAdmin) {
    delay(1000);  // Brief pause before showing admin panel
    lcd.clear();
    lcd.print(name);
    lcd.setCursor(0, 1);
    lcd.print("Admin Panel");
    delay(1500);
    
    adminMenu(name);
  }

  // Halt the card
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  // Return to idle screen
  isIdle = true;
  lastTimeUpdate = millis();
  displayIdleScreen();
}
