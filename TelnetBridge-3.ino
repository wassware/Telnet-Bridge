// Telnet Bridge -provides routing from fixed ip to any esp on the local network
// must be on fixed IP - I use x.x.x.201
// Built on standard framework.
// First line entered after connect is an IP or the mqttid of the IP to connect to. copy of dns got 
// from mqttbroker to resolve the IP. or blank to telnet to the bridge.
// It also does what everyone tells you not to which is to expose Telnet via the
// firewall on the router. On an mqtt command from the MQTT panel the listener port switches
// to another port which is exposed on the firewall (not saying which!....) and starts an inactive timeout
// after it which it reverts to the usual port. The MQTT panel also advises the router external
// ip address and a one time password.
// The first line entered on the session must be the one time password or telnet shuts down.
// the next is the mqtt of the esp32 to connect to as above.
// The switch is done via mqtt to my cloud Hive mqtt broker.
// The display using Android IoT MQTT Panel by Rahul Kundu shows:
//   the home router external IP
//   the one time password
//   the session timeout remaining
// Once a session is active any further session attempts are silently rejected.
// WifiSerial Android app used for Telnet access

// Secure Shell is of course  the way to go in this day and age, but all too fiddly.



const char* module = "telnetBridge 2";
const char* buildDate = __DATE__  "  "  __TIME__;

// ----------- start properties include 1 --------------------
#include <SPIFFS.h>
#include <ArduinoJson.h>
String propFile = "/props.properties";   // prop file name
#define PROPSSIZE 40
String propNames[PROPSSIZE];
int propNamesSz = 0;
#define BUFFLEN 200
char buffer[BUFFLEN];
int bufPtr = 0;
#include <esp_task_wdt.h>
#include <EEPROM.h>
// ----------- end properties include 1 --------------------

// ----------- start WiFi & Mqtt & Telnet include 1 ---------------------
#define HAVEWIFI 1
// sort of a DNS
#define DNSSIZE 20
#define SYNCHINTERVAL 30
// wifi, time, mqtt
#include <ESP32Time.h>
ESP32Time rtc;
#include <WiFi.h>
#include "ESPTelnet.h"
#include "EscapeCodes.h"
#include <PicoMQTT.h>
ESPTelnet telnet;
IPAddress ip;
PicoMQTT::Client mqttClient;

// ----------- end  WiFi & Mqtt & Telnet include 1 ---------------------

// ---------- start custom ------------------------

// ---------- end custom ------------------------

// --------- standard properties ------------------
int logLevel = 2;
String logLevelN = "logLevel";
int eeWriteLimit = 100;
String eeWriteLimitN = "eeWriteLimit";
String wifiSsid = "<ssid>";
String wifiSsidN = "wifiSsid";        
String wifiPwd = "<pwd>";
String wifiPwdN = "wifiPwd";
byte wifiIp4 = 0;   // > 0 for fixed ip
String wifiIp4N = "wifiIp4";
byte mqttIp4 = 200;
String mqttIp4N = "mqttIp4";
int mqttPort = 1883;
String mqttPortN = "mqttPort";   
int telnetPort = 23;
String telnetPortN = "telnetport";   
String mqttId = "xx";          // is username-N and token xx/n/.. from unitId
String mqttIdN = "mqttId";   
int unitId  = 9;                  // uniquness of mqtt id
String unitIdN = "unitId";
int wdTimeout = 30;
String wdTimeoutN = "wdTimeout";
// generic way of setting property as 2 property setting operations
String propNameA = "";
String propNameN = "propName";
String propValue = "";
String propValueN = "propValue";
// these used to apply adjustment via props system
String restartN = "restart";
String writePropsN = "writeProps";

// ------- custom properties -----------
int telnetFWPort = 69;
String telnetFWPortN = "telnetFWport"; 
// ------- end custom properties -----------

// ----------- start properties include 2 --------------------

// ------------start  for bridge ------------------------
#include <HTTPClient.h>
HTTPClient http;
NetworkClient outClient;    // where we will route to


// ------------end  for bridge ------------------------

bool mountSpiffs()
{
   if(!SPIFFS.begin(true))
  {
    log(1, "SPIFFS Mount Failed");
    return false;
  }
  else
  {
    log(1, "SPIFFS formatted");
  }
  return true;
}

// checks a property name in json doc and keeps a list in propNames
bool checkProp(JsonDocument &doc, String propName, bool reportMissing)
{
  if (propNamesSz >= PROPSSIZE)
  {
    log(0, "!! props names limit");
  }
  else
  {
    propNames[propNamesSz++] = propName;
  }
  if (doc.containsKey(propName))
  {
    String val = doc[propName].as<String>();
    log(0, propName + "=" + val);
    return true;
  }
  if (reportMissing)
  {
    log(0, propName + " missing");
  }
  return false;
}

