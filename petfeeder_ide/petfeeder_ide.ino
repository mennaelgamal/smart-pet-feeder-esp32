#define BLYNK_TEMPLATE_ID "TMPL2l19XYC3B"
#define BLYNK_TEMPLATE_NAME "PetFeeder"
#define BLYNK_AUTH_TOKEN "YvluyGz4_KyO0yTxvF19UR_M0QhMOqOj"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>

const char* ssid     = "Menna";     // ← replace
const char* password = "mennaelgamal"; // ← replace

#define I2C_SDA_PIN 27
#define I2C_SCL_PIN 26
#define SERVO_PIN   4
#define SS_PIN      5
#define RST_PIN     25

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo feederServo;
MFRC522 rfid(SS_PIN, RST_PIN);
BlynkTimer timer;

struct Pet {
  byte uid[4];
  const char* name;
  DateTime lastFed;
};

Pet pets[] = {
  {{0xF3, 0x4C, 0xF8, 0x19}, "Kiwi",  DateTime(2000,1,1,0,0,0)},
  {{0xA5, 0x36, 0x31, 0x03}, "Berry", DateTime(2000,1,1,0,0,0)},
  {{0x93, 0xF4, 0x26, 0x0E}, "Leo",   DateTime(2000,1,1,0,0,0)},
  {{0x43, 0xE2, 0x9A, 0x14}, "Rocky", DateTime(2000,1,1,0,0,0)}
};

const int numPets      = sizeof(pets) / sizeof(pets[0]);
DateTime lastAutoFeed  = DateTime(2000,1,1,0,0,0);
long autoFeedSeconds   = 14400;
int  servoAngle        = 90;
String lastPetName     = "None";
String lastFeedTimeStr = "Never";

// ============================================================
// V0 — Feed Button
// ============================================================
BLYNK_WRITE(V0) {
  if (param.asInt() == 1) {
    DateTime now = rtc.now();

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("App Triggered");
    lcd.setCursor(0, 1); lcd.print("Feeding...");

    dispenseFood();

    for (int i = 0; i < numPets; i++) pets[i].lastFed = now;

    lastPetName = "App (All)";
    buildTimeString(now, lastFeedTimeStr);

    Blynk.virtualWrite(V2, lastPetName);
    Blynk.virtualWrite(V3, lastFeedTimeStr);
    Blynk.virtualWrite(V0, 0);
  }
}

// ============================================================
// V1 — Servo Angle
// ============================================================
BLYNK_WRITE(V1) {
  servoAngle = param.asInt();
  Serial.print("Servo angle set to: ");
  Serial.println(servoAngle);
}

// ============================================================
// V4 — Auto Feed Hours
// ============================================================
BLYNK_WRITE(V4) {
  int hours = param.asInt();
  if (hours > 0) {
    autoFeedSeconds = (long)hours * 3600;
    Serial.print("Auto feed interval: ");
    Serial.print(hours);
    Serial.println(" hour(s)");
  }
}

// ============================================================
// Send status to Blynk every 5 seconds
// ============================================================
void sendStatusToBlynk() {
  Blynk.virtualWrite(V2, lastPetName);
  Blynk.virtualWrite(V3, lastFeedTimeStr);
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();

  // ── WiFi Connect ──
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  Serial.println("Connecting to Hotspot...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\nWiFi FAILED!");
    Serial.print("Status code: ");
    Serial.println(WiFi.status());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Check Hotspot");
    delay(3000);
  }

  // ── Blynk Connect ──
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting Blynk");
  Serial.println("Connecting to Blynk...");

  Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 443);

  int blynkAttempts = 0;
  while (!Blynk.connected() && blynkAttempts < 10) {
    Blynk.connect(5000);
    blynkAttempts++;
    Serial.print("Blynk attempt: ");
    Serial.println(blynkAttempts);
    lcd.setCursor(0, 1);
    lcd.print("Attempt: ");
    lcd.print(blynkAttempts);
    lcd.print("   ");
  }

  if (Blynk.connected()) {
    Serial.println("Blynk Connected!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blynk Connected!");
    delay(2000);
  } else {
    Serial.println("Blynk FAILED!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blynk Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Check Token");
    delay(3000);
  }

  // ── RTC ──
  if (!rtc.begin()) {
    Serial.println("RTC Error!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC Error!");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, adjusting...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // ── Servo ──
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN);
  feederServo.write(0);

  // ── Servo Boot Test ──
  Serial.println("Testing servo...");
  delay(1000);
  feederServo.write(90);
  delay(2000);
  feederServo.write(0);
  Serial.println("Servo test done");

  // ── Blynk Timer ──
  timer.setInterval(5000L, sendStatusToBlynk);

  // ── Ready ──
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
  Serial.println("System Ready!");
  delay(1000);
}

// ============================================================
// Loop
// ============================================================
void loop() {
  Blynk.run();
  timer.run();

  DateTime now = rtc.now();
  displayTime(now);
  checkAutoFeed(now);
  checkRFID(now);
}

// ============================================================
// Auto Feed
// ============================================================
void checkAutoFeed(DateTime now) {
  if ((now.unixtime() - lastAutoFeed.unixtime()) >= autoFeedSeconds) {
    lcd.setCursor(0, 1);
    lcd.print("Auto Feeding   ");
    dispenseFood();
    lastAutoFeed = now;

    for (int i = 0; i < numPets; i++) pets[i].lastFed = now;

    lastPetName = "Auto (All)";
    buildTimeString(now, lastFeedTimeStr);

    Blynk.virtualWrite(V2, lastPetName);
    Blynk.virtualWrite(V3, lastFeedTimeStr);
  }
}

// ============================================================
// RFID Check
// ============================================================
void checkRFID(DateTime now) {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  for (int i = 0; i < numPets; i++) {
    if (memcmp(rfid.uid.uidByte, pets[i].uid, 4) == 0) {
      int delta = now.unixtime() - pets[i].lastFed.unixtime();

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(pets[i].name);

      if (delta >= 15) {
        lcd.setCursor(0, 1);
        lcd.print("Manual Feeding");
        dispenseFood();
        pets[i].lastFed = now;

        lastPetName = String(pets[i].name);
        buildTimeString(now, lastFeedTimeStr);

        Blynk.virtualWrite(V2, lastPetName);
        Blynk.virtualWrite(V3, lastFeedTimeStr);
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Too Early!");
      }

      delay(3000);
      break;
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================
// Dispense Food
// ============================================================
void dispenseFood() {
  feederServo.write(servoAngle);
  delay(5000);
  feederServo.write(0);
}

// ============================================================
// Display Time on LCD
// ============================================================
void displayTime(DateTime now) {
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  if (now.hour()   < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());
}

// ============================================================
// Build time string for Blynk labels
// ============================================================
void buildTimeString(DateTime dt, String& out) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%02d:%02d %02d/%02d",
           dt.hour(), dt.minute(), dt.day(), dt.month());
  out = String(buf);
}