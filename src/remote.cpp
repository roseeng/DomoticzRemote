#include <SPI.h>
#include <Arduino.h>
#include <xpt2046.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "pins.h"
#include "remote.h"
#include "site.h"
#include "interval.h"

bool ConnectToWifi(String ssid, String password);
void WiFiEvent(WiFiEvent_t event);
void wifiInit(void);
int pollDomoticz();
void toggleDomoticz(int switch1or2);

WiFiClient wifiClient;
HTTPClient client;

#if __has_include("data.h")
#include "data.h"
#define USE_CALIBRATION_DATA 1
#endif

TFT_eSPI tft = TFT_eSPI();

XPT2046 touch = XPT2046(SPI, TOUCHSCREEN_CS_PIN, TOUCHSCREEN_IRQ_PIN);

#if USE_CALIBRATION_DATA
touch_calibration_t calibration_data[4];
#endif

void setBrightness(uint8_t value)
{
    static uint8_t steps = 16;
    static uint8_t _brightness = 0;

    if (_brightness == value)
    {
        return;
    }

    if (value > 16)
    {
        value = 16;
    }
    if (value == 0)
    {
        digitalWrite(BK_LIGHT_PIN, 0);
        delay(3);
        _brightness = 0;
        return;
    }
    if (_brightness == 0)
    {
        digitalWrite(BK_LIGHT_PIN, 1);
        _brightness = steps;
        delayMicroseconds(30);
    }
    int from = steps - _brightness;
    int to = steps - value;
    int num = (steps + to - from) % steps;
    for (int i = 0; i < num; i++)
    {
        digitalWrite(BK_LIGHT_PIN, 0);
        digitalWrite(BK_LIGHT_PIN, 1);
    }
    _brightness = value;
}

void setup()
{
    pinMode(PWR_EN_PIN, OUTPUT);
    digitalWrite(PWR_EN_PIN, HIGH);

    // Init serial
    Serial.begin(115200);
    Serial.println("Hello T-HMI");
    delay(1000);

    // Init power button (magnet)
    pinMode(PWR_EN_PIN, OUTPUT);
    digitalWrite(PWR_EN_PIN, HIGH);

    // Init touch
#if USE_CALIBRATION_DATA
    data_init();
    data_read(calibration_data);
#endif

    SPI.begin(TOUCHSCREEN_SCLK_PIN, TOUCHSCREEN_MISO_PIN, TOUCHSCREEN_MOSI_PIN);
    touch.begin(240, 320);
#if USE_CALIBRATION_DATA
    touch.setCal(calibration_data[0].rawX, calibration_data[2].rawX, calibration_data[0].rawY, calibration_data[2].rawY, 240, 320); // Raw xmin, xmax, ymin, ymax, width, height
#else
    touch.setCal(1788, 285, 1877, 311, 240, 320); // Raw xmin, xmax, ymin, ymax, width, height
    Serial.println("Use default calibration data");
#endif
    touch.setRotation(0);

    // Init TFT
    tft.begin();
    tft.setRotation(0);
    tft.setSwapBytes(true);

    // Set backlight level, range 0 ~ 16
    setBrightness(16);
    tft.fillScreen(TFT_BLACK);

    delay(3000);

    wifiInit();
}

#define WAIT 50
String s;
Interval switchInterval;

void loop()
{
    if (touch.pressed())
    {
        tft.fillRect(40, 100, 200, 40, TFT_BLACK);

        s = "RAW X: ";
        s += touch.RawX();
        s += " RAW Y: ";
        s += touch.RawY();
        Serial.println(s);
        tft.drawString(s, 40, 100, 2);

        s = "X: ";
        s += touch.X();
        s += " Y: ";
        s += touch.Y();
        Serial.println(s);
        tft.drawString(s, 40, 120, 2);

        tft.fillCircle(touch.X(), touch.Y(), 20, TFT_RED);

        toggleDomoticz((touch.Y() > 150) ? 2 : 1);  
    }

    delay(WAIT);

    if (switchInterval.Every(30)) {
        int newStatus = pollDomoticz();  
    }
}

