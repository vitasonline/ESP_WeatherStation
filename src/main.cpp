#include <Arduino.h>

// China SL-TX583 WWW.HH.COM 433Mhz weather sensor decoder.

// __           ___       ___    ___
//   |         |  |      |  |   |  |
//   |_________|  |______|  |___|  |
//
//   |  Sync      |    1    |  0   |
//   |  9000us    | 4250us  | 1750us
// Defines
const int data433Pin = 4;
#define DataBits0 4                                       // Number of data0 bits to expect
#define DataBits1 32                                      // Number of data1 bits to expect
#define allDataBits 36                                    // Number of data sum 0+1 bits to expect
// isrFlags bit numbers
#define F_HAVE_DATA 1                                     // 0=Nothing in read buffer, 1=Data in read buffer
#define F_GOOD_DATA 2                                     // 0=Unverified data, 1=Verified (2 consecutive matching reads)
#define F_CARRY_BIT 3                                     // Bit used to carry over bit shift from one long to the other
#define F_STATE 7                                         // 0=Sync mode, 1=Data mode
// Constants
const unsigned long sync_MIN = 8000;                      // Minimum Sync time in micro seconds
const unsigned long sync_MAX = 10000;
const unsigned long bit1_MIN = 2500;
const unsigned long bit1_MAX = 6000;
const unsigned long bit0_MIN = 1000;
const unsigned long bit0_MAX = 2500;
const unsigned long glitch_Length = 700;                  // Anything below this value is a glitch and will be ignored.
// Interrupt variables
unsigned long fall_Time = 0;                              // Placeholder for microsecond time when last falling edge occured.
unsigned long rise_Time = 0;                              // Placeholder for microsecond time when last rising edge occured.
byte bit_Count = 0;                                       // Bit counter for received bits.
unsigned long build_Buffer[] = {0,0};                     // Placeholder last data packet being received.
volatile unsigned long read_Buffer[] = {0,0};             // Placeholder last full data packet read.
volatile byte isrFlags = 0;   


/*
 * BMx280 VCC -> ESP8266 +3.3V, Gnd -> Gnd, SCL -> GPIO5 (D1), SDA -> GPIO4 (D2)
 */

//#define BMP085 // for BMP180
//#define BMP280
#define BME280

#if (defined(BMP085) && defined(BMP280)) || (defined(BMP085) && defined(BME280)) || (defined(BMP280) && defined(BME280))
#error "Please select only one target sensor!"
#endif

#include <Wire.h>
#if defined(BMP085)
#include <Adafruit_BMP085.h>
#elif defined(BMP280)
#include <Adafruit_BMP280.h>
#else
#include <Adafruit_BME280.h>
#endif
#include "ESPWebMQTT.h"

const char* const jsonTemperature PROGMEM = "temperature";
const char* const jsonPressure PROGMEM = "pressure";
#if defined(BME280)
const char* const jsonHumidity PROGMEM = "humidity";
#endif

class ESPWeatherStation : public ESPWebMQTTBase {
public:
  ESPWeatherStation();
protected:
  void setupExtra();
  void loopExtra();
  void handleRootPage();
  String jsonData();
private:
#if defined(BMP085)
  Adafruit_BMP085* bm;
#elif defined(BMP280)
  Adafruit_BMP280* bm;
#else
  Adafruit_BME280* bm;
#endif
  float temperature;
  float pressure;
#if defined(BME280)
  float humidity;
#endif
};

// Various flag bits
void dec2binLong(unsigned long myNum, byte NumberOfBits) {
  if (NumberOfBits <= 32){
    myNum = myNum << (32 - NumberOfBits);
    for (int i=0; i<NumberOfBits; i++) {
      if (bitRead(myNum,31) == 1)
      Serial.print("1");
      else
      Serial.print("0");
      myNum = myNum << 1;
    }
  }
}
/*
 * ESPWeatherStation class implementation
 */

ESPWeatherStation::ESPWeatherStation() : ESPWebMQTTBase() {
#if defined(BMP085)
  bm = new Adafruit_BMP085();
#elif defined(BMP280)
  bm = new Adafruit_BMP280(); // I2C
#else
  bm = new Adafruit_BME280(); // I2C
#endif
}

void ESPWeatherStation::setupExtra() {
  ESPWebMQTTBase::setupExtra();
  bm->begin();

}