bool readProps()
{
  log(0, "Reading file " + propFile);
  File file = SPIFFS.open(propFile);
  if(!file || file.isDirectory())
  {
    log(0, "− failed to open file for reading");
    return false;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) 
  {
    log(0, "deserializeJson() failed: ");
    log(0, error.f_str());
    return false;
  }
  extractProps(doc, true);
  return true;
}

// writes/displays the props
bool writeProps(bool noWrite)
{
  JsonDocument doc;
  addProps(doc);
  String s;
  serializeJsonPretty(doc, s);
  s = s.substring(3,s.length()-2);
  log(0, s);
  if (noWrite)
  {
    return true;
  }
  log(0, "Writing file:" + propFile);
  File file = SPIFFS.open(propFile, FILE_WRITE);
  if(!file)
  {
    log(0, "− failed to open file for write");
    return false;
  }
  serializeJson(doc, file);
  file.close();
  return true;
}

// is expected to be form 'name=value' or 'name value' and can be a comma sep list
// name can be case insensitve match on first unique characters..
// converted to Json to update
void adjustProp(String s)
{
  String ss = s;
  while (true)
  {
    int p1 = ss.indexOf(',');
    if (p1 < 0)
    {
      adjustProp2(ss);
      return;
    }
    String s1 = ss.substring(0, p1);
    adjustProp2(s1);
    ss = ss.substring(p1+1);
  }
}
void adjustProp2(String s)
{
  int p1 = s.indexOf('=');
  if (p1 < 0)
  {
    p1 = s.indexOf(' ');
  }
  if (p1 < 0)
  {
    log(0, "no = or space found");
    return;
  }
  String p = s.substring(0,p1);
  String pl = p;
  pl.toLowerCase();
  String v = s.substring(p1+1);
  int ip;
  int m = 0;
  for (int ix = 0; ix < propNamesSz; ix++)
  {
    if (propNames[ix].length() >= pl.length())
    {
      String pn = propNames[ix].substring(0, pl.length());
      pn.toLowerCase();
      if (pl == pn)
      {
        if (m == 0)
        {
          ip = ix;
          m++;
        }
        else
        {
          if (m == 1)
          {
            log(0, "duplicate match " + p + " " + propNames[ip]);
          }
          m++;
          log(0, "duplicate match " + p + " " + propNames[ix]);
        }
      }
    }
  }
  if (m > 1)
  {
    return;
  }
  else if (m==0)
  {
    log(0, "no match " + p);
    return;
  }
  s = "{\"" + propNames[ip] + "\":\"" + v + "\"}";
 
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, s);
  if (error) 
  {
    log(0, "deserializeJson() failed: ");
    log(0, error.f_str());
    return;
  }
  extractProps(doc, false);
}

// logger
// logging redefined later on
#if 0
void log(int level, String s)
{
  if (level > logLevel) return;
  Serial.println(s);
  #if HAVEWIFI
  telnet.println(s);
  #endif
}
void log(int level)
{
  if (level > logLevel) return;
  Serial.println();
  #if HAVEWIFI
  telnet.println();
  #endif
}
void loga(int level, String s)
{
  if (level > logLevel) return;
  Serial.print(s);
  #if HAVEWIFI
  telnet.print(s);
  #endif
}
#endif

void checkSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
      case 0:
        break;
      case '\r':
        break;
      case '\n':
        buffer[bufPtr++] = 0;
        processCommandLine(String(buffer));
        bufPtr = 0;
        break;
      default:
        if (bufPtr < BUFFLEN -1)
        {
          buffer[bufPtr++] = c;
        }
        break;
    }
  } 
}
// for counting restarts - write to eprom
struct eeStruct
{
  unsigned int writes = 0;
  unsigned int wdtRestart = 0;
  unsigned int isrRestart = 0;
  unsigned int panicRestart = 0;
  unsigned int otherRestart = 0;
};

eeStruct eeData;

// for reliability stats from each restart
int getGatewayCount = 0;
int getWifiCount = 0;
int reConnWifiCount = 0;
unsigned long mqttDiscMs = 0;
int mqttConnCount = 0;
int mqttDiscCount = 0;
int mqttConnFailCount = 0;
int mqttConnTimeMs = 0;
int mqttSendCount = 0;
int mqttInCount = 0;

void eeDataReset()
{
  eeData.writes = 0;
  eeData.isrRestart = 0;
  eeData.wdtRestart = 0;
  eeData.panicRestart = 0;
  eeData.otherRestart = 0;
}

