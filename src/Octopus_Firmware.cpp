#include "Octopus_Firmware.h"

// -------- Global Instances --------
SFE_UBLOX_GNSS myGPS;
SensirionI2cSps30 sps30;
RTC_RX8025NB rtc;
Adafruit_DotStar strip(1, 0, 3, DOTSTAR_BGR);

bool Octopus::rtcInitialized = false;
bool Octopus::triedInitialRead = false;

// SPS30 state
bool Octopus::sps30Active = false;
bool Octopus::sps30Starting = false;
unsigned long Octopus::sps30StartMillis = 0;
uint8_t Octopus::notReadyStreak = 0;
unsigned long Octopus::lastNotReadyReport = 0;

// -------- SDLogging Implementation --------
SDLogging::SDLogging(int csPin, int recordsPerFile)
: csPin(csPin), recordsPerFile(recordsPerFile),
  recordCount(0), fileIndex(0) {}

void SDLogging::begin() {
    if (!SD.begin(csPin)) Serial.println("SD Card initialization failed.");
    else Serial.println("SD Card initialized.");
    createNewFile();
}

void SDLogging::createNewFile() {
    recordCount = 0;
    fileName = "log" + String(fileIndex) + ".csv";
    Serial.print("Creating new file: ");
    Serial.println(fileName);

    if (SD.exists(fileName)) {
        File file = SD.open(fileName, FILE_READ);
        if (file && file.size() > 0) {
            Serial.println("File exists and is not empty, skipping header.");
            file.close();
        } else {
            if (file) file.close();
            File wf = SD.open(fileName, FILE_WRITE);
            if (wf) {
                Serial.println("Writing header to new file.");
                wf.println("Timestamp,Latitude,Longitude,Temperature,Humidity,PM1.0,PM2.5,PM4.0,PM10.0");
                wf.close();
            } else {
                Serial.println("Failed to create log file.");
                return;
            }
        }
    } else {
        File wf = SD.open(fileName, FILE_WRITE);
        if (wf) {
            Serial.println("Writing header to new file.");
            wf.println("Timestamp,Latitude,Longitude,Temperature,Humidity,PM1.0,PM2.5,PM4.0,PM10.0");
            wf.close();
        } else {
            Serial.println("Failed to create log file.");
            return;
        }
    }
    fileIndex++;
}

void SDLogging::logData(String data) {
    if (recordCount >= recordsPerFile) {
        Serial.println("Record count reached limit. Creating a new file...");
        createNewFile();
    }
    File file = SD.open(fileName, FILE_WRITE);
    if (file) {
        file.println(data);
        file.close();
        recordCount++;
        Serial.print("Data logged: ");
        Serial.println(data);
        Serial.print("Current record count: ");
        Serial.println(recordCount);
    } else {
        Serial.println("Failed to open log file for writing.");
    }
}

// Global SD instance
static SDLogging sd(4, 100);
void initSD(int recordsPerFile) {
    sd = SDLogging(4, recordsPerFile);
    sd.begin();
}
void logToSD(String data) { sd.logData(data); }

// -------- Octopus Core --------
bool Octopus::initializeSensors() {
    return HS300x.begin();
}
void Octopus::setInterval(long) {}
bool Octopus::start() {
    return initializeSensors() && initializeSPS30();
}
float Octopus::readTemperature() {
    return HS300x.readTemperature();
}
float Octopus::readHumidity() {
    return HS300x.readHumidity();
}

// GPS
bool Octopus::initializeGPS() {
    Wire.begin();
    if (!myGPS.begin()) {
        Serial.println("Failed to initialize GPS!");
        return false;
    }
    myGPS.setI2COutput(COM_TYPE_UBX);
    Serial.println("GPS initialized.");
    return true;
}
bool Octopus::readGPSData(float &latitude, float &longitude, float &altitude) {
    latitude  = myGPS.getLatitude()  / 10000000.0;
    longitude = myGPS.getLongitude() / 10000000.0;
    altitude  = myGPS.getAltitude()  / 1000.0;
    return true;
}
String Octopus::getGPSTime() {
    if (myGPS.getTimeValid()) {
        return String(myGPS.getYear()) + "-" +
               String(myGPS.getMonth()) + "-" +
               String(myGPS.getDay()) + " " +
               String(myGPS.getHour()) + ":" +
               String(myGPS.getMinute()) + ":" +
               String(myGPS.getSecond());
    }
    return "0.00";
}

// ---- SPS30 (with warm-up & restart logic) ----
bool Octopus::initializeSPS30() {
    if (sps30Active || sps30Starting) {
        Serial.println("SPS30 init skipped (already active or starting).");
        return true;
    }

    sps30.begin(Wire, SPS30_I2C_ADDR_69);
    sps30.stopMeasurement();
    delay(100);

    int8_t serial[32] = {0};
    if (sps30.readSerialNumber(serial, sizeof(serial)) == 0) {
        Serial.print("SPS30 Serial: ");
        for (int i=0;i<32 && serial[i]!=0;i++) Serial.print((char)serial[i]);
        Serial.println();
    }

    int16_t err = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_UINT16);
    if (err) {
        Serial.print("SPS30 startMeasurement error: ");
        Serial.println(err);
        sps30Active = false;
        sps30Starting = false;
        return false;
    }

    sps30Active = true;
    sps30Starting = true;
    sps30StartMillis = millis();
    notReadyStreak = 0;
    lastNotReadyReport = 0;

    Serial.println("SPS30 measurement started (warm-up).");
    return true;
}

