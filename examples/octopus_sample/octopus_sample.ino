#define ENABLE_RTC 0
#include <Octopus_Firmware.h>

// Timing
unsigned long previousMillis = 0;
const unsigned long interval = 5000;
unsigned long blinkInterval = 100;
unsigned long lastBlinkMillis = 0;
bool isBlinkOn = false;

// Pins
const int buttonPin = 7;
const int vbatPin   = A0;
const int chargeStatePin = 7;

// State
bool deviceOn = false;          // Start OFF
bool longPressHandled = false;
unsigned long buttonPressTime = 0;
const unsigned long longPressDuration = 2000;

// Logging
const int RECORDS_PER_FILE = 200;

// Thresholds
const float coldThreshold = 20.0;
const float hotThreshold  = 25.0;

bool sps30Available = false;
bool gpsAvailable   = false;

const unsigned long SERIAL_WAIT_MS = 0;

void setup() {
    unsigned long startMs = millis();
    Serial.begin(9600);
    while(!Serial && (millis()-startMs) < SERIAL_WAIT_MS) {}

    Serial.println("Starting Octopus Device (Outdoor, deferred SPS30)...");

    if (!Octopus::initializeSensors())
        Serial.println("Failed to initialize sensors. Continuing...");
    else
        Serial.println("Sensors initialized.");

    Serial.println("Initializing GPS...");
    gpsAvailable = Octopus::initializeGPS();
    if (!gpsAvailable) Serial.println("GPS not detected.");
    else Serial.println("GPS initialized.");

    Serial.println("SPS30 will start on button ON.");

    initSD(RECORDS_PER_FILE);
    Serial.println("SD card initialized.");

    initBatteryMonitoring();
    pinMode(buttonPin, INPUT_PULLUP);

    Serial.println("Setup complete. Press button to turn on device and start SPS30.");
}

void handleButton() {
    int state = digitalRead(buttonPin);
    unsigned long now = millis();

    if (state == LOW) {
        if (buttonPressTime == 0) buttonPressTime = now;
        if (!longPressHandled && (now - buttonPressTime) >= longPressDuration) {
            if (deviceOn) {
                deviceOn = false;
                Serial.println("Device turned off (long press)");
                setDotStarColor(0,0,0);
                if (sps30Available) {
                    Octopus::stopSPS30();
                    sps30Available = false;
                    Serial.println("SPS30 stopped.");
                }
                delay(120);
                longPressHandled = true;
            }
        }
    } else {
        if (buttonPressTime != 0) {
            if (!longPressHandled) {
                if (!deviceOn) {
                    deviceOn = true;
                    Serial.println("Device turned on (short press)");
                    //initSD(RECORDS_PER_FILE);
                    initBatteryMonitoring();
                    if (!sps30Available) {
                        Serial.println("Starting SPS30...");
                        sps30Available = Octopus::initializeSPS30();
                        if (sps30Available) Serial.println("SPS30 started.");
                        else Serial.println("SPS30 failed to start.");
                    }
                }
            }
            buttonPressTime = 0;
            longPressHandled = false;
            delay(40);
        }
    }
}

void logSample() {
    if (!deviceOn) return;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis < interval) return;
    previousMillis = currentMillis;

    String gpsTime = gpsAvailable ? Octopus::getGPSTime() : "N/A";

    float latitude=0, longitude=0, altitude=0;
    if (gpsAvailable)
        Octopus::readGPSData(latitude, longitude, altitude);

    float temperature = Octopus::readTemperature();
    float humidity    = Octopus::readHumidity();

    float pm1_0=0, pm2_5=0, pm4_0=0, pm10_0=0;
    if (sps30Available) {
        if (!Octopus::readSPS30Data(pm1_0, pm2_5, pm4_0, pm10_0))
            Serial.println("SPS30 data not ready");
    }

    String data = gpsTime + "," + String(latitude,7) + "," + String(longitude,7) + "," +
                  temperature + "," + humidity;
    if (sps30Available)
        data += "," + String(pm1_0) + "," + String(pm2_5) + "," + String(pm4_0) + "," + String(pm10_0);
    else
        data += ",N/A,N/A,N/A,N/A";
    logToSD(data);

    Serial.print("GPS Time: "); Serial.println(gpsTime);
    Serial.print("Latitude: "); Serial.println(latitude,7);
    Serial.print("Longitude: "); Serial.println(longitude,7);

    int vbatRaw = analogRead(vbatPin);
    float vbatVoltage = vbatRaw * (3.294 / 1023.0) * 1.279;
    bool chargeState = digitalRead(chargeStatePin);
    bool batteryConnected = vbatVoltage > 2.5;
    float batteryPct = batteryConnected ? calculateBatteryPercentage(vbatVoltage) : 0.0;

    if (temperature < coldThreshold) setDotStarColor(0,0,255);
    else setDotStarColor(128,0,128);

    if (vbatVoltage < 2.5 || !batteryConnected) {
        if (currentMillis - lastBlinkMillis >= blinkInterval) {
            lastBlinkMillis = currentMillis;
            isBlinkOn = !isBlinkOn;
            if (isBlinkOn) setDotStarColor(255,0,0); else setDotStarColor(0,0,0);
        }
    }

    Serial.print("VBAT Voltage: "); Serial.print(vbatVoltage,2);
    Serial.print(" V, Charge State: ");
    Serial.print(chargeState ? "Charging" : "Not Charging");
    Serial.print(", Battery Percentage: ");
    Serial.print(batteryPct,1);
    Serial.println(" %\n");
}

void loop() {
    handleButton();
    logSample();
    delay(40);
}