void ESPWeatherStation::loopExtra() {
  const uint32_t timeout = 2000; // 2 sec.
  static uint32_t nextTime;

  ESPWebMQTTBase::loopExtra();
  if (millis() >= nextTime) {
    float temp_temperature = bm->readTemperature();
    pressure = bm->readPressure() / 133.33;
#if defined(BME280)
    humidity = bm->readHumidity();
#endif
 if (isnan(temperature) || (temperature != temp_temperature)) {
    temperature = temp_temperature;
    if (pubSubClient->connected()) {
      String path, topic;

      if (_mqttClient != strEmpty) {
        path += charSlash;
        path += _mqttClient;
      }
      path += charSlash;
      topic = path + FPSTR(jsonTemperature);
      mqttPublish(topic, String(temperature));
      topic = path + FPSTR(jsonPressure);
      mqttPublish(topic, String(pressure));
#if defined(BME280)
      topic = path + FPSTR(jsonHumidity);
      mqttPublish(topic, String(humidity));
#endif
    }
 }
    nextTime = millis() + timeout;
//////////////////
  unsigned long myData0 = 0;
  unsigned long myData1 = 0;
  if (bitRead(isrFlags,F_GOOD_DATA) == 1)
  {
    // We have at least 2 consecutive matching reads
    myData0 = read_Buffer[0]; // Read the data spread over 2x 32 variables
    myData1 = read_Buffer[1];
    bitClear(isrFlags,F_HAVE_DATA); // Flag we have read the data
    dec2binLong(myData0,DataBits0);
    dec2binLong(myData1,DataBits1);
    Serial.print(" - ID=");
    byte ID = (myData1 >> 24) & 0xFF;   // Get ID
    Serial.print(ID);
    Serial.print(" - Battery=");
    byte B = (myData1 >> 23) & 0x1;   // Get Battery
    Serial.print(B);
    Serial.print(" - TX=");
    byte TX = (myData1 >> 22) & 0x1;   // Get TX
    Serial.print(TX);
    Serial.print(" Channel=");
    byte CH = ((myData1 >> 20) & 0x3) + 1;     // Get Channel
    Serial.print(CH);
    Serial.print(" Temperature=");
    unsigned long T = (myData1 >> 8) & 0xFFF; // Get Temperature
    Serial.print(T/10.0,1);
    Serial.print("C Humidity=");
    byte H = (myData1 >> 0) & 0xFF;       // Get LLLL
    Serial.print(H);
    Serial.println("%");

    if (pubSubClient->connected()) {
      String path, topic;

      if (_mqttClient != strEmpty) {
        path += charSlash;
        path += _mqttClient;
      }
      path += charSlash;
      path += CH;
      path += charSlash;
      topic = path + FPSTR(jsonTemperature);
      mqttPublish(topic, String(T/10.0));
      topic = path + FPSTR(jsonHumidity);
      mqttPublish(topic, String(H));
    }
  }

  }
}

void ESPWeatherStation::handleRootPage() {
  String script = F("function refreshData() {\n\
var request = new XMLHttpRequest();\n\
request.open('GET', '");
  script += FPSTR(pathData);
  script += F("', true);\n\
request.onreadystatechange = function() {\n\
if (request.readyState == 4) {\n\
var data = JSON.parse(request.responseText);\n\
document.getElementById('");
  script += FPSTR(jsonFreeHeap);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonFreeHeap);
  script += F(";\n\
document.getElementById('");
  script += FPSTR(jsonUptime);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonUptime);
  script += F(";\n\
document.getElementById('");
  script += FPSTR(jsonTemperature);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTemperature);
  script += F(";\n\
document.getElementById('");
  script += FPSTR(jsonPressure);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonPressure);
  script += F(";\n");
#if defined(BME280)
  script += F("document.getElementById('");
  script += FPSTR(jsonHumidity);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHumidity);
  script += F(";\n");
#endif
  script += F("}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 1000);\n");

  String page = ESPWebBase::webPageStart(F("BMx280"));
  page += ESPWebBase::webPageScript(script);
  page += ESPWebBase::webPageBody();
  page += F("<h3>ESP8266</h3>\n\
<p>\n\
Heap free size: <span id=\"");
  page += FPSTR(jsonFreeHeap);
  page += F("\">0</span> bytes<br/>\n\
Uptime: <span id=\"");
  page += FPSTR(jsonUptime);
  page += F("\">0</span> seconds<br/>\n\
Temperature: <span id=\"");
  page += FPSTR(jsonTemperature);
  page += F("\">0</span> C<br/>\n\
Pressure: <span id=\"");
  page += FPSTR(jsonPressure);
  page += F("\">0</span> mmHg<br/>\n");