void eepromInit()
{
  int eeSize = sizeof(eeData);
  EEPROM.begin(eeSize);
  log(0, "ee size "+ String(eeSize));
}
void eepromWrite()
{
  eeData.writes++;
  if (eeData.writes > eeWriteLimit)
  {
    log(0, "eeprop Write limit..");
    return;
  }
  EEPROM.put(0, eeData);
  EEPROM.commit();
  log(0, "eewrite:" + String(eeData.writes));
}
void eepromRead()
{
  EEPROM.get(0, eeData);
  log(0, "eeWrites: " + String(eeData.writes));
}
void checkRestartReason()
{
  eepromRead();
  int resetReason = esp_reset_reason();
  log(0, "ResetReason: " + String(resetReason));
  switch (resetReason)
  {
    case ESP_RST_POWERON:
      return;// ok
    case ESP_RST_PANIC:
      eeData.panicRestart++;
      break;
    case ESP_RST_INT_WDT:
      eeData.isrRestart++;
      break;
    case ESP_RST_TASK_WDT:
      eeData.wdtRestart++;
      break;
    default:
      eeData.otherRestart++;
      break;
  }
  eepromWrite();
  logResetStats();
}

void logResetStats()
{
  log(0, "eeWrites: " + String(eeData.writes));
  log(0, "panic: " + String(eeData.panicRestart));
  log(0, "taskwd: " + String(eeData.wdtRestart));
  log(0, "irswd: " + String(eeData.isrRestart));
  log(0, "other: " + String(eeData.otherRestart));
  #if HAVEWIFI
  log(0, "getGateway: " + String(getGatewayCount));
  log(0, "getWifi: " + String(getWifiCount));
  log(0, "reconnWifi: " + String(reConnWifiCount));
  log(0, "mqttConn: " + String(mqttConnCount));
  log(0, "mqttConnT: " + String(mqttConnTimeMs));
  log(0, "mqttDisc: " + String(mqttDiscCount));
  log(0, "mqttFail: " + String(mqttConnFailCount));
  log(0, "mqttSend: " + String(mqttSendCount));
  log(0, "mqttIn: " + String(mqttInCount));
  log(0, "wfChannel: " + String(WiFi.channel()));
  log(0, "wfRSSI: " + String(WiFi.RSSI()));
  log(0, "wfPower: " + String(WiFi.getTxPower()));
  #endif
}
// ----------- end properties include 2 --------------------

// ----------- custom properties modify section  --------------------
// extract and add properties to json doc
// customize this for props expected and data types - watch with bools

void extractProps(JsonDocument &doc, bool reportMissing)
{
  propNamesSz = 0;
  log(0, "setting properties:");
  String propName;
  propName = logLevelN; if (checkProp(doc, propName, reportMissing)) logLevel = doc[propName].as<int>();
  propName = eeWriteLimitN; if (checkProp(doc, propName, reportMissing)) eeWriteLimit = doc[propName].as<int>();
  propName = wifiSsidN; if (checkProp(doc, propName, reportMissing)) wifiSsid = doc[propName].as<String>();
  propName = wifiPwdN;  if (checkProp(doc, propName, reportMissing)) wifiPwd = doc[propName].as<String>();
  propName = wifiIp4N;  if (checkProp(doc, propName, reportMissing)) wifiIp4 = doc[propName].as<byte>();
  propName = mqttPortN; if (checkProp(doc, propName, reportMissing)) mqttPort = doc[propName].as<int>();
  propName = mqttIp4N;  if (checkProp(doc, propName, reportMissing)) mqttIp4 = doc[propName].as<byte>();
  propName = telnetPortN;if (checkProp(doc, propName, reportMissing)) telnetPort = doc[propName].as<int>();
  propName = mqttIdN;   if (checkProp(doc, propName, reportMissing)) mqttId = doc[propName].as<String>();
  propName = unitIdN;   if (checkProp(doc, propName, reportMissing)) unitId = doc[propName].as<int>();
  propName = wdTimeoutN;if (checkProp(doc, propName, reportMissing)) wdTimeout = max(doc[propName].as<int>(),30);
  // these just for adjustment
  propName = restartN; if (checkProp(doc, propName, false)) ESP.restart();
  propName = writePropsN; if (checkProp(doc, propName, false)) writeProps(false);
  propName = propNameN; if (checkProp(doc, propName, false)) propNameA = doc[propName].as<String>();
  propName = propValueN;if (checkProp(doc, propName, false)) propValue = doc[propName].as<String>();  // picked up in checkState()

  // ----- start custom extract -----
  propName = telnetFWPortN;if (checkProp(doc, propName, reportMissing)) telnetFWPort = doc[propName].as<int>();
  // ----- end custom extract -----
}

