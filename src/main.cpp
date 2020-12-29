#include <Arduino.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include <SoftwareSerial.h>
#include <Adafruit_BME280.h>
#include "ESPWebMQTT.h"
// Комнатная метеостанция на ESP8266 с MQTT
// Подключение к ESP8266-07:
// -------------------------
// ILI9341 SPI:
// VCC		 ->	+3.3
// GND		 ->	GND
// CS		 ->	GPIO16
// Reset	 ->	+3.3
// DC		 ->	GPIO15
// SDI/MOSI ->	GPIO13
// SCK		 ->	GPIO14
// LED		 ->	+3.3
// SDO/MISO ->	not connected
// -------------------------
// BME280:
// SDO - GND для установки адреса 0x76 (по умолчанию 0x77)
// GND		 ->	GND
// +3.3	 ->	+3.3
// SDA		 ->	GPIO4
// SCL		 ->	GPIO5
// -------------------------
// MH-Z19:
// VCC		 ->	+5
// GND		 ->	GND
// TX		 ->	GPIO0 (RX)
// RX		 ->	GPIO2 (TX)
// -------------------------
// China SL-TX583 WWW.HH.COM 433Mhz weather sensor decoder.
// __           ___       ___    ___
//   |         |  |      |  |   |  |
//   |_________|  |______|  |___|  |
//
//   |  Sync      |    1    |  0   |
//   |  9000us    | 4250us  | 1750us
// Defines
const int data433Pin = 10;
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
//----------------------------------------------------------------------------------------------------
#define max_readings 120

// ILI9341 hardware SPI (MOSI, SCK)
#define TFT_DC 15
#define TFT_CS 16
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

float min_temp = 100, max_temp = -100, min_humi = 100, max_humi = -100;
float min_bar = 1000, max_bar = -1000, min_co2 = 2000, max_co2 = -2000;

float temp_readings[max_readings + 1] = {0};
float humi_readings[max_readings + 1] = {0};
float bar_readings[max_readings + 1] = {0};
float co2_readings[max_readings + 1] = {0};
int   reading = 1;

// Значения цветов 16-bit:
#define BLACK       0x0000
#define BLUE        0x001F
#define RED         0xF800
#define GREEN       0x07E0
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define WHITE       0xFFFF
#define GREY        0x8410
#define LBLUE       0x07FF
#define LGREEN      0x87E0
#define ORANGE      0xFC00
#define LRED        0xF81F
//----------------------------------------------------------------------------------------------------

unsigned long currentTime;
unsigned long loopTime;
// CO2
SoftwareSerial mySerial(0, 2); // RX, TX
byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // команда запроса данных у MH-Z19 
unsigned char response[9];	 // сюда пишем ответ MH-Z19
unsigned int ppm = 0;		 // текущее значение уровня СО2

// Сервер narodmon.ru
const char* host = "narodmon.ru";
const int httpPort = 8283;

void DrawGraph(int x_pos, int y_pos, int width, int height, int Y1Max, String title, float DataArray[max_readings], boolean auto_scale, boolean barchart_mode, int graph_colour);
String utf8rus(String source);

const char* const jsonTemperature PROGMEM = "temperature";
const char* const jsonPressure PROGMEM = "pressure";
const char* const jsonHumidity PROGMEM = "humidity";
const char* const jsonCO2 PROGMEM = "co2";
const char* const jsonTemp1 PROGMEM = "temp1";
const char* const jsonTemp2 PROGMEM = "temp2";
const char* const jsonTemp3 PROGMEM = "temp3";
const char* const jsonHum1 PROGMEM = "hum1";
const char* const jsonHum2 PROGMEM = "hum2";
const char* const jsonHum3 PROGMEM = "hum3";

Adafruit_BME280 bme; 