#if defined(BME280)
  page += F("Humidity: <span id=\"");
  page += FPSTR(jsonHumidity);
  page += F("\">0</span> %<br/>\n");
#endif
  page += F("</p>\n");
  page += navigator();
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

String ESPWeatherStation::jsonData() {
  String result = ESPWebMQTTBase::jsonData();
  result += F(",\"");
  result += FPSTR(jsonTemperature);
  result += F("\":");
  result += String(temperature);
  result += F(",\"");
  result += FPSTR(jsonPressure);
  result += F("\":");
  result += String(pressure);
#if defined(BME280)
  result += F(",\"");
  result += FPSTR(jsonHumidity);
  result += F("\":");
  result += String(humidity);
#endif

  return result;
}

ESPWeatherStation* app = new ESPWeatherStation();


void ICACHE_RAM_ATTR PinChangeISR0(){                                     // Pin 2 (Interrupt 0) service routine
  unsigned long Time = micros();     // Get current time
  if (digitalRead(data433Pin) == HIGH) {                             // Set 'HIGH' some receivers
// Falling edge
    if (Time > (rise_Time + glitch_Length)) {
// Not a glitch
      Time = micros() - fall_Time;     
      // Subtract last falling edge to get pulse time.
      if (bitRead(build_Buffer[1],31) == 1)
        bitSet(isrFlags, F_CARRY_BIT);
      else
        bitClear(isrFlags, F_CARRY_BIT);
        //Serial.println(isrFlags,BIN);
      if (bitRead(isrFlags, F_STATE) == 1) {
// Looking for Data
//Serial.println(Time);
        if ((Time > bit0_MIN) && (Time < bit0_MAX)) {
// 0 bit
          build_Buffer[1] = build_Buffer[1] << 1;
          build_Buffer[0] = build_Buffer[0] << 1;
          if (bitRead(isrFlags,F_CARRY_BIT) == 1)
            bitSet(build_Buffer[0],0);
          bit_Count++;
        }
        else if ((Time > bit1_MIN) && (Time < bit1_MAX)) {
// 1 bit
          build_Buffer[1] = build_Buffer[1] << 1;
          bitSet(build_Buffer[1],0);
          build_Buffer[0] = build_Buffer[0] << 1;
          if (bitRead(isrFlags,F_CARRY_BIT) == 1)
            bitSet(build_Buffer[0],0);
          bit_Count++;
        }
        else {
// Not a 0 or 1 bit so restart data build and check if it's a sync?
          bit_Count = 0;
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bitClear(isrFlags, F_GOOD_DATA);                // Signal data reads dont' match
          bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
          if ((Time > sync_MIN) && (Time < sync_MAX)) {
            // Sync length okay
            bitSet(isrFlags, F_STATE);                    // Set data mode
          }
        } //Serial.println(bit_Count);
        if (bit_Count >= allDataBits) {
// All bits arrived
          bitClear(isrFlags, F_GOOD_DATA);                // Assume data reads don't match
          if (build_Buffer[0] == read_Buffer[0]) {
            if (build_Buffer[1] == read_Buffer[1])
              bitSet(isrFlags, F_GOOD_DATA);              // Set data reads match
          }
          read_Buffer[0] = build_Buffer[0];
          read_Buffer[1] = build_Buffer[1];
          bitSet(isrFlags, F_HAVE_DATA);                  // Set data available
          bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bit_Count = 0;
        }
      }
      else {
// Looking for sync
        if ((Time > sync_MIN) && (Time < sync_MAX)) {
// Sync length okay
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bit_Count = 0;
          bitSet(isrFlags, F_STATE);                      // Set data mode
        }
      }
      fall_Time = micros();                               // Store fall time
    }
  }
  else {
// Rising edge
    if (Time > (fall_Time + glitch_Length)) {
      // Not a glitch
      rise_Time = Time;                                   // Store rise time
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(data433Pin, INPUT);
  Serial.print(F("ISR Pin "));
  Serial.print(data433Pin);
  Serial.println(F(" Configured For Input."));
  attachInterrupt(digitalPinToInterrupt(data433Pin),PinChangeISR0,CHANGE);
  Serial.print(F("Pin "));
  Serial.print(data433Pin);
  Serial.println(F(" SR Function Attached. Here we go."));
  app->setup();
}

void loop() {
  app->loop();
}


