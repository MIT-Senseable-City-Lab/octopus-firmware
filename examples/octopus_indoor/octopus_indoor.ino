#include <Octopus_Firmware.h>

// Console enable (keep your setting)
#undef ENABLE_RTC_CONSOLE
#define ENABLE_RTC_CONSOLE 1

// Disable per-second RTC print spam
#define ENABLE_RTC_SECOND_TICK 0

// Timing
unsigned long previousMillis = 0;
const unsigned long interval = 5000;
unsigned long blinkInterval = 100;
unsigned long lastBlinkMillis = 0;
bool isBlinkOn = false;

// Button / power logic
const int buttonPin = 7;
bool deviceOn = false;          // START OFF now
bool longPressHandled = false;
unsigned long buttonPressTime = 0;
const unsigned long longPressDuration = 2000;

// Logging
const int RECORDS_PER_FILE = 200;
const int vbatPin = A0;
const int chargeStatePin = 7;

// Thresholds
const float coldThreshold = 20.0;
const float hotThreshold  = 25.0;

bool sps30Available = false;
bool gpsAvailable   = false;


const unsigned long SERIAL_WAIT_MS = 0;

#if ENABLE_RTC_CONSOLE
#define RX8025_I2C_ADDR 0x32
#define REG_SEC      0x00
#define REG_MIN      0x01
#define REG_HOUR     0x02
#define REG_WDAY     0x03
#define REG_MDAY     0x04
#define REG_MONTH    0x05
#define REG_YEAR     0x06
#define REG_CTRL1    0x0E
#define REG_CTRL2    0x0F
#define CTRL1_WALE   (1u<<7)
#define CTRL1_DALE   (1u<<6)
#define CTRL1_24H    (1u<<5)
#define CTRL1_TEST   (1u<<3)
#define CTRL2_DAFG   (1u<<0)
#define CTRL2_WAFG   (1u<<1)
#define CTRL2_CTFG   (1u<<2)
#define CTRL2_CLEN1  (1u<<3)
#define CTRL2_PON    (1u<<4)
#define CTRL2_XST    (1u<<5)
#define CTRL2_VDET   (1u<<6)
static bool rxWriteN(uint8_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(RX8025_I2C_ADDR);
  Wire.write((uint8_t)(reg << 4));
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}
static bool rxWrite1(uint8_t reg, uint8_t v){return rxWriteN(reg,&v,1);}
static bool rxReadN(uint8_t reg, uint8_t* data, uint8_t len){
  Wire.beginTransmission(RX8025_I2C_ADDR);
  Wire.write((uint8_t)(reg << 4));
  if (Wire.endTransmission(false)!=0)return false;
  uint8_t got = Wire.requestFrom(RX8025_I2C_ADDR,len);
  for (uint8_t i=0;i<got;i++) data[i]=Wire.read();
  return got==len;
}
static void rxInitControls(){
  uint8_t c1=0,c2=0;
  rxReadN(REG_CTRL1,&c1,1);
  c1 &= ~(CTRL1_WALE|CTRL1_DALE|CTRL1_TEST);
  c1 |= CTRL1_24H;
  rxWrite1(REG_CTRL1,c1);
  rxReadN(REG_CTRL2,&c2,1);
  c2 &= ~(CTRL2_PON|CTRL2_VDET|CTRL2_DAFG|CTRL2_WAFG|CTRL2_CTFG);
  c2 |= CTRL2_CLEN1|CTRL2_XST;
  rxWrite1(REG_CTRL2,c2);
}
static void printStatus(){
  uint8_t c1=0,c2=0;
  rxReadN(REG_CTRL1,&c1,1);
  rxReadN(REG_CTRL2,&c2,1);
  Serial.print(F("CTRL1=0b"));Serial.println(c1,BIN);
  Serial.print(F("CTRL2=0b"));Serial.println(c2,BIN);
  Serial.print(F("  24h="));Serial.println((c1&CTRL1_24H)?F("yes"):F("no"));
  Serial.print(F("  TEST="));Serial.println((c1&CTRL1_TEST)?F("1 (disable!)"):F("0"));
  Serial.print(F("  PON="));Serial.println((c2&CTRL2_PON)?F("1 (POR seen)"):F("0"));
  Serial.print(F("  VDET="));Serial.println((c2&CTRL2_VDET)?F("1 (VDD drop)"):F("0"));
  Serial.print(F("  XST="));Serial.println((c2&CTRL2_XST)?F("1 (osc OK)"):F("0 (stopped)"));
}
static bool setToCompileTime(){
  const char* d=__DATE__;
  const char* t=__TIME__;
  const char* months="JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* p=strstr(months,d);
  int month=p?((int)(p-months)/3+1):1;
  int day=atoi(d+4);
  int year=atoi(d+7);
  int hh=atoi(t);
  int mm=atoi(t+3);
  int ss=atoi(t+6);
  rtc.setDateTime(year,month,day,hh,mm,ss);
  return true;
}
static void printTm(const tmElements_t& tm){
  char buf[32];
  snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d",
           tmYearToCalendar(tm.Year),tm.Month,tm.Day,
           tm.Hour,tm.Minute,tm.Second);
  Serial.println(buf);
}
String rxLine;
static void handleCommand(const String& s){
  if (s=="now"){
    if (setToCompileTime()) Serial.println(F("RTC set to sketch compile time."));
  } else if (s.startsWith("set ")){
    uint16_t Y;uint8_t m,d,hh,mm,ss;
    if (sscanf(s.c_str()+4,"%hu-%hhu-%hhu %hhu:%hhu:%hhu",&Y,&m,&d,&hh,&mm,&ss)==6){
      rtc.setDateTime(Y,m,d,hh,mm,ss);
      Serial.println(F("RTC set to given timestamp."));
    } else Serial.println(F("Bad format. Use: set YYYY-MM-DD HH:MM:SS"));
  } else if (s=="stat"){
    printStatus();
  } else if (s.length()){
    Serial.println(F("Unknown command."));
  }
}
#endif // ENABLE_RTC_CONSOLE

