#include <pgmspace.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <FS.h>
#include "ESPWeb.h"

/*
 *   ESPWebBase class implementation
 */

ESPWebBase::ESPWebBase() {
  httpServer = new ESP8266WebServer(80);
}

void ESPWebBase::setup() {
#ifndef NOBLED
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  EEPROM.begin(4096);
  if (! readConfig()) {
#ifndef NOSERIAL
    Serial.println(F("EEPROM is empty!"));
#endif
    defaultConfig();
  }
  if (! SPIFFS.begin()) {
#ifndef NOSERIAL
    Serial.println(F("Unable to mount SPIFFS!"));
#endif
    return;
  }

  setupWiFi();
  setupHttpServer();
  setupExtra();
}

void ESPWebBase::loop() {
  if ((!_apMode) && (WiFi.status() != WL_CONNECTED)) {
    setupWiFi();
  }

  httpServer->handleClient();
  loopExtra();

  yield(); // For WiFi maintenance
}

void ESPWebBase::setupExtra() {
  // Stub
}

void ESPWebBase::loopExtra() {
  // Stub
}

uint16_t ESPWebBase::readEEPROMString(uint16_t offset, String& str, uint16_t len) {
  str = strEmpty;
  for (uint16_t i = 0; i < len; i++) {
    char c = EEPROM.read(offset + i);
    if (! c)
      break;
    else
      str += c;
  }

  return offset + len;
}

uint16_t ESPWebBase::writeEEPROMString(uint16_t offset, const String& str, uint16_t len) {
  int slen = str.length();

  for (uint16_t i = 0; i < len; i++) {
    if (i < slen)
      EEPROM.write(offset + i, str[i]);
    else
      EEPROM.write(offset + i, 0);
  }

  return offset + len;
}

uint16_t ESPWebBase::readConfig() {
  uint16_t offset = 0;

#ifndef NOSERIAL
  Serial.println(F("Reading config from EEPROM"));
#endif
  for (byte i = 0; i < sizeof(ESPWebBase::_signEEPROM); i++) {
    char c = pgm_read_byte(ESPWebBase::_signEEPROM + i);
    if (EEPROM.read(offset + i) != c)
      return 0;
  }
  offset += sizeof(ESPWebBase::_signEEPROM);
  _apMode = EEPROM.read(offset);
  offset += sizeof(_apMode);
  offset = ESPWebBase::readEEPROMString(offset, _ssid, maxStringLen);
  offset = ESPWebBase::readEEPROMString(offset, _password, maxStringLen);
  offset = ESPWebBase::readEEPROMString(offset, _domain, maxStringLen);

  return offset;
}

uint16_t ESPWebBase::writeConfig(bool commit) {
  uint16_t offset = 0;

#ifndef NOSERIAL
  Serial.println(F("Writing config to EEPROM"));
#endif
  for (byte i = 0; i < sizeof(ESPWebBase::_signEEPROM); i++) {
    char c = pgm_read_byte(ESPWebBase::_signEEPROM + i);
    EEPROM.write(offset + i, c);
  }
  offset += sizeof(ESPWebBase::_signEEPROM);
  EEPROM.write(offset, _apMode);
  offset += sizeof(_apMode);
  offset = ESPWebBase::writeEEPROMString(offset, _ssid, maxStringLen);
  offset = ESPWebBase::writeEEPROMString(offset, _password, maxStringLen);
  offset = ESPWebBase::writeEEPROMString(offset, _domain, maxStringLen);
  if (commit)
    commitConfig();

  return offset;
}

inline void ESPWebBase::commitConfig() {
  EEPROM.commit();
}

void ESPWebBase::defaultConfig() {
  _apMode = true;
  _ssid = FPSTR(defSSID);
  _password = FPSTR(defPassword);
}

bool ESPWebBase::setConfigParam(const String& name, const String& value) {
  if (name.equals(FPSTR(paramApMode)))
    _apMode = constrain(value.toInt(), 0, 1);
  else if (name.equals(FPSTR(paramSSID)))
    _ssid = value;
  else if (name.equals(FPSTR(paramPassword)))
    _password = value;
  else if (name.equals(FPSTR(paramDomain)))
    _domain = value;
  else
    return false;

  return true;
}

