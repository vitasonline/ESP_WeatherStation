#include <Arduino.h>
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

class ESPBaro : public ESPWebMQTTBase {
public:
  ESPBaro();
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

/*
 * ESPWebBaro class implementation
 */

ESPBaro::ESPBaro() : ESPWebMQTTBase() {
#if defined(BMP085)
  bm = new Adafruit_BMP085();
#elif defined(BMP280)
  bm = new Adafruit_BMP280(); // I2C
#else
  bm = new Adafruit_BME280(); // I2C
#endif
}

void ESPBaro::setupExtra() {
  ESPWebMQTTBase::setupExtra();
  bm->begin();
}

void ESPBaro::loopExtra() {
  const uint32_t timeout = 2000; // 2 sec.
  static uint32_t nextTime;

  ESPWebMQTTBase::loopExtra();
  if (millis() >= nextTime) {
    temperature = bm->readTemperature();
    pressure = bm->readPressure() / 133.33;
#if defined(BME280)
    humidity = bm->readHumidity();
#endif

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
    nextTime = millis() + timeout;
  }
}

void ESPBaro::handleRootPage() {
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

String ESPBaro::jsonData() {
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

ESPBaro* app = new ESPBaro();

void setup() {
  Serial.begin(115200);
  Serial.println();

  app->setup();
}

void loop() {
  app->loop();
}