bool ConnectToWifi(String ssid, String password)
{
    Serial.printf("Connecting to %s... ", ssid);
    WiFi.begin(ssid, password);
    int retry = 20;
    while (WiFi.status() != WL_CONNECTED && retry-- > 0)
    {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED)
        return false;

    Serial.print("  CONNECTED to ");
    Serial.println(ssid);
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

int wifiSite = -1;

void wifiInit(void)
{
    WiFi.disconnect(true);
    delay(100);

    Serial.println(F("WiFi: Set mode to STA"));
    Serial.println(F("Connecting to WiFi"));

    WiFi.mode(WIFI_AP_STA);
    WiFi.onEvent(WiFiEvent);

    if (ConnectToWifi(sites[0].ssid, sites[0].password))
    {
        wifiSite = 0;
    }
    else if (ConnectToWifi(sites[1].ssid, sites[1].password))
    {
        wifiSite = 1;
    }
    else
    {
        Serial.println("Failed to connect to wifi!");
        return;
    }
    
}

void WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);

    switch (event)
    {
    case ARDUINO_EVENT_WIFI_READY:
        Serial.println("WiFi interface ready");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        Serial.println("Completed scan for access points");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        Serial.println("WiFi client started");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        Serial.println("WiFi clients stopped");
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("Connected to access point");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        // TODO: Lägg till kod som gör reconnect mot rätt nätverk
        //WiFi.begin(ssid, password);
        Serial.println("Disconnected from WiFi access point");
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        Serial.println("Authentication mode of access point has changed");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("Obtained IP address: ");
        Serial.println(WiFi.localIP());
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        Serial.println("Lost IP address and IP address is reset to 0");
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.println("WiFi access point started");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        Serial.println("WiFi access point  stopped");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("Client connected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("Client disconnected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("Assigned IP address to client");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        Serial.println("Received probe request");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        Serial.println("AP IPv6 is preferred");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        Serial.println("STA IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        Serial.println("Ethernet IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_START:
        Serial.println("Ethernet started");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("Ethernet stopped");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("Ethernet connected");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("Ethernet disconnected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.println("Obtained IP address");
        break;
    default:
        break;
    }
}

int pollDomoticz()
{
  char switchUrl[256];
  
  if (wifiSite == -1)
  {
    Serial.println("Not yet connected.");
    return -1;
  }

  sprintf(switchUrl, "http://%s:%d/json.htm?type=command&param=getdevices&rid=%d", sites[wifiSite].host, sites[wifiSite].port, sites[wifiSite].switchIdx);
  Serial.printf("Calling Domoticz: %s\n", switchUrl);

  client.begin(switchUrl);
  client.addHeader("Content-Type", "application/json");
  int statusCode = client.GET();
  String responseBody = client.getString();
  // client.sendBasicAuth("username", "password");
  client.end();

  if (statusCode != 200) {
    Serial.printf("Response status: %d\n", statusCode);
    return -1;
  }

  /*
  int statusCode = client.returnError
  String responseBody = client->responseBody();
  Serial.print("Response status: ");
  Serial.println(statusCode);

  if (statusCode != 200) {
    Serial.println("Response:\n" + responseBody);
    return -1;
  }
  */
  DynamicJsonDocument doc(3072);

  DeserializationError error = deserializeJson(doc, responseBody);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return -1;
  }

  JsonObject result_0 = doc["result"][0];
  const char* status = result_0["Data"];

  Serial.print("Switchen är: ");
  Serial.println(status);

  return status[1] == 'f' ? 0 : 1; 
}

void toggleDomoticz(int switch1or2)
{
  char switchUrl[256];
  
  if (wifiSite == -1)
  {
    Serial.println("Not yet connected.");
    return;
  }

  int idx = (switch1or2 == 1) ? sites[wifiSite].switchIdx : sites[wifiSite].switch2Idx;
  sprintf(switchUrl, "http://%s:%d/json.htm?type=command&param=switchlight&idx=%d&switchcmd=Toggle", sites[wifiSite].host, sites[wifiSite].port, idx);
  Serial.printf("Calling Domoticz: %s\n", switchUrl);

  client.begin(switchUrl);
  client.addHeader("Content-Type", "application/json");
  int statusCode = client.GET();
  String responseBody = client.getString();
  // client.sendBasicAuth("username", "password");
  client.end();

  if (statusCode != 200) {
    Serial.printf("Response status: %d\n", statusCode);
    return;
  }

  DynamicJsonDocument doc(3072);

  DeserializationError error = deserializeJson(doc, responseBody);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

 // JsonObject result_0 = doc["message"];
  const char* status = doc["message"];

  Serial.print("Resultat: ");
  Serial.println(status);

  return; 
}