bool ESPWebBase::setupWiFiAsStation() {
  const uint32_t timeout = 60000;
  uint32_t maxtime = millis() + timeout;

#ifndef NOSERIAL
  Serial.print(F("Connecting to "));
  Serial.println(_ssid);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid.c_str(), _password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
#ifndef NOBLED
    digitalWrite(LED_BUILTIN, LOW);
#endif
    delay(500);
#ifndef NOBLED
    digitalWrite(LED_BUILTIN, HIGH);
#endif
#ifndef NOSERIAL
    Serial.print(charDot);
#endif
    if (millis() >= maxtime) {
#ifndef NOSERIAL
      Serial.println(F(" fail!"));
#endif
      return false;
    }
  }
#ifndef NOSERIAL
  Serial.println();
  Serial.println(F("WiFi connected"));
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif
  return true;
}

void ESPWebBase::setupWiFiAsAP() {
#ifndef NOSERIAL
  Serial.print(F("Configuring access point "));
  Serial.println(_ssid);
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ssid.c_str(), _password.c_str());

#ifndef NOSERIAL
  Serial.print(F("IP address: "));
  Serial.println(WiFi.softAPIP());
#endif
}

void ESPWebBase::setupWiFi() {
  if (_apMode || (! setupWiFiAsStation()))
    setupWiFiAsAP();

  if (_domain.length()) {
    if (MDNS.begin(_domain.c_str())) {
      MDNS.addService("http", "tcp", 80);
#ifndef NOSERIAL
      Serial.println(F("mDNS responder started"));
#endif
    } else {
#ifndef NOSERIAL
      Serial.println(F("Error setting up mDNS responder!"));
#endif
    }
  }

  onWiFiConnected();
}

void ESPWebBase::onWiFiConnected() {
  httpServer->begin();
#ifndef NOSERIAL
  Serial.println(F("HTTP server started"));
#endif
}

void ESPWebBase::setupHttpServer() {
  httpServer->onNotFound(std::bind(&ESPWebBase::handleNotFound, this));
  httpServer->on(strSlash, HTTP_GET, std::bind(&ESPWebBase::handleRootPage, this));
  httpServer->on(String(String(charSlash) + FPSTR(indexHtml)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleRootPage, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleSPIFFS, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_POST, std::bind(&ESPWebBase::handleFileUploaded, this), std::bind(&ESPWebBase::handleFileUpload, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_DELETE, std::bind(&ESPWebBase::handleFileDelete, this));
  httpServer->on(String(FPSTR(pathUpdate)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleUpdate, this));
  httpServer->on(String(FPSTR(pathUpdate)).c_str(), HTTP_POST, std::bind(&ESPWebBase::handleSketchUpdated, this), std::bind(&ESPWebBase::handleSketchUpdate, this));
  httpServer->on(String(FPSTR(pathWiFi)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleWiFiConfig, this));
  httpServer->on(String(FPSTR(pathStore)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleStoreConfig, this));
  httpServer->on(String(FPSTR(pathReboot)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleReboot, this));
  httpServer->on(String(FPSTR(pathData)).c_str(), HTTP_GET, std::bind(&ESPWebBase::handleData, this));
}

void ESPWebBase::handleNotFound() {
  if (! handleFileRead(httpServer->uri()))
    httpServer->send(404, FPSTR(textPlain), FPSTR(fileNotFound));
}

void ESPWebBase::handleRootPage() {
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
}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 500);\n");

  String page = ESPWebBase::webPageStart(F("ESP8266"));
  page += ESPWebBase::webPageScript(script);
  page += ESPWebBase::webPageBody();
  page += F("<h3>ESP8266</h3>\n\
<p>\n\
Heap free size: <span id=\"");
  page += FPSTR(jsonFreeHeap);
  page += F("\">0</span> bytes<br/>\n\
Uptime: <span id=\"");
  page += FPSTR(jsonUptime);
  page += F("\">0</span> seconds</p>\n\
<p>\n");
  page += navigator();
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebBase::handleFileUploaded() {
  httpServer->send(200, FPSTR(textHtml), F("<META http-equiv=\"refresh\" content=\"2;URL=\">Upload successful."));
}

void ESPWebBase::handleFileUpload() {
  static File uploadFile;

  if (httpServer->uri() != FPSTR(pathSPIFFS))
    return;
  HTTPUpload& upload = httpServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (! filename.startsWith(strSlash))
      filename = charSlash + filename;
    uploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile)
      uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile)
      uploadFile.close();
  }
}