// adds props for props write - customize
void addProps(JsonDocument &doc)
{
  doc[logLevelN] = logLevel;
  doc[eeWriteLimitN] = eeWriteLimit;
  doc[wifiSsidN] = wifiSsid;
  doc[wifiPwdN] = wifiPwd;
  doc[wifiIp4N] = wifiIp4;
  doc[mqttIp4N] = mqttIp4;
  doc[telnetPortN] = telnetPort;
  doc[mqttPortN] = mqttPort;
  doc[mqttIdN] = mqttId;
  doc[unitIdN] = unitId;
  doc[wdTimeoutN] = wdTimeout;

  // ----- start custom add -----
  doc[telnetFWPortN] = telnetFWPort;
  // ----- end custom add -----
}

// custom modified section for props control and general commands
void processCommandLine(String cmdLine)
{
  if (cmdLine.length() == 0)
  {
    return;
  }
  
  switch (cmdLine[0])
  {
    case 'h':
    case '?':
      log(0, "v:version, w:writeprops, d:dispprops, l:loadprops p<prop>=<val>: change prop, r:restart");
      log(0, "s:showstats, z:zerostats, n:dns, 0,1,2:loglevel = " + String(logLevel));
      return;
    case 'w':
      writeProps(false);
      return;
    case 'd':
      writeProps(true);
      return;
    case 'l':
      readProps();
      return;
    case 'p':
      adjustProp(cmdLine.substring(1));
      return;
    case 'r':
      ESP.restart();
      return;
    case 'v':
      loga(0, module);
      loga(0, " ");
      log(0, buildDate);
      return;
    case 'z':
      eeDataReset();
      return;
    case 's':
      logResetStats();
      return;
    case 't':
      {
        tm now = rtc.getTimeStruct();
        log(0, "ESP time: " + dateTimeIso(now));
        return;
      }
    case 'n':
      logDns();
      break;
    case '0':
      logLevel = 0;
      log(0, " loglevel=" + String(logLevel));
      return;
    case '1':
      logLevel = 1;
      log(0, " loglevel=" + String(logLevel));
      return;
    case '2':
      logLevel = 2;
      log(0, " loglevel=" + String(logLevel));
      return;

  // ----- start custom cmd -----

  // ----- end custom cmd -----
    default:
      log(0, "????");
      return;
  }
}
// ----------- end custom properties modify section  --------------------

// ------------ start wifi and mqtt include section 2 ---------------
IPAddress localIp;
IPAddress gatewayIp;
IPAddress primaryDNSIp;
String mqttMoniker;

int recoveries = 0;
unsigned long seconds = 0;
unsigned long lastSecondMs = 0;
unsigned long startRetryDelay = 0;
int long retryDelayTime = 10;  // seconds
bool retryDelay = false;


// state engine
#define START 0
#define STARTGETGATEWAY 1
#define WAITGETGATEWAY 2
#define STARTCONNECTWIFI 3
#define WAITCONNECTWIFI 4
#define ALLOK 5

String stateS(int state)
{
  if (state == START) return "start";
  if (state == STARTGETGATEWAY) return "startgetgateway";
  if (state == WAITGETGATEWAY) return "waitgetgateway";
  if (state == STARTCONNECTWIFI) return "startconnectwifi";
  if (state == WAITCONNECTWIFI) return "waitconnectwifi";
  if (state == ALLOK) return "allok";
  return "????";
}
int state = START;

unsigned long startWaitWifi;

bool startGetGateway()
{
  getGatewayCount++;
  WiFi.disconnect();
  delay (500);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  log(1, "Start wifi dhcp" + wifiSsid + " " + wifiPwd);
  WiFi.begin(wifiSsid, wifiPwd);
  startWaitWifi = millis();
  return true;
}
bool startWifi()
{
  getWifiCount++;
  WiFi.disconnect();
  WiFi.setAutoReconnect(true);
  delay (500);
  unsigned long startWaitWifi = millis();
  WiFi.mode(WIFI_STA);
  if (wifiIp4 == 0)
  {
    log(1, "Start wifi dhcp: " + wifiSsid + " " + wifiPwd);
  }
  else
  {
    IPAddress subnet(255, 255, 0, 0);
    IPAddress fixedIp = localIp;
    fixedIp[3] = wifiIp4;
    if (!WiFi.config(fixedIp, gatewayIp, subnet, primaryDNSIp, primaryDNSIp)) 
    {
      log(1, "STA Failed to configure");
      return false;
    }
    log(1, "Start wifi fixip: " + wifiSsid + " " + wifiPwd);
  }
  WiFi.begin(wifiSsid, wifiPwd);
  return true;
}

