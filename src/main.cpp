#include <Arduino.h>

// for SSL Connection
#include <time.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Define Slave I2C Address
#define SLAVE_ADDR 9

// Define Slave answer size
#define ANSWERSIZE 50

#define CMD_PURCHASE "1"
#define CMD_RELOAD "2"

enum STATESM
{
  STATE_INIT,
  STATE_WAIT_NEW_CARD,
  STATE_CARD_DETECTED,
  STATE_CARD_VALIDATING,
  STATE_CARD_VALIDATING_WAIT,
  STATE_CARD_VALIDATED,
  STATE_DISPENSING,
  STATE_DISPENSING_DONE,
  STATE_MAX
};
STATESM currentState = STATE_INIT;

char DATETIME[] = "2022/01/01 00:00:00";
char receivedData[40];
String csCardID = "";
char csCMD;
float amount, currentBalance;

void FormatDateTime(struct tm t)
{
  // struct tm t2;
  sprintf(DATETIME, "%04d/%02d/%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

bool connectToWiFi()
{
  Serial.println("+connectToWiFi()");
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Connecting to wifi...");
    int failedAttemptsNum = 0;
    while (WiFi.status() != WL_CONNECTED && failedAttemptsNum <= 10)
    {
      delay(1000);
      Serial.print(".");
    }

    if (failedAttemptsNum >= 10)
    {
      Serial.println("Failed to connect to WiFi.");
      return false;
    }
  }

  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  display.println("WiFi connected");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  return true;
}

void setClock()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");

  display.println("Waiting NTP...");
  display.display();

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");

  struct tm timeinfo;
  // struct tm *timeinfo;
  localtime_r(&now, &timeinfo);

  FormatDateTime(timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
  Serial.println(DATETIME);

  // display.println(asctime(&timeinfo));
  display.println(DATETIME);
  display.display();
}

// Dummy
bool IsCardValid(String csID)
{
  Serial.println("Validating card...");

  BearSSL::WiFiClientSecure httpsclient;
  BearSSL::X509List list(cert);
  httpsclient.setTrustAnchors(&list);
  HTTPClient http;

  String csURL = API_HOST + "/" + csID;
  Serial.println(csURL);
  if (http.begin(httpsclient, csURL))
  {
    Serial.println("Sending request...");
    http.addHeader("Authorization", AUTHURIZATION_STR);
    int httpCode = http.GET();
    if (httpCode > 0)
    {
      Serial.print("HttpResponseCode: ");
      Serial.println(httpCode);
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
      {
        String payload = http.getString();
        Serial.println("ResponseText: ");

        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        serializeJsonPretty(doc, Serial);
        Serial.println("");
        JsonObject obj = doc.as<JsonObject>();

        // updated display
        display.print("Balance: ");
        currentBalance = obj["balance"];
        display.println(currentBalance);
        display.display();
        delay(2000);

        // if (doc["cardID"] == "7B1CB128")
        if (currentBalance > 10)
        {
          return true;
        }
        else
        {
          return false;
        }
      }
      return false;
    }
    else
    {
      Serial.println("Error");
      return false;
    }
  }
  else
  {
    return false;
  }
}

void SwitchState(STATESM sm)
{
  currentState = sm;
}

void GetCardDetails()
{
  Serial.println("+GetCardDetails()");

  BearSSL::WiFiClientSecure httpsclient;
  BearSSL::X509List list(cert);
  httpsclient.setTrustAnchors(&list);
  HTTPClient http;

  if (http.begin(httpsclient, API_HOST))
  {
    int httpCode = http.GET();
    Serial.println(httpCode);
    if (httpCode > 0)
    {
      Serial.println("HTTP GET -> " + httpCode);
      if (httpCode == HTTP_CODE_OK)
      {
        String payload = http.getString();
        Serial.println("PAYLOAD: ");
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        serializeJsonPretty(doc, Serial);
      }
    }
    else
    {
      Serial.println("Unable to connect to REST API ret: ");
    }
  }
  else
  {
    Serial.println("Unable to connect to REST API.");
  }
  http.end();

  Serial.println("-GetCardDetails()");
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  // Display Adafruit Logo
  // display.display();
  // delay(2000); // Pause for 2 seconds

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(1, 1);

  display.println("Init wifi..");
  display.display();
  delay(1000);
  // Serial.println("Wifi init...");

  if (connectToWiFi())
  {
    delay(1500);
    Serial.println("Syncing clock...");
    display.clearDisplay();
    display.setCursor(1, 1);
    display.println("Synching clock");
    display.display();
    setClock();
  }
  else
  {
    display.println("No wifi connection!");
    display.display();
    Serial.println("Not connected to Wifi");
  }

  // Initialize I2C communications as Master
  Wire.begin();

  delay(5000);
  // GetCardDetails();
}

bool ParseCommand()
{
  Serial.println("Parsing data...");
  Serial.println(receivedData);

  // cardID 8 Chars
  char token[15];

  if (strlen(receivedData) > 8 && receivedData[8] == '|')
  {
    strncpy(token, receivedData, 8);
    token[8] = '\0';
    csCardID = token;
    Serial.print("CardID:  ");
    Serial.println(csCardID);

    csCMD = receivedData[9];

    Serial.print("CMD:  ");
    Serial.println(csCMD);

    // get value
    token[0] = '\0';
    amount = 0;
    for (unsigned int i = 0; i < sizeof(receivedData); i++)
    {
      if (receivedData[10 + i] != '\0')
      {
        token[i] = receivedData[10 + i];
      }
      else
      {
        break;
      }
    }
    if (token[0] != '\0')
    {
      token[sizeof(token) - 1] = '\0';
      amount = atof(token);
    }
    Serial.print("Amount: ");
    Serial.println(amount);
    return true;
  }
  else
  {
    Serial.println("Invalid data.");
    return false;
  }
}

bool RequestCommand()
{
  Wire.requestFrom(SLAVE_ADDR, ANSWERSIZE);

  int i = 0;
  receivedData[0] = '\0';
  while (Wire.available())
  {
    char c = Wire.read();
    if (isAlphaNumeric(c) || c == '|')
    {
      // response += c;
      receivedData[i] = c;
      i++;
    }
  }

  receivedData[i] = '\0';
  if (i > 0)
  {
    // String response(receivedData);
    // return response;
    return true;
  }
  return false;
}

String response;

void loop()
{
  switch (currentState)
  {
  case STATE_INIT:
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Waiting for card scan");
    display.display();
    SwitchState(STATE_WAIT_NEW_CARD);
    break;
  case STATE_WAIT_NEW_CARD:
    delay(100);
    if (RequestCommand())
    {
      if (receivedData[0] == '\0' || strcmp(receivedData, "X") == 0)
      {
        // do nothing
      }
      else
      {
        if (ParseCommand())
        {
          // valid data
          SwitchState(STATE_CARD_DETECTED);
        }
      }
    }
    break;
  case STATE_CARD_DETECTED:
    // delay(250);
    display.println("-> " + csCardID);
    display.display();

    SwitchState(STATE_CARD_VALIDATING);
    break;
  case STATE_CARD_VALIDATING:
    if (IsCardValid(csCardID))
    {
      Serial.println("Valid card");
      display.println("Valid card");
      display.display();
      Wire.beginTransmission(SLAVE_ADDR);
      Wire.write("OK");
      Wire.endTransmission();

      currentState = STATE_CARD_VALIDATING_WAIT;
    }
    else
    {
      // Serial.println("Invalid card");
      // display.println("Invalid card");
      // display.display();
      Wire.beginTransmission(SLAVE_ADDR);
      Wire.write("ERR");
      Wire.endTransmission();

      currentState = STATE_INIT;
    }
    delay(1000);

    break;
  case STATE_CARD_VALIDATING_WAIT:
    delay(500);
    if (RequestCommand())
    {
      if (receivedData[0] == '\0' || strcmp(receivedData, "X") == 0)
      {
      }
      else
      {
        // start of dispense
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Dispensing...");
        display.display();

        currentState = STATE_DISPENSING;
      }
    }
    break;
  case STATE_DISPENSING:
    delay(1000);
    if (RequestCommand())
    {
      if (receivedData[0] != '\0' && strcmp(receivedData, "X3") == 0)
      {
        // Done dispensing
        display.println("Done...");
        delay(1000);
        currentState = STATE_INIT;
      }
    }
    break;
  }
  delay(250);
}