void setup() {
    unsigned long startMs = millis();
    Serial.begin(115200);
    // Non-blocking (USB not required)
    while(!Serial && (millis()-startMs) < SERIAL_WAIT_MS) {}

    Serial.println("Starting Octopus Device (Indoor, deferred SPS30)...");

    Octopus::initializeRTC();
#if ENABLE_RTC_CONSOLE
    rxInitControls();
    Serial.println(F("Commands:\n  now\n  set YYYY-MM-DD HH:MM:SS\n  stat"));
    Serial.println(F("RTC console ready.\n"));
#endif

    Serial.println("Initializing sensors...");
    if (!Octopus::initializeSensors()) Serial.println("Failed to initialize sensors. Continuing...");
    else Serial.println("Sensors initialized.");

    Serial.println("Initializing GPS...");
    gpsAvailable = Octopus::initializeGPS();
    if (!gpsAvailable) Serial.println("GPS not detected.");
    else Serial.println("GPS initialized.");

    Serial.println("SPS30 will start on first button ON.");
    Serial.println("Initializing SD card...");
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
                Serial.println("Device turned off (long press).");
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
                    Serial.println("Device turned on (short press).");                    
                    if (!sps30Available) {
                        Serial.println("Starting SPS30...");
                        sps30Available = Octopus::initializeSPS30();
                        if (sps30Available) Serial.println("SPS30 started.");
                        else                Serial.println("SPS30 failed to start.");
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
    unsigned long now = millis();
    if (now - previousMillis < interval) return;
    previousMillis = now;

    String ts = Octopus::getTimestamp();

    float latitude=0, longitude=0, altitude=0;
    if (gpsAvailable) {
        if (!Octopus::readGPSData(latitude, longitude, altitude))
            Serial.println("Failed to read GPS data");
    }

    float temperature = Octopus::readTemperature();
    float humidity    = Octopus::readHumidity();

    float pm1_0=0, pm2_5=0, pm4_0=0, pm10_0=0;
    if (sps30Available) {
        if (!Octopus::readSPS30Data(pm1_0, pm2_5, pm4_0, pm10_0))
            Serial.println("SPS30 data not ready");
    }

    String data = ts + "," + String(latitude,7) + "," + String(longitude,7) + "," +
                  temperature + "," + humidity;
    if (sps30Available)
        data += "," + String(pm1_0) + "," + String(pm2_5) + "," + String(pm4_0) + "," + String(pm10_0);
    else
        data += ",N/A,N/A,N/A,N/A";
    logToSD(data);

    Serial.print("Timestamp: "); Serial.println(ts);
    Serial.print("Latitude: "); Serial.println(latitude,7);
    Serial.print("Longitude: "); Serial.println(longitude,7);

    int vbatRaw = analogRead(vbatPin);
    float vbatVoltage = vbatRaw * (3.294 / 1023.0) * 1.279;
    bool chargeState  = digitalRead(chargeStatePin);
    bool batteryConnected = vbatVoltage > 2.5;
    float batteryPct  = batteryConnected ? calculateBatteryPercentage(vbatVoltage) : 0.0;

    if (temperature < coldThreshold) setDotStarColor(0,0,255);
    else setDotStarColor(128,0,128);

    if (vbatVoltage < 2.5 || !batteryConnected) {
        if (now - lastBlinkMillis >= blinkInterval) {
            lastBlinkMillis = now;
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
#if ENABLE_RTC_CONSOLE
    // Console input (still available)
    while (Serial.available()){
        char c=Serial.read();
        if (c=='\r') continue;
        if (c=='\n'){ handleCommand(rxLine); rxLine=""; }
        else rxLine += c;
    }
#if ENABLE_RTC_SECOND_TICK
    static unsigned long lastRTC=0;
    if (millis()-lastRTC >= 1000){
        lastRTC += 1000;
        tmElements_t tm = rtc.read();
        printTm(tm);
    }
#endif
#endif

    handleButton();
    logSample();
    delay(40);
}