int waitWifi()
{
  // 0=waiting, <0=fail, >0=ok
  unsigned long et = millis() - startWaitWifi;
  if (WiFi.status() == WL_CONNECTED)
  {
    localIp = WiFi.localIP();
    gatewayIp = WiFi.gatewayIP();
    primaryDNSIp = WiFi.dnsIP();
    log(1, "connected, t=" + String(et) + ", local=" + localIp.toString() + " gateway=" + gatewayIp.toString() + " dns=" + primaryDNSIp.toString());
    reConnWifiCount--;
    return 1;
  }
  
  if (et > 30000)
  {
    log(1, "... fail wifi connection timeout");
    return -1;
  }
  return 0;
}


// dns support
struct dnsIsh
{
  bool used = false;
  String name;
  String ip;
  int timeout;
};
dnsIsh dnsList[DNSSIZE];
unsigned long dnsVersion = 0;
unsigned long lastSynchTime = 0;

void logDns()
{
  log(0, "dns v=" + String(dnsVersion));
  for (int ix = 0; ix < DNSSIZE; ix++)
  {
    if (dnsList[ix].used && dnsList[ix].timeout > 0)
    {
      log(0, String(ix) + " " + dnsList[ix].name + " " + dnsList[ix].ip);
    }
  }
}

String dnsGetIp(String name)
{
  for (int ix = 0; ix < DNSSIZE; ix++)
  {
    if (dnsList[ix].used && dnsList[ix].name.startsWith(name))
    {
      return dnsList[ix].ip;
    }
  }
  return "";
}

// ESP32 Time
String formatd2(int i)
{
  if (i < 10)
  {
    return "0" + String(i);
  }
  return String(i);
}
String dateTimeIso(tm d)
{
  return String(d.tm_year+1900)+"-"+formatd2(d.tm_mon+1)+"-"+formatd2(d.tm_mday)+"T"+formatd2(d.tm_hour)+":"+formatd2(d.tm_min)+":"+formatd2(d.tm_sec);
}

// time and dns synch
void sendSynch()
{
  // will get updates if not in synch
  JsonDocument doc;
  doc["r"] = mqttMoniker + "/c/s";    // reply token
  doc["n"] = mqttId + String(unitId);
  doc["i"] = localIp.toString();
  doc["e"] = rtc.getEpoch();
  doc["v"] = dnsVersion;
  mqttSend("mb/s", doc);
}

void synchCheck()
{
  if (seconds - lastSynchTime > SYNCHINTERVAL/2)
  {
    lastSynchTime = seconds;
    sendSynch();
  }
}

void processSynch(JsonDocument &doc)
{
  unsigned long epoch = doc["e"].as<unsigned long>();
  if (epoch > 0)
  {
    rtc.setTime(epoch);
    tm now = rtc.getTimeStruct();
    log(2, "espTimeSet: " + dateTimeIso(now));
  }
  else
  {
    int timeAdjust = doc["t"].as<int>();
    if (timeAdjust != 0)
    {
      rtc.setTime(rtc.getEpoch() + timeAdjust);
      log(2, "espTimeAdjust: " + String(timeAdjust));
    }
  }
  long newDnsVersion = doc["v"].as<long>();
  if (newDnsVersion != 0)
  {
    dnsVersion  = newDnsVersion;
    log(2, "dns version: " + String(dnsVersion));
    for (int ix = 0; ix < DNSSIZE; ix++)
    {
      dnsList[ix].used = false;
    }
    for (int ix = 0; ix < DNSSIZE; ix++)
    {
      if (doc.containsKey("n" + String(ix)))
      {
        dnsList[ix].name = doc["n" + String(ix)].as<String>();
        dnsList[ix].ip = doc["i" + String(ix)].as<String>();
        dnsList[ix].used = true;
        dnsList[ix].timeout = 1;   // for consistency with dnsLog
        log(2, ".. " + dnsList[ix].name + " " + dnsList[ix].ip);
      }
      else
      {
        break;
      }
    }
  }
}



// ------------- mqtt section -----------------

// mqtt 
void setupMqttClient()
{
  IPAddress fixedIp = localIp;
  fixedIp[3] = mqttIp4;
  String server = fixedIp.toString();
  mqttClient.host = server;
  mqttClient.port = mqttPort;
  mqttMoniker = mqttId + "/" + String(unitId);
  mqttClient.client_id = mqttMoniker;
  String topic = mqttMoniker + "/c/#";
  mqttClient.subscribe(topic, &mqttMessageHandler);
  mqttSubscribeAdd();
  mqttClient.connected_callback = [] {mqttConnHandler();};
  mqttClient.disconnected_callback = [] {mqttDiscHandler();};
  mqttClient.connection_failure_callback = [] {mqttFailHandler();};
  mqttClient.begin();
}