class ESPWeatherStation : public ESPWebMQTTBase {
public:
  ESPWeatherStation();
protected:
  void setupExtra();
  void loopExtra();
  void handleRootPage();
  String jsonData();
private:
  float temperature;
  float pressure;
  float humidity;
  float co2;
  float co2mqtt;
  float temp1, temp2, temp3;
  int hum1, hum2, hum3;
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

}

void ESPWeatherStation::setupExtra() {
  ESPWebMQTTBase::setupExtra();
  bool status = bme.begin();
  if (!status) {
      Serial.println("Error read BME280 sensor!");
      while (1);
  }
}

void ESPWeatherStation::loopExtra() {
  const uint32_t timeout = 5000; // 5 sec.
  static uint32_t nextTime;

  ESPWebMQTTBase::loopExtra();

  // Read sensors 433 MHz
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
    int Temperarure;
    if (T > 2047) Temperarure = T - 4096;
    else Temperarure = T;
    Serial.print(Temperarure/10.0,1);
    Serial.print("C Humidity=");
    byte H = (myData1 >> 0) & 0xFF;       // Get LLLL
    Serial.print(H);
    Serial.println("%");
    
    if (pubSubClient->connected() && ID != 0 ) {
      String path, topic;

      if (_mqttClient != strEmpty) {
        path += charSlash;
        path += _mqttClient;
      }
      path += charSlash;
      path += CH;
      path += charSlash;
      topic = path + FPSTR(jsonTemperature);
      mqttPublish(topic, String(Temperarure/10.0));
      topic = path + FPSTR(jsonHumidity);
      mqttPublish(topic, String(H));
    }
    if (CH == 1) {
      temp1 = Temperarure/10.0;
      hum1 = H;
    }
    if (CH == 2) {
      temp2 = Temperarure/10.0;
      hum2 = H;
    }
    if (CH == 3) {
      temp3 = Temperarure/10.0;
      hum3 = H;
    }
  }
  delay(100);
  
  if (millis() >= nextTime) {

  float temp_temperature = (rint(bme.readTemperature()*10))/10; 
  float temp_humidity    = rint(bme.readHumidity()); 
  float temp_pressure    = rint((bme.readPressure() / 100.0F)*0.7500617) - 680; 

  if (isnan(temp_humidity) || isnan(temp_temperature) || isnan(temp_pressure)) {
    Serial.println("Failed to read from BME280 sensor!");
    return;
  }

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
        }
    }
    if (isnan(pressure) || (pressure != temp_pressure)) {
      pressure = temp_pressure;
      if (pubSubClient->connected()) {
        String path, topic;

        if (_mqttClient != strEmpty) {
          path += charSlash;
          path += _mqttClient;
        }
        path += charSlash;
        topic = path + FPSTR(jsonPressure);
        mqttPublish(topic, String(pressure+680));
      }
    }
    if (isnan(humidity) || (humidity != temp_humidity)) {
      humidity = temp_humidity;
      if (pubSubClient->connected()) {
        String path, topic;

        if (_mqttClient != strEmpty) {
          path += charSlash;
          path += _mqttClient;
        }
        path += charSlash;
        topic = path + FPSTR(jsonHumidity);
        mqttPublish(topic, String(humidity));
      }
    }
    
    //MH-Z19
    mySerial.write(cmd, 9); //Запрашиваем данные у MH-Z19
    memset(response, 0, 9); //Чистим переменную от предыдущих значений
    //delay(10);
    mySerial.readBytes(response, 9); //Записываем свежий ответ от MH-Z19

    unsigned int i;
    byte crc = 0;//Ниже магия контрольной суммы
    for (i = 1; i < 8; i++) crc += response[i];
    crc = 255 - crc;
    crc++;

    //Проверяем контрольную сумму и если она не сходится - перезагружаем модуль
    if ( !(response[0] == 0xFF && response[1] == 0x86 && response[8] == crc) ) {
      Serial.println("CRC error: " + String(crc) + " / "+ String(response[8]));
      while (mySerial.available()) {
        mySerial.read();        
      }
    //ESP.restart();
    }  

    else {
    unsigned int responseHigh = (unsigned int) response[2];
    unsigned int responseLow = (unsigned int) response[3];
    co2 = (256 * responseHigh) + responseLow;  //responseLow - 380
    }
    float temp_co2 = co2;
    if (isnan(co2mqtt) || (co2mqtt != temp_co2)) {
      co2mqtt = temp_co2;
      if (pubSubClient->connected()) {
        String path, topic;

        if (_mqttClient != strEmpty) {
          path += charSlash;
          path += _mqttClient;
        }
        path += charSlash;
        topic = path + FPSTR(jsonCO2);
        mqttPublish(topic, String(co2mqtt+380));
      }
    }

    nextTime = millis() + timeout;
  
  //tft.fillRect(80, 160, 30, 320, BLACK);
  // вывод температуры
  tft.setTextSize(2);
  tft.setTextColor(RED);
  tft.fillRect(3, 30, 80, 20, BLACK);
  tft.setCursor(3, 30);
  tft.print(temperature, 1);
  tft.print(char(0xB0));            //Deg-C symbol
  tft.print("C");
  
  // вывод влажности
  tft.setTextColor(LBLUE);
  tft.fillRect(3, 110, 80, 20, BLACK);
  tft.setCursor(3, 110);
  tft.print(humidity, 1);
  tft.print("%");
  
  temp_readings[reading] = temperature;
  humi_readings[reading] = humidity;
  if (temperature > max_temp) max_temp = temperature;
  if (temperature < min_temp) min_temp = temperature;
  if (humidity    > max_humi) max_humi = humidity;
  if (humidity    < min_humi) min_humi = humidity;
  
  tft.setTextSize(1);
  
  // max and min
  tft.setTextColor(LRED);
  tft.setCursor(6, 10);
  tft.fillRect(6, 10, 80, 10, BLACK);
  tft.print(max_temp, 1);
  tft.print(char(0xB0));          // Deg-C symbol
  tft.print("C max");
  tft.setCursor(6, 90);
  tft.fillRect(6, 90, 80, 10, BLACK);
  tft.print(max_humi, 1);
  tft.print("% max");
  
  tft.setTextColor(GREEN);
  tft.setCursor(6, 55);
  tft.fillRect(6, 55, 80, 10, BLACK);
  tft.print(min_temp, 1);
  tft.print(char(0xB0));          // Deg-C symbol
  tft.print("C min");
  tft.setCursor(6, 135);
  tft.fillRect(6, 135, 80, 10, BLACK);
  tft.print(min_humi, 1);
  tft.print("% min");
  
  // Display temperature readings on graph
  // DrawGraph(int x_pos, int y_pos, int width, int height, int Y1_Max, String title, float data_array1[max_readings], boolean auto_scale, boolean barchart_mode, int colour)
  
  DrawGraph(110, 10, 120, 50, 40, "Температура", temp_readings, autoscale_off,  barchart_on, RED);
  DrawGraph(110, 90, 120, 50, 100, "Влажность",   humi_readings, autoscale_off, barchart_off,  LBLUE);

  // вывод давления
  tft.setTextSize(2);
  tft.setTextColor(ORANGE);
  tft.fillRect(3, 190, 80, 20, BLACK);
  tft.setCursor(3, 190);
  tft.print(pressure+680, 1);
  tft.setTextSize(1);
  tft.print("mmHg");

  // вывод co2
  tft.setTextSize(2);
  tft.setTextColor(LGREEN);  
  tft.fillRect(3, 270, 80, 20, BLACK);
  tft.setCursor(3, 270);
  tft.print((int)co2+380, 1);
  tft.setTextSize(2);
  tft.print("ppm");
  
  bar_readings[reading] = pressure;
  co2_readings[reading] = co2;
  if (pressure > max_bar) max_bar = pressure;
  if (pressure < min_bar) min_bar = pressure;
  if (co2    > max_co2) max_co2 = co2;
  if (co2    < min_co2) min_co2 = co2;
  
  tft.setTextSize(1);
  // max and min
  tft.setTextColor(LRED);
  tft.setCursor(6, 170);
  tft.fillRect(6, 170, 80, 10, BLACK);
  tft.print(max_bar+680, 1);
  tft.print("mmHg max");
  
  tft.setCursor(6, 250);
  tft.fillRect(6, 250, 80, 10, BLACK);
  tft.print(max_co2+380, 1);
  tft.print("ppm max");
  
  tft.setTextColor(GREEN);
  tft.setCursor(6, 215);
  tft.fillRect(6, 215, 80, 10, BLACK);
  tft.print(min_bar+680, 1);
  tft.print("mmHg min");
  
  tft.setCursor(6, 295);
  tft.fillRect(6, 295, 80, 10, BLACK);
  tft.print(min_co2+380, 1);
  tft.print("ppm min");
  tft.setTextColor(WHITE);
 
  DrawGraph(110, 170, 120, 50, 80, "Давление", bar_readings, autoscale_off,  barchart_on, ORANGE);
  DrawGraph(110, 250, 120, 50, 820, "CO2",   co2_readings, autoscale_off, barchart_on,  LGREEN);

  }
  
  currentTime = millis();                           // считываем время, прошедшее с момента запуска программы
  if(currentTime >= (loopTime + 360000)){           // сравниваем текущий таймер с переменной loopTime + 360 секунд
  
	  reading = reading + 1;
	  if (reading > max_readings) { 
		reading = max_readings;
		for (int i = 1; i < max_readings; i++) {
		  temp_readings[i] = temp_readings[i + 1];
		  humi_readings[i] = humi_readings[i + 1];
		  bar_readings[i] = bar_readings[i + 1];
		  co2_readings[i] = co2_readings[i + 1];
		}
		temp_readings[reading] = temperature;
		humi_readings[reading] = humidity;
		bar_readings[reading] = pressure;
		co2_readings[reading] = co2;
	  } 
	  
	  loopTime = currentTime;                         

	  /*Передача данных на сервер narodmon.ru*/
		// Use WiFiClient class to create TCP connections
		WiFiClient client;
		
		if (!client.connect(host, httpPort)) {
		Serial.println("connection failed");
		return;
		}
		
		// отправляем данные  
		Serial.println("Sending..."); 
		// заголовок
		client.print("#");
		client.print(WiFi.macAddress()); // отправляем МАС нашей ESP8266
		client.print("\n");
	   
		// отправляем данные 
		client.print("#temp#");  // название датчика
		client.print(temperature);
		client.print("\n");
		client.print("#humi#");
		client.print(humidity);
		client.print("\n");
		client.print("#bar#");
		client.print(pressure+680);
		client.print("\n");
		client.print("#co2#");
		client.print(co2+380);
		client.println("\n##");

		// читаем ответ с и отправляем его в сериал
		Serial.print("Requesting: ");  
		while(client.available()){
		String line = client.readStringUntil('\r');
		Serial.print(line); // хотя это можно убрать
		}
		
		client.stop();
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
  script += F("document.getElementById('");
  script += FPSTR(jsonHumidity);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHumidity);
   script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonCO2);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonCO2);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonTemp1);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTemp1);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonHum1);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHum1);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonTemp2);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTemp2);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonHum2);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHum2);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonTemp3);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTemp3);
  script += F(";\n");
  script += F("document.getElementById('");
  script += FPSTR(jsonHum3);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHum3);
  script += F(";\n");

  script += F("}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 1000);\n");

  String page = ESPWebBase::webPageStart(F("WeaterStation"));
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
  page += F("Humidity: <span id=\"");
  page += FPSTR(jsonHumidity);
  page += F("\">0</span> %<br/>\n");

  page += F("CO2: <span id=\"");
  page += FPSTR(jsonCO2);
  page += F("\">0</span> ppm<br/>\n");
  
  page += F("Wireless Temp CH1: <span id=\"");
  page += FPSTR(jsonTemp1);
  page += F("\">0</span> C<br/>\n");
  page += F("Wireless Humidity CH1: <span id=\"");
  page += FPSTR(jsonHum1);
  page += F("\">0</span> %<br/>\n");

  page += F("Wireless Temp CH2: <span id=\"");
  page += FPSTR(jsonTemp2);
  page += F("\">0</span> C<br/>\n");
  page += F("Wireless Humidity CH2: <span id=\"");
  page += FPSTR(jsonHum2);
  page += F("\">0</span> %<br/>\n");

  page += F("Wireless Temp CH3: <span id=\"");
  page += FPSTR(jsonTemp3);
  page += F("\">0</span> C<br/>\n");
  page += F("Wireless Humidity CH3: <span id=\"");
  page += FPSTR(jsonHum3);
  page += F("\">0</span> %<br/>\n");


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
  result += String(pressure+680);
  result += F(",\"");
  result += FPSTR(jsonHumidity);
  result += F("\":");
  result += String(humidity);
  result += F(",\"");
  result += FPSTR(jsonCO2);
  result += F("\":");
  result += String(co2+380);
  result += F(",\"");
  result += FPSTR(jsonTemp1);
  result += F("\":");
  result += String(temp1);
  result += F(",\"");
  result += FPSTR(jsonHum1);
  result += F("\":");
  result += String(hum1);
  result += F(",\"");
  result += FPSTR(jsonTemp2);
  result += F("\":");
  result += String(temp2);
  result += F(",\"");
  result += FPSTR(jsonHum2);
  result += F("\":");
  result += String(hum2);
  result += F(",\"");
  result += FPSTR(jsonTemp3);
  result += F("\":");
  result += String(temp3);
  result += F(",\"");
  result += FPSTR(jsonHum3);
  result += F("\":");
  result += String(hum3);

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

