#ifndef __ESPWEB_H
#define __ESPWEB_H

#include <ESP8266WebServer.h>

//#define NOSERIAL // uncomment this define to silent mode (no serial debug info)
//#define NOBLED // uncomment this define to blind mode (no builtin led blinks)

const char charCR = '\r';
const char charLF = '\n';
const char charSlash = '/';
const char charSpace = ' ';
const char charDot = '.';
const char charComma = ',';
const char charColon = ':';
const char charSemicolon = ';';
const char charQuote = '"';
const char charApostroph = '\'';
const char charOpenBrace = '{';
const char charCloseBrace = '}';
const char charEqual = '=';
const char charLess = '<';
const char charGreater = '>';

const char* const strEmpty = "";
const char* const strSlash = "/";

const char* const defSSID PROGMEM = "ESP8266";
const char* const defPassword PROGMEM = "Pa$$w0rd";

const char* const pathSPIFFS PROGMEM = "/spiffs";
const char* const pathUpdate PROGMEM = "/update";
const char* const pathWiFi PROGMEM = "/wifi";
const char* const pathStore PROGMEM = "/store";
const char* const pathReboot PROGMEM = "/reboot";
const char* const pathData PROGMEM = "/data";

const char* const textPlain PROGMEM = "text/plain";
const char* const textHtml PROGMEM = "text/html";
const char* const textJson PROGMEM = "text/json";

const char* const fileNotFound PROGMEM = "FileNotFound";
const char* const indexHtml PROGMEM = "index.html";

const char* const headerTitleOpen PROGMEM = "<!DOCTYPE html>\n\
<html>\n\
<head>\n\
<title>";
const char* const headerTitleClose PROGMEM = "</title>\n";
const char* const headerStyleOpen PROGMEM = "<style type=\"text/css\">\n";
const char* const headerStyleClose PROGMEM = "</style>\n";
const char* const headerStyleExtOpen PROGMEM = "<link rel=\"stylesheet\" href=\"";
const char* const headerStyleExtClose PROGMEM = "\">\n";
const char* const headerScriptOpen PROGMEM = "<script type=\"text/javascript\">\n";
const char* const headerScriptClose PROGMEM = "</script>\n";
const char* const headerScriptExtOpen PROGMEM = "<script type=\"text/javascript\" src=\"";
const char* const headerScriptExtClose PROGMEM = "\"></script>\n";
const char* const headerBodyOpen PROGMEM = "</head>\n\
<body";
const char* const footerBodyClose PROGMEM = "</body>\n\
</html>";
const char* const inputTypeOpen PROGMEM = "<input type=\"";
const char* const inputNameOpen PROGMEM = " name=\"";
const char* const inputValueOpen PROGMEM = " value=\"";
const char* const simpleTagClose PROGMEM = " />";
const char* const typeText PROGMEM = "text";
const char* const typePassword PROGMEM = "password";
const char* const typeRadio PROGMEM = "radio";
const char* const typeButton PROGMEM = "button";
const char* const typeSubmit PROGMEM = "submit";
const char* const typeReset PROGMEM = "reset";
const char* const typeHidden PROGMEM = "hidden";
const char* const typeFile PROGMEM = "file";
const char* const extraChecked PROGMEM = "checked";

const char* const jsonFreeHeap PROGMEM = "freeheap";
const char* const jsonUptime PROGMEM = "uptime";

const char bools[2][6] PROGMEM = { "false", "true" };

const char* const paramApMode PROGMEM = "apmode";
const char* const paramSSID PROGMEM = "ssid";
const char* const paramPassword PROGMEM = "password";
const char* const paramDomain PROGMEM = "domain";
const char* const paramReboot PROGMEM = "reboot";

const uint16_t maxStringLen = 32;

class ESPWebBase {
public:
  ESPWebBase();
  virtual void setup();
  virtual void loop();

  ESP8266WebServer* httpServer;
protected:
  virtual void setupExtra();
  virtual void loopExtra();

  static uint16_t readEEPROMString(uint16_t offset, String& str, uint16_t len);
  static uint16_t writeEEPROMString(uint16_t offset, const String& str, uint16_t len);

  virtual uint16_t readConfig();
  virtual uint16_t writeConfig(bool commit = true);
  virtual void commitConfig();
  virtual void defaultConfig();
  virtual bool setConfigParam(const String& name, const String& value);

  virtual bool setupWiFiAsStation();
  virtual void setupWiFiAsAP();
  virtual void setupWiFi();
  virtual void onWiFiConnected();

  virtual void setupHttpServer();
  virtual void handleNotFound();
  virtual void handleRootPage();
  virtual void handleFileUploaded();
  virtual void handleFileUpload();
  virtual void handleFileDelete();
  virtual void handleSPIFFS();
  virtual void handleUpdate();
  virtual void handleSketchUpdated();
  virtual void handleSketchUpdate();
  virtual void handleWiFiConfig();
  virtual void handleStoreConfig();
  virtual void handleReboot();
  virtual void handleData();
  virtual String jsonData();

  virtual String btnBack();
  virtual String btnWiFiConfig();
  virtual String btnReboot();
  virtual String navigator();

  virtual String getContentType(const String& fileName);
  virtual bool handleFileRead(const String& path);

  static String webPageStart(const String& title);
  static String webPageStyle(const String& style, bool file = false);
  static String webPageScript(const String& script, bool file = false);
  static String webPageBody();
  static String webPageBody(const String& extra);
  static String webPageEnd();
  static String escapeQuote(const String& str);
  static String tagInput(const String& type, const String& name, const String& value);
  static String tagInput(const String& type, const String& name, const String& value, const String& extra);

  bool _apMode;
  String _ssid;
  String _password;
  String _domain;
  static const char _signEEPROM[4] PROGMEM;
};

#endif