// mqtt handlers
void mqttConnHandler()
{
  log(0, "MQTT connected: " + String(millis() - mqttDiscMs));
  sendSynch();
  mqttConnCount++;
}
void mqttDiscHandler()
{
  log(0, "MQTT disconnected");
  mqttDiscCount++;
  mqttDiscMs = millis();
}
void mqttFailHandler()
{
  log(0, "MQTT CONN FAIL");
  if (WiFi.isConnected())
  {
    mqttConnFailCount++;
  }
  mqttDiscMs = millis();
}
void mqttMessageHandler(const char * topicC, Stream & stream)
{
  mqttInCount++;
  String topic = String(topicC);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, stream);
  if (error) 
  {
    log(2, "deserialize fail: " +  String(error.f_str()) + " " + topic);
    return;
  }
  if (logLevel >=2)
  {
    String s;
    serializeJson(doc, s);
    log(2, "in:" + topic + " " + s);
   }
  if (topic.endsWith("/p"))
  {   
    // its a property setting
    adjustProp(doc["p"].as<String>());
  }
  else if (topic.endsWith("/s"))
  {   
    // its a synch response message
    processSynch(doc);
  }
  else
  {
    handleIncoming(topic, doc);
  }
}

void mqttSend(String topic, JsonDocument &doc)
{
  if (logLevel >=2)
  {
    String s;
    serializeJson(doc, s);
    log(2, "out: " + topic + " " + s);
  }
  if (WiFi.isConnected() && mqttClient.connected())
  {
    // publish using begin_publish()/send() API
    auto publish = mqttClient.begin_publish(topic, measureJson(doc));
    serializeJson(doc, publish);
    publish.send();
    mqttSendCount++;
  }
}

// ------------ telnet --------------
void setupTelnet(int port) 
{  
  telnet.stop();
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  telnet.onDisconnect(onTelnetDisconnect);
  telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet.onReconnect(onTelnetReconnect);
  telnet.onInputReceived(onTelnetInput);

  if (telnet.begin(port)) 
  {
    log(1, "telnet running");
  } 
  else 
  {
    log(1, "telnet start fail");
  }
}
#if 0
// telnet refedined later
void onTelnetConnect(String ip) 
{
  Serial.println("telnet connected");
  telnet.println("hello..");
}
void onTelnetDisconnect(String ip) 
{
  Serial.println("telnet disconnected");
}

void onTelnetReconnect(String ip) 
{
  Serial.println("telnet reconnected");
}

void onTelnetConnectionAttempt(String ip) 
{
  Serial.println("another telnet tried to connected - disconnecting");
  telnet.println("Another session trying. disconnecting you..");
  telnet.disconnectClient();
}

void onTelnetInput(String str) 
{
  processCommandLine(str);
}
#endif

void setRetryDelay()
{
  startRetryDelay = seconds;
  retryDelay = true;
  log(1, "retry delay....");
  recoveries++;
}
bool lastWifiState = false;
// state engine manager
void checkState()
{
  if (propValue != "")
  {
    adjustProp(propNameA + "=" + propValue);
    propValue = "";
  }  
  unsigned long nowMs = millis();
  while (nowMs - lastSecondMs > 1000)
  {
    seconds++;
    lastSecondMs+= 1000;
  }
  synchCheck();
  bool thisWifiState = WiFi.isConnected();
  if (thisWifiState != lastWifiState)
  {
    if (thisWifiState)
    {
      log(0, "WiFi Connected..");
      reConnWifiCount++;
    }
    else
    {
      log(0, "WiFi Disconnected..");
    }
    lastWifiState = thisWifiState;
  }
 
  if (retryDelay)
  {
    if (seconds - startRetryDelay < (retryDelayTime))
    {
      return; // retry wait
    }
    else
    {
      retryDelay = false;
    }
  }

  int res;
  switch (state)
  {
    case START:
      if (wifiIp4 == 0)
      {
        state = STARTCONNECTWIFI;    // dhcp ip
      }
      else
      {
        state = STARTGETGATEWAY;
      }
      return;
    case STARTGETGATEWAY:
      // only get gateway for fixed ip
      if (!startGetGateway())
      {
        setRetryDelay();
        return;
      }
      state = WAITGETGATEWAY;
      return;
    case WAITGETGATEWAY:
      res = waitWifi();
      if (res == 0)
      {
        return;
      }
      if (res < 0)
      {
        setRetryDelay();
        state = STARTGETGATEWAY;
        return;
      }
    case STARTCONNECTWIFI:
      if (!startWifi())
      {
        setRetryDelay();
        return;
      }
      state = WAITCONNECTWIFI;
      return;
    case WAITCONNECTWIFI:
      // mandatory we get connected once before proceeding
      res = waitWifi();
      if (res == 0)
      {
        return;
      }
      if (res < 0)
      {
        setRetryDelay();
        state = STARTCONNECTWIFI;
        return;
      }
      setupTelnet(telnetPort);
      setupMqttClient();
      state = ALLOK;

      return;

    case ALLOK:
      return;
  }
}
// ------------ end wifi and mqtt include section 2 ---------------

