#ifndef __ESPWEBMQTT_H
#define __ESPWEBMQTT_H

#include "ESPWeb.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

const char* const defMQTTClient PROGMEM = "ESP8266";
const uint16_t defMQTTPort = 1883;

const char* const pathMQTT PROGMEM = "/mqtt";

const char* const paramMQTTServer PROGMEM = "mqttserver";
const char* const paramMQTTPort PROGMEM = "mqttport";
const char* const paramMQTTUser PROGMEM = "mqttuser";
const char* const paramMQTTPassword PROGMEM = "mqttpswd";
const char* const paramMQTTClient PROGMEM = "mqttclient";

class ESPWebMQTTBase : public ESPWebBase {
public:
  ESPWebMQTTBase();

  PubSubClient* pubSubClient;
protected:
  void setupExtra();
  void loopExtra();
  uint16_t readConfig();
  uint16_t writeConfig(bool commit = true);
  void defaultConfig();
  bool setConfigParam(const String& name, const String& value);
  void setupHttpServer();
  void handleRootPage();
  virtual void handleMQTTConfig();

  virtual String btnMQTTConfig();
  String navigator();

  virtual bool mqttReconnect();
  virtual void mqttCallback(char* topic, byte* payload, unsigned int length);
  virtual void mqttResubscribe();
  bool mqttSubscribe(const String& topic);
  bool mqttPublish(const String& topic, const String& value);

  String _mqttServer;
  uint16_t _mqttPort;
  String _mqttUser;
  String _mqttPassword;
  String _mqttClient;
  WiFiClient* _espClient;
};

#endif