bool Octopus::restartSPS30() {
    Serial.println("Attempting SPS30 restart...");
    stopSPS30();
    delay(200);
    return initializeSPS30();
}

bool Octopus::stopSPS30() {
    bool ok = (sps30.stopMeasurement() == 0);
    sps30Active = false;
    sps30Starting = false;
    return ok;
}

bool Octopus::readSPS30Data(float &pm1_0, float &pm2_5, float &pm4_0, float &pm10_0) {
    pm1_0 = pm2_5 = pm4_0 = pm10_0 = 0.0f;

    if (!sps30Active) {
        // Not active; caller can ignore
        return false;
    }

    // Warm-up suppression
    unsigned long now = millis();
    if (sps30Starting && (now - sps30StartMillis < SPS30_WARMUP_MS)) {
        // Still warming up;
        return false;
    }
    sps30Starting = false;

    uint16_t ready = 0;
    int16_t f = sps30.readDataReadyFlag(ready);
    if (f != 0) {
        // Flag read error; attempt restart after a few consecutive errors
        if (now - lastNotReadyReport > NOT_READY_REPORT_INTERVAL_MS) {
            Serial.print("SPS30 readDataReadyFlag error: ");
            Serial.println(f);
            lastNotReadyReport = now;
        }
        notReadyStreak++;
        if (notReadyStreak >= NOT_READY_RESTART_THRESHOLD) {
            Serial.println("SPS30 restarting due to repeated flag errors.");
            restartSPS30();
        }
        return false;
    }
    if (ready == 0) {
        // Data not yet ready
        notReadyStreak++;
        if (now - lastNotReadyReport > NOT_READY_REPORT_INTERVAL_MS) {
            Serial.println("SPS30: data not ready (waiting)...");
            lastNotReadyReport = now;
        }
        if (!sps30Starting && notReadyStreak >= NOT_READY_RESTART_THRESHOLD) {
            Serial.println("SPS30 restarting due to repeated not-ready flags.");
            restartSPS30();
        }
        return false;
    }

    // Data is ready
    notReadyStreak = 0;

    uint16_t mc1, mc2_5, mc4, mc10;
    uint16_t n0p5, n1, n2_5, n4, n10, tps;
    int16_t err = sps30.readMeasurementValuesUint16(
        mc1, mc2_5, mc4, mc10,
        n0p5, n1, n2_5, n4, n10, tps
    );
    if (err != 0) {
        if (now - lastNotReadyReport > NOT_READY_REPORT_INTERVAL_MS) {
            Serial.print("SPS30 measurement read error: ");
            Serial.println(err);
            lastNotReadyReport = now;
        }
        notReadyStreak++;
        if (notReadyStreak >= NOT_READY_RESTART_THRESHOLD) {
            Serial.println("SPS30 restarting due to repeated read errors.");
            restartSPS30();
        }
        return false;
    }

    // UINT16 mass concentrations are scaled by 10
    pm1_0  = mc1   / 10.0f;
    pm2_5  = mc2_5 / 10.0f;
    pm4_0  = mc4   / 10.0f;
    pm10_0 = mc10  / 10.0f;

    return true;
}

// RTC
bool Octopus::initializeRTC() {
    if (rtcInitialized) return true;
    Wire.begin();
    rtcInitialized = true;

    if (!triedInitialRead) {
        triedInitialRead = true;
        tmElements_t tm = rtc.read();
        uint16_t yr = tmYearToCalendar(tm.Year);
        if (yr < 2024) {
            Serial.println("RTC not set (year < 2024).");
        } else {
            Serial.print("RTC time: ");
            Serial.print(yr); Serial.print("-");
            Serial.print(tm.Month); Serial.print("-");
            Serial.print(tm.Day); Serial.print(" ");
            Serial.print(tm.Hour); Serial.print(":");
            Serial.print(tm.Minute); Serial.print(":");
            Serial.println(tm.Second);
        }
    }
    return true;
}

String Octopus::getTimestamp() {
    if (myGPS.getTimeValid()) return getGPSTime();
    if (rtcInitialized) {
        tmElements_t tm = rtc.read();
        uint16_t yr = tmYearToCalendar(tm.Year);
        if (yr >= 2024) {
            char buf[25];
            snprintf(buf,sizeof(buf),"%04u-%02u-%02u %02u:%02u:%02u",
                     yr, tm.Month, tm.Day, tm.Hour, tm.Minute, tm.Second);
            return String(buf);
        }
    }
    return String("NO-TIME");
}

// Battery / LED
void initBatteryMonitoring() {
    strip.begin();
    strip.show();
}
float calculateBatteryPercentage(float voltage) {
    float p;
    if (voltage >= 4.2) p = 100.0;
    else if (voltage >= 3.7) p = 100.0 - ((4.2 - voltage)/0.5)*20.0;
    else if (voltage >= 3.0) p = 20.0 - ((3.7 - voltage)/0.7)*20.0;
    else p = 0.0;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}
void setDotStarColor(uint8_t r, uint8_t g, uint8_t b) {
    float br = 0.1f;
    strip.setPixelColor(0, strip.Color((uint8_t)(r*br),(uint8_t)(g*br),(uint8_t)(b*br)));
    strip.show();
}