// ------------ start wifi and mqtt custom section 2 ---------------
void mqttSubscribeAdd()
{
  // use standard or custom handler
  // start subscribe add
  //String topic = "other/#";
  //mqttClient.subscribe(topic, &mqttMessageHandler);
  // end subscribe add
}
void handleIncoming(String topic, JsonDocument &doc)
{
  // start custom additional incoming
  // driven by topic - json data dummy
  // check for commands in
  if (topic == mqttMoniker + "/c/ip")
  {
    setGetExternalIp();
  }
  if (topic == mqttMoniker + "/c/ta")
  {
    setActivateFWTelnet();
  }
  if (topic == mqttMoniker + "/c/ts")
  {
    stopFWTelnet();
  }
  // end custom additional incoming
}
// ------------ end wifi and mqtt custom section 2 ---------------

// ---- start custom code --------------
// ------------ custom for telnetBridge ---------------------

#define BRIDGEBUFSIZE 100
char bridgeBuf[BRIDGEBUFSIZE];
int bridgeBufPtr = 0;
unsigned long lastTelnetActionAt = 0;
unsigned long lastMqttUpdateAt = 0;
unsigned int telnetTimeout = 60;
bool telnetFWActive = false;    // via firewall port - else standard port 23
int telnetPassword = -1;
String externalIp;
bool bridgeLocal = true;     // if telnet is for bridge itself
// things-to-do list on telnet input
bool bridgeVerifyPassword = false;
bool bridgeGetIp = false;
// .. and on bridgeloop()
bool doMqttUpdate = false;
bool doActivateFWTelnet = false;
bool doGetExternalIp = false;

// note here we log to Serial() not generic log() as log might be going to Telnet as well..
void bridgeLog(int level, String s)
{
  if (level <= logLevel)
  {
    Serial.println(s);
  }
}

void setGetExternalIp()
{
  doGetExternalIp = true;
}
void stopFWTelnet()
{
  lastTelnetActionAt = 0;
}
void setActivateFWTelnet()
{
  if (!telnetFWActive)
  {
  doActivateFWTelnet = true;
  }
}
// redifine log functions - original comment out
void log(int level, String s)
{
  if (level > logLevel) return;
  Serial.println(s);
  if (bridgeLocal)
  {
    telnet.println(s);
  }
}
void log(int level)
{
  if (level > logLevel) return;
  Serial.println();
  if (bridgeLocal)
  {
    telnet.println();
  }
}
void loga(int level, String s)
{
  if (level > logLevel) return;
  Serial.print(s);
  if (bridgeLocal)
  {
    telnet.print(s);
  }
}


// redefine
void onTelnetConnect(String ip) 
{
  bridgeLog(1, "telnet connect:" + ip);
  outClient.stop();
  bridgeLocal = false;
  if (!telnetFWActive)
  {
    // safe to talk
    telnet.println("local - enter destination..");
    bridgeGetIp = true;
  }
  else
  {
    bridgeVerifyPassword = true;
  }
}
// redefine
void onTelnetDisconnect(String ip) 
{
  bridgeLog(1, "telnet disconnected");
  outClient.stop();
}
// redefine
void onTelnetReconnect(String ip) 
{
  Serial.println("telnet reconnected");
}
// redefine
void onTelnetConnectionAttempt(String ip) 
{
  bridgeLog(1, "another connect attempt: "+ ip);
  telnet.println("another connect attempt: "+ ip);

}
// redefine on telnet input
void onTelnetInput(String str) 
{
  lastTelnetActionAt = seconds;
  if (bridgeLocal)
  {
    processCommandLine(str);
    return;
  }
  if (outClient.connected())
  {
    outClient.println(str);
    return;
  }
  if (bridgeVerifyPassword)
  {
    bridgeVerifyPassword = false;
    bridgeLog(1, "verify password: " + str);
    int pwd = str.toInt();
    if (pwd != telnetPassword)
    {
      telnet.disconnectClient();
      bridgeLog(1, "password fail - disconnect");
      lastTelnetActionAt = 0;   //kill connection
      return;
    }
    bridgeGetIp = true;
    return;
  }
  if (bridgeGetIp)
  {
    bridgeGetIp = false;
    bridgeLog(1, "get ip: " + str);
    String destIpS;
    if (str == "")
    {
      bridgeLocal = true;   // bridging to local
      bridgeLog(1, "bridge local");
      telnet.println("local");
      return;
    }
    if (str.length() > 10  && isDigit(str[0]))
    {
      destIpS = str;    //looks like a IP
    }
    else
    {
      destIpS = dnsGetIp(str);
      if (destIpS == "")
      {
        bridgeLog(1, "unresolved ip");
        telnet.println("not in dns..try again");
        bridgeGetIp = true;
        return;
      }
    }
    if (destIpS == localIp.toString())
    {
      bridgeLocal = true;       // bridging to local
      bridgeLog(1, "bridge local");
      telnet.println("local");
      return;
    }

    IPAddress destIp;
    destIp.fromString(destIpS);
    telnet.println("bridging to " + destIp.toString());
    bridgeLog(1, "connect to: " + destIp.toString());
    if (!outClient.connect(destIp, telnetPort))
    {
      telnet.println("faled to connect..try again");
      bridgeLog(1, "connect to failed: " + destIp.toString());
      bridgeGetIp = true;
      return;
    }
    telnet.println(">" + destIpS);
    bridgeLog(1, "connected ok..");
  }
}