void ESPWebBase::handleFileDelete() {
  if (httpServer->args() == 0)
    return httpServer->send(500, FPSTR(textPlain), F("BAD ARGS"));
  String path = httpServer->arg(0);
  if (path == strSlash)
    return httpServer->send(500, FPSTR(textPlain), F("BAD PATH"));
  if (! SPIFFS.exists(path))
    return httpServer->send(404, FPSTR(textPlain), FPSTR(fileNotFound));
  SPIFFS.remove(path);
  httpServer->send(200, FPSTR(textPlain), strEmpty);
  path = String();
}

void ESPWebBase::handleSPIFFS() {
  String script = F("function openUrl(url, method) {\n\
var request = new XMLHttpRequest();\n\
request.open(method, url, false);\n\
request.send(null);\n\
if (request.status != 200)\n\
alert(request.responseText);\n\
}\n\
function getSelectedCount() {\n\
var inputs = document.getElementsByTagName(\"input\");\n\
var result = 0;\n\
for (var i = 0; i < inputs.length; i++) {\n\
if (inputs[i].type == \"checkbox\") {\n\
if (inputs[i].checked == true)\n\
result++;\n\
}\n\
}\n\
return result;\n\
}\n\
function updateSelected() {\n\
document.getElementsByName(\"delete\")[0].disabled = (getSelectedCount() > 0) ? false : true;\n\
}\n\
function deleteSelected() {\n\
var inputs = document.getElementsByTagName(\"input\");\n\
for (var i = 0; i < inputs.length; i++) {\n\
if (inputs[i].type == \"checkbox\") {\n\
if (inputs[i].checked == true)\n\
openUrl(\"");
  script += FPSTR(pathSPIFFS);
  script += F("?filename=/\" + encodeURIComponent(inputs[i].value), \"DELETE\");\n\
}\n\
}\n\
location.reload(true);\n\
}\n");

  String page = ESPWebBase::webPageStart(F("SPIFFS"));
  page += ESPWebBase::webPageScript(script);
  page += ESPWebBase::webPageBody();
  page += F("<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\" onsubmit=\"if (document.getElementsByName('upload')[0].files.length == 0) { alert('No file to upload!'); return false; }\">\n\
<h3>SPIFFS</h3>\n\
<p>\n");

  Dir dir = SPIFFS.openDir("/");
  int cnt = 0;
  while (dir.next()) {
    cnt++;
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    if (fileName.startsWith(strSlash))
      fileName = fileName.substring(1);
    page += F("<input type=\"checkbox\" name=\"file");
    page += String(cnt);
    page += F("\" value=\"");
    page += fileName;
    page += F("\" onchange=\"updateSelected()\" /><a href=\"/");
    page += fileName;
    page += F("\" download>");
    page += fileName;
    page += F("</a>\t");
    page += String(fileSize);
    page += F("<br/>\n");
  }
  page += String(cnt);
  page += F(" file(s)<br/>\n\
<p>\n");
  page += ESPWebBase::tagInput(FPSTR(typeButton), F("delete"), F("Delete"), F("onclick=\"if (confirm('Are you sure to delete selected file(s)?') == true) deleteSelected()\" disabled"));
  page += F("\n\
<p>\n\
Upload new file:<br/>\n");
  page += ESPWebBase::tagInput(FPSTR(typeFile), F("upload"), strEmpty);
  page += charLF;
  page += ESPWebBase::tagInput(FPSTR(typeSubmit), strEmpty, F("Upload"));
  page += F("\n\
</form>\n");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebBase::handleUpdate() {
  String page = ESPWebBase::webPageStart(F("Sketch Update"));
  page += ESPWebBase::webPageBody();
  page += F("<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\" onsubmit=\"if (document.getElementsByName('update')[0].files.length == 0) { alert('No file to update!'); return false; }\">\n\
Select compiled sketch to upload:<br/>\n");
  page += ESPWebBase::tagInput(FPSTR(typeFile), F("update"), strEmpty);
  page += charLF;
  page += ESPWebBase::tagInput(FPSTR(typeSubmit), strEmpty, F("Update"));
  page += F("\n\
</form>\n");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebBase::handleSketchUpdated() {
  static const char* const updateFailed PROGMEM = "Update failed!";
  static const char* const updateSuccess PROGMEM = "<META http-equiv=\"refresh\" content=\"15;URL=\">Update successful! Rebooting...";

  httpServer->send(200, FPSTR(textHtml), Update.hasError() ? FPSTR(updateFailed) : FPSTR(updateSuccess));

  ESP.restart();
}

void ESPWebBase::handleSketchUpdate() {
  if (httpServer->uri() != FPSTR(pathUpdate))
    return;
  HTTPUpload& upload = httpServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    WiFiUDP::stopAll();
#ifndef NOSERIAL
    Serial.print(F("Update sketch from file \""));
    Serial.print(upload.filename.c_str());
    Serial.println(charQuote);
#endif
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (! Update.begin(maxSketchSpace)) { // start with max available size
#ifndef NOSERIAL
      Update.printError(Serial);
#endif
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
#ifndef NOSERIAL
    Serial.print(charDot);
#endif
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
#ifndef NOSERIAL
      Update.printError(Serial);
#endif
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { // true to set the size to the current progress
#ifndef NOSERIAL
      Serial.println();
      Serial.print(F("Updated "));
      Serial.print(upload.totalSize);
      Serial.println(F(" byte(s) successful. Rebooting..."));
#endif
    } else {
#ifndef NOSERIAL
      Update.printError(Serial);
#endif
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
#ifndef NOSERIAL
    Serial.println(F("\nUpdate was aborted"));
#endif
  }
  yield();
}

void ESPWebBase::handleWiFiConfig() {
  String page = ESPWebBase::webPageStart(F("WiFi Setup"));
  page += ESPWebBase::webPageBody();
  page += F("<form name=\"wifi\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\">\n\
<h3>WiFi Setup</h3>\n\
Mode:<br/>\n");
  if (_apMode)
    page += ESPWebBase::tagInput(FPSTR(typeRadio), FPSTR(paramApMode), "1", FPSTR(extraChecked));
  else
    page += ESPWebBase::tagInput(FPSTR(typeRadio), FPSTR(paramApMode), "1");
  page += F("AP\n");
  if (! _apMode)
    page += ESPWebBase::tagInput(FPSTR(typeRadio), FPSTR(paramApMode), "0", FPSTR(extraChecked));
  else
    page += ESPWebBase::tagInput(FPSTR(typeRadio), FPSTR(paramApMode), "0");
  page += F("Infrastructure\n<br/>\n\
SSID:<br/>\n");
  page += ESPWebBase::tagInput(FPSTR(typeText), FPSTR(paramSSID), _ssid, String(F("maxlength=")) + String(maxStringLen));
  page += F("\n<br/>\n\
Password:<br/>\n");
  page += ESPWebBase::tagInput(FPSTR(typePassword), FPSTR(paramPassword), _password, String(F("maxlength=")) + String(maxStringLen));
  page += F("\n<br/>\n\
mDNS domain:<br/>\n");
  page += ESPWebBase::tagInput(FPSTR(typeText), FPSTR(paramDomain), _domain, String(F("maxlength=")) + String(maxStringLen));
  page += F("\n\
.local (leave blank to ignore mDNS)\n\
<p>\n");
  page += ESPWebBase::tagInput(FPSTR(typeSubmit), strEmpty, F("Save"));
  page += charLF;
  page += btnBack();
  page += ESPWebBase::tagInput(FPSTR(typeHidden), FPSTR(paramReboot), "1");
  page += F("\n\
</form>\n");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebBase::handleStoreConfig() {
  String argName, argValue;

#ifndef NOSERIAL
  Serial.print(F("/store("));
#endif
  for (byte i = 0; i < httpServer->args(); i++) {
#ifndef NOSERIAL
    if (i)
      Serial.print(F(", "));
#endif
    argName = httpServer->argName(i);
    argValue = httpServer->arg(i);
#ifndef NOSERIAL
    Serial.print(argName);
    Serial.print(F("=\""));
    Serial.print(argValue);
    Serial.print(charQuote);
#endif
    setConfigParam(argName, argValue);
  }
#ifndef NOSERIAL
  Serial.println(')');
#endif

  writeConfig();

  String page = ESPWebBase::webPageStart(F("Store Setup"));
  page += F("<meta http-equiv=\"refresh\" content=\"5;URL=/\">\n");
  page += ESPWebBase::webPageBody();
  page += F("Configuration stored successfully.\n");
  if (httpServer->arg(FPSTR(paramReboot)) == "1")
    page += F("<br/>\n\
<i>You must reboot module to apply new configuration!</i>\n");
  page += F("<p>\n\
Wait for 5 sec. or click <a href=\"/\">this</a> to return to main page.\n");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebBase::handleReboot() {
#ifndef NOSERIAL
  Serial.println(F("/reboot"));
#endif

  String page = ESPWebBase::webPageStart(F("Reboot"));
  page += F("<meta http-equiv=\"refresh\" content=\"5;URL=/\">\n");
  page += ESPWebBase::webPageBody();
  page += F("Rebooting...\n");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);

  ESP.restart();
}

void ESPWebBase::handleData() {
  String page;

  page += charOpenBrace;
  page += jsonData();
  page += charCloseBrace;

  httpServer->send(200, FPSTR(textJson), page);
}

String ESPWebBase::jsonData() {
  String result;

  result += charQuote;
  result += FPSTR(jsonFreeHeap);
  result += F("\":");
  result += String(ESP.getFreeHeap());
  result += F(",\"");
  result += FPSTR(jsonUptime);
  result += F("\":");
  result += String(millis() / 1000);

  return result;
}

String ESPWebBase::btnBack() {
  String result = ESPWebBase::tagInput(FPSTR(typeButton), strEmpty, F("Back"), F("onclick=\"history.back()\""));
  result += charLF;

  return result;
}

String ESPWebBase::btnWiFiConfig() {
  String result = ESPWebBase::tagInput(FPSTR(typeButton), strEmpty, F("WiFi Setup"), String(F("onclick=\"location.href='")) + String(FPSTR(pathWiFi)) + String(F("'\"")));
  result += charLF;

  return result;
}

String ESPWebBase::btnReboot() {
  String result = ESPWebBase::tagInput(FPSTR(typeButton), strEmpty, F("Reboot!"), String(F("onclick=\"if (confirm('Are you sure to reboot?')) location.href='")) + String(FPSTR(pathReboot)) + String(F("'\"")));
  result += charLF;

  return result;
}

String ESPWebBase::navigator() {
  String result = btnWiFiConfig();
  result += btnReboot();

  return result;
}

String ESPWebBase::getContentType(const String& fileName) {
  if (httpServer->hasArg(F("download")))
    return String(F("application/octet-stream"));
  else if (fileName.endsWith(F(".htm")) || fileName.endsWith(F(".html")))
    return String(FPSTR(textHtml));
  else if (fileName.endsWith(F(".css")))
    return String(F("text/css"));
  else if (fileName.endsWith(F(".js")))
    return String(F("application/javascript"));
  else if (fileName.endsWith(F(".png")))
    return String(F("image/png"));
  else if (fileName.endsWith(F(".gif")))
    return String(F("image/gif"));
  else if (fileName.endsWith(F(".jpg")) || fileName.endsWith(F(".jpeg")))
    return String(F("image/jpeg"));
  else if (fileName.endsWith(F(".ico")))
    return String(F("image/x-icon"));
  else if (fileName.endsWith(F(".xml")))
    return String(F("text/xml"));
  else if (fileName.endsWith(F(".pdf")))
    return String(F("application/x-pdf"));
  else if (fileName.endsWith(F(".zip")))
    return String(F("application/x-zip"));
  else if (fileName.endsWith(F(".gz")))
    return String(F("application/x-gzip"));

  return String(FPSTR(textPlain));
}

bool ESPWebBase::handleFileRead(const String& path) {
  String fileName = path;
  if (fileName.endsWith(strSlash))
    fileName += FPSTR(indexHtml);
  String contentType = getContentType(fileName);
  String pathWithGz = fileName + F(".gz");
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(fileName)) {
    if (SPIFFS.exists(pathWithGz))
      fileName = pathWithGz;
    File file = SPIFFS.open(fileName, "r");
    size_t sent = httpServer->streamFile(file, contentType);
    file.close();

    return true;
  }

  return false;
}

String ESPWebBase::webPageStart(const String& title) {
  String result = FPSTR(headerTitleOpen);
  result += title;
  result += FPSTR(headerTitleClose);

  return result;
}

String ESPWebBase::webPageStyle(const String& style, bool file) {
  String result;

  if (file) {
    result = FPSTR(headerStyleExtOpen);
    result += style;
    result += FPSTR(headerStyleExtClose);
  } else {
    result = FPSTR(headerStyleOpen);
    result += style;
    result += FPSTR(headerStyleClose);
  }

  return result;
}

String ESPWebBase::webPageScript(const String& script, bool file) {
  String result;

  if (file) {
    result = FPSTR(headerScriptExtOpen);
    result += script;
    result += FPSTR(headerScriptExtClose);
  } else {
    result = FPSTR(headerScriptOpen);
    result += script;
    result += FPSTR(headerScriptClose);
  }

  return result;
}

String ESPWebBase::webPageBody() {
  String result = FPSTR(headerBodyOpen);
  result += charGreater;
  result += charLF;

  return result;
}

String ESPWebBase::webPageBody(const String& extra) {
  String result = FPSTR(headerBodyOpen);
  result += charSpace;
  result += extra;
  result += charGreater;
  result += charLF;

  return result;
}

String ESPWebBase::webPageEnd() {
  String result = FPSTR(footerBodyClose);

  return result;
}

String ESPWebBase::escapeQuote(const String& str) {
  String result;
  int start = 0, pos;

  while (start < str.length()) {
    pos = str.indexOf(charQuote, start);
    if (pos != -1) {
      result += str.substring(start, pos) + F("&quot;");
      start = pos + 1;
    } else {
      result += str.substring(start);
      break;
    }
  }

  return result;
}

String ESPWebBase::tagInput(const String& type, const String& name, const String& value) {
  String result = FPSTR(inputTypeOpen);

  result += type;
  result += charQuote;
  if (name != strEmpty) {
    result += FPSTR(inputNameOpen);
    result += name;
    result += charQuote;
  }
  if (value != strEmpty) {
    result += FPSTR(inputValueOpen);
    result += ESPWebBase::escapeQuote(value);
    result += charQuote;
  }
  result += FPSTR(simpleTagClose);

  return result;
}

String ESPWebBase::tagInput(const String& type, const String& name, const String& value, const String& extra) {
  String result = FPSTR(inputTypeOpen);

  result += type;
  result += charQuote;
  if (name != strEmpty) {
    result += FPSTR(inputNameOpen);
    result += name;
    result += charQuote;
  }
  if (value != strEmpty) {
    result += FPSTR(inputValueOpen);
    result += ESPWebBase::escapeQuote(value);
    result += charQuote;
  }
  result += charSpace;
  result += extra;
  result += FPSTR(simpleTagClose);

  return result;
}

const char ESPWebBase::_signEEPROM[4] PROGMEM = { '#', 'E', 'S', 'P' };