/* (C) D L BIRD
*  This function will draw a graph on a TFT / LCD display, it requires an array that contrains the data to be graphed.
*  The variable 'max_readings' determines the maximum number of data elements for each array. Call it with the following parametric data:
*  x_pos - the x axis top-left position of the graph
*  y_pos - the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
*  width - the width of the graph in pixels
*  height - height of the graph in pixels
*  Y1_Max - sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
*  data_array1 is parsed by value, externally they can be called anything else, e.g. within the routine it is called data_array1, but externally could be temperature_readings
*  auto_scale - a logical value (TRUE or FALSE) that switches the Y-axis autoscale On or Off
*  barchart_on - a logical value (TRUE or FALSE) that switches the drawing mode between barhcart and line graph
*  barchart_colour - a sets the title and graph plotting colour
*  If called with Y!_Max value of 500 and the data never goes above 500, then autoscale will retain a 0-500 Y scale, if on, it will increase the scale to match the data to be displayed, and reduce it accordingly if required.
*  auto_scale_major_tick, set to 1000 and autoscale with increment the scale in 1000 steps.
*/
void DrawGraph(int x_pos, int y_pos, int width, int height, int Y1Max, String title, float DataArray[max_readings], boolean auto_scale, boolean barchart_mode, int graph_colour) {
#define auto_scale_major_tick 5 // Sets the autoscale increment, so axis steps up in units of e.g. 5
#define yticks 5                // 5 y-axis division markers
  int maxYscale = 0;
  int y_text = 0;
  if (auto_scale) {
    for (int i = 1; i <= max_readings; i++ ) if (maxYscale <= DataArray[i]) maxYscale = DataArray[i];
    maxYscale = ((maxYscale + auto_scale_major_tick + 2) / auto_scale_major_tick) * auto_scale_major_tick; // Auto scale the graph and round to the nearest value defined, default was Y1Max
    if (maxYscale < Y1Max) Y1Max = maxYscale;
  }
  //Graph the received data contained in an array
  // Draw the graph outline
  tft.drawRect(x_pos, y_pos, width + 2, height + 3, WHITE);
  tft.setTextSize(1);
  tft.setTextColor(graph_colour);
  tft.setCursor(x_pos + (width - title.length() * 3) / 2, y_pos - 10); // 12 pixels per char assumed at size 2 (10+2 pixels)
  tft.print(utf8rus(title));
  tft.setTextSize(1);
  // Draw the data
  int x1, y1, x2, y2;
  for (int gx = 1; gx <= max_readings; gx++) {
    if (DataArray[gx] > 0) {
      x1 = x_pos + gx * width / max_readings;
      y1 = y_pos + height;
      x2 = x_pos + gx * width / max_readings; // max_readings is the global variable that sets the maximum data that can be plotted
      y2 = y_pos + height - constrain(DataArray[gx], 0, Y1Max) * height / Y1Max + 2;
      if (barchart_mode) {
        tft.drawFastVLine(x1, y1-height+1, height-1, BLACK);
        tft.drawLine(x1, y2, x2, y1, graph_colour);

      } else {
        tft.drawFastVLine(x1, y1-height+1, height-1, BLACK);
        tft.drawPixel(x2, y2, graph_colour);
        tft.drawPixel(x2, y2 - 1, graph_colour); // Make the line a double pixel height to emphasise it, -1 makes the graph data go up!
      }
    }
  }

  //Draw the Y-axis scale
  for (int spacing = 0; spacing <= yticks; spacing++) {
    if (!barchart_mode) {

  #define number_of_dashes 40
  
      for (int j = 0; j < number_of_dashes; j++) { // Draw dashed graph grid lines
        if (spacing < yticks) tft.drawFastHLine((x_pos + 1 + j * width / number_of_dashes), y_pos + (height * spacing / yticks), width / (2 * number_of_dashes), WHITE);
      }    }
    if (title=="Давление"){
      y_text = (Y1Max - Y1Max / yticks * spacing)+680;
      }
      else if(title=="CO2"){
      y_text = (Y1Max - Y1Max / yticks * spacing)+380;
      }
      else {
        y_text = Y1Max - Y1Max / yticks * spacing;
      }
    tft.setTextColor(YELLOW);
    
    String y_text_s = (String)y_text;
    int y_text_d = y_text_s.length();
    tft.setCursor((x_pos - (5*y_text_d))-5, y_pos + height * spacing / yticks - 4);
    tft.print(y_text);
  }
  tft.setTextColor(WHITE);
  int x=0;
  for (int t = 0; t <= 10; t++) {
      x+=10;
      tft.setCursor((x_pos - 12)+x, y_pos+58);
      tft.print(t);
  }
  tft.setCursor(225, y_pos+58);
  tft.print("12");
 
  if (barchart_mode) {
    for (int vl = 10; vl <= max_readings; vl += 10) {
      tft.drawFastVLine((x_pos + vl), y_pos + 2, height, GREY);
    }
  }

}

/* Recode russian fonts from UTF-8 to Windows-1251 */
String utf8rus(String source)
{
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xC0) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB8; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
          break;
        }
      }
    }
    m[0] = n; target = target + String(m);
  }
return target;
}

void setup() {
  Serial.begin(115200);
  pinMode(data433Pin, INPUT);

  mySerial.begin(9600);
  tft.begin(); 				// Подключаем TFT экран
  tft.cp437(true);			// Включаем поддержку RusFont
  tft.setRotation(2);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.fillScreen(BLACK);   	// Очистка экрана
  analogWriteFreq(500);    	// Яркость TFT экрана

  for (int x = 0; x <= max_readings; x++) {
    temp_readings[x] = 0;
    humi_readings[x] = 0;
    bar_readings[x] = 0;
    co2_readings[x] = 0;
  } // Очистка масива показаний датчиков

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
  //delay(100);
}