void bridgeLoop()
{
  if (telnetFWActive && seconds - lastMqttUpdateAt >= 5)
  {
    doMqttUpdate = true;
  }
  if (doGetExternalIp)
  {
    doGetExternalIp = false;
    getExternalIp();
    doMqttUpdate = true;
  }
  if (doActivateFWTelnet)
  {
    doActivateFWTelnet = false;
    bridgeLog(1, "restarting Telnet listener to " + String(telnetFWPort));
    telnet.stop();
    randomSeed(millis());
    telnetPassword = random(8999) + 1000;
    setupTelnet(telnetFWPort);
    telnetFWActive = true;
    lastTelnetActionAt = seconds;
    doMqttUpdate = true;
  }
  if (doMqttUpdate)
  {
    doMqttUpdate = false;
    lastMqttUpdateAt = seconds;
    JsonDocument doc;
    doc["ip"] = externalIp;
    doc["ta"] = telnetFWActive;
    if (telnetFWActive)
    {
      doc["tp"] = telnetPassword;
      doc["tt"] = telnetTimeout - (seconds - lastTelnetActionAt);
    }
    else
    {
      doc["tp"] = "...";
      doc["tt"] = "...";
    }
    String topic = mqttMoniker + "/d";
    mqttSend(topic, doc);           // local
    mqttSend("mb/f/"+ topic, doc);     // and remote
  }
  if (telnetFWActive && (seconds - lastTelnetActionAt > telnetTimeout))
  {
    if (telnet.isConnected())
    {
      telnet.println("shut down telnet");
      telnet.disconnectClient();
    }
    telnetFWActive = false;
    telnet.stop();
    log(1, "firewall telnet timeout - shutdown");
    bridgeLog(1, "FW Telnet timeout restarting listener to " + String(telnetPort));
    setupTelnet(telnetPort);
    doMqttUpdate = true;
  }
  if (!outClient.connected())
  {
    return;
  }
  while (outClient.available())
  {
    char c = outClient.read();
    switch (c)
    {
      case 0:
        break;
      case '\r':
        break;
      case '\n':
        bridgeBuf[bridgeBufPtr++] = 0;
        telnet.println(String(bridgeBuf));
        bridgeBufPtr = 0;
        break;
      default:
        if (bridgeBufPtr < BRIDGEBUFSIZE -1)
        {
          bridgeBuf[bridgeBufPtr++] = c;
        }
        break;
    }
  } 
}
void getExternalIp()
{
  const char * textURL = "http://api.ipify.org";
  if (WiFi.status() == WL_CONNECTED) 
  {
    WiFiClient client;
    HTTPClient http;
    String payload = "";
    http.begin(client, textURL);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) 
    {
      bridgeLog(1, "HTTP Response code: " + String(httpResponseCode));
      payload = http.getString();
      externalIp = payload;
      bridgeLog(1, "Extrnam IP: " + externalIp);
    }
    else 
    {
      bridgeLog(1, "Error code: " +String(httpResponseCode));
    }
    http.end();
  }
}

// ------------ end custom for telnetBridge ---------------------
// ---- end custom code --------------

void setup()
{
  loga(1, module);
  loga(1, " ");
  log(1, buildDate);
  Serial.begin(115200);
  checkRestartReason();
  mountSpiffs();
  readProps();
  // customSetup();
  esp_task_wdt_config_t config;
  int wdt = max(wdTimeout*1000,2000);
  config.timeout_ms = max(wdTimeout*1000,2000);;
  config.idle_core_mask = 3; //both cores
  config.trigger_panic = true;
  esp_task_wdt_reconfigure(&config);
  esp_task_wdt_add(NULL);
}

void loop()
{
  esp_task_wdt_reset();
  telnet.loop();
  mqttClient.loop();
  bridgeLoop();
  checkSerial();
  checkState();

  delay(1);
}
