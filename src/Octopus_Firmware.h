#ifndef OCTOPUS_FIRMWARE_H
#define OCTOPUS_FIRMWARE_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino_HS300x.h>
#include <Adafruit_DotStar.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <SensirionI2cSps30.h>
#include <RTC_RX8025NB.h>
#include <TimeLib.h>

#ifndef ENABLE_RTC_CONSOLE
#define ENABLE_RTC_CONSOLE 0
#endif

// ---------------- SD Logging ----------------
class SDLogging {
public:
    SDLogging(int csPin, int recordsPerFile);
    void begin();
    void logData(String data);
private:
    int csPin;
    int recordsPerFile;
    int recordCount;
    int fileIndex;
    String fileName;
    void createNewFile();
};

// ---------------- Octopus Core ----------------
class Octopus {
public:
    static bool initializeSensors();
    static void setInterval(long interval);
    static bool start();
    static float readTemperature();
    static float readHumidity();

    // SPS30
    static bool initializeSPS30(); // safe to call multiple times; ignores if already active
    static bool readSPS30Data(float &pm1_0, float &pm2_5, float &pm4_0, float &pm10_0);
    static bool stopSPS30();
    static bool restartSPS30(); // force a full stop/start

    // GPS
    static bool initializeGPS();
    static bool readGPSData(float &latitude, float &longitude, float &altitude);
    static String getGPSTime();

    // RTC
    static bool initializeRTC();
    static String getTimestamp();
    static bool rtcInitialized;

private:
    static bool triedInitialRead;

    // ----SPS30 State----
    static bool  sps30Active;
    static bool  sps30Starting;
    static unsigned long sps30StartMillis;
    static const unsigned long SPS30_WARMUP_MS = 3000UL;
    static uint8_t notReadyStreak;
    static unsigned long lastNotReadyReport;
    static const unsigned long NOT_READY_REPORT_INTERVAL_MS = 2000UL;
    static const uint8_t NOT_READY_RESTART_THRESHOLD = 10; // consecutive not-ready after warm-up triggers restart
};

// Battery / LED
void initBatteryMonitoring();
float calculateBatteryPercentage(float voltage);
void setDotStarColor(uint8_t r, uint8_t g, uint8_t b);

// SD helpers
void initSD(int recordsPerFile);
void logToSD(String data);

// Globals
extern SFE_UBLOX_GNSS myGPS;
extern SensirionI2cSps30 sps30;
extern RTC_RX8025NB rtc;

#endif // OCTOPUS_FIRMWARE_H
