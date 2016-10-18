#include <ESP8266WiFi.h>
#include <ws2812_i2s.h>
#include "ntp.h"


// Interval in ms to check sync
#define ntpSyncInterval (600000)
unsigned long lastNTPRequest = 0;
unsigned long lastNTPSync = 0;

#define serialStatusInterval (10000)
unsigned long lastSerialStatus = 0;

#define lightUpdateInterval (200)
unsigned long lastLightUpdate = 0;
char alarmOn = 0;
unsigned long sunriseStartTime = 0;

#define NUM_LEDS 120
static WS2812 ledstrip;
static Pixel_t pixels[NUM_LEDS];

// Define these in the config.h file
//#define WIFI_SSID "yourwifi"
//#define WIFI_PASSWORD "yourpassword"
//#define WEBSERVER_USERNAME "something"
//#define WEBSERVER_PASSWORD "something"
#include "config.h"

#define DEVICE_NAME "partycat"

#define RED_LED_PIN 16
#define WHITE_PIN 14


#define SETTINGS_VERSION "s4d2"
struct Settings {
  int onTime;
  int offTime;
  int timeZone;
} settings = {
  23400,
  28800,
  -7
};

#include "libdcc/webserver.h"
#include "libdcc/settings.h"
#include "colourscheme.h"

String formatSettings() {
  return \
    String("onTime=") + String(settings.onTime) + \
    String(",offTime=") + String(settings.offTime) + \
    String(",timeZone=") + String(settings.timeZone);
}

void handleSettings() {
  REQUIRE_AUTH;

  for (int i=0; i<server.args(); i++) {
    if (server.argName(i).equals("onTime")) {
      settings.onTime = server.arg(i).toFloat();
    } else if (server.argName(i).equals("offTime")) {
      settings.offTime = server.arg(i).toFloat();
    } else if (server.argName(i).equals("timeZone")) {
      settings.timeZone = server.arg(i).toFloat();
    } else {
      Serial.println("Unknown argument: " + server.argName(i) + ": " + server.arg(i));
    }
  }

  saveSettings();

  String msg = String("Settings saved: ") + formatSettings();
  Serial.println(msg);
  server.send(200, "text/plain", msg);
}

void setup() {
  pinMode(WHITE_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  Serial.begin(115200);

  ledstrip.init(NUM_LEDS);
  setColour(0, 0, 0);
  setStatus(255, 0, 0);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Internet Lightbox", WEBSERVER_PASSWORD);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  server.on("/settings", handleSettings);
  server.on("/restart", handleRestart);
  server.on("/status", handleStatus);
  server.on("/colour", handleColour);
  server.on("/white", handleWhite);
  server.on("/flash", handleFlash);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}


void handleFlash() {
  int times=0;

  for (int i=0; i<server.args(); i++) {
    if (server.argName(i).equals("times")) {
      times = server.arg(i).toInt();
    } else {
      Serial.println("Unknown argument: " + server.argName(i) + ": " + server.arg(i));
    }
  }

  flash(times);

  server.send(200, "text/plain", "ok");
}

void handleWhite() {
  int level=-1;

  for (int i=0; i<server.args(); i++) {
    if (server.argName(i).equals("level")) {
      level = server.arg(i).toInt();
    } else {
      Serial.println("Unknown argument: " + server.argName(i) + ": " + server.arg(i));
    }
  }

  if (level > -1) {
    Serial.print("Setting white to ");
    Serial.println(level);
    analogWrite(WHITE_PIN, level);
  }

  server.send(200, "text/plain", "ok");
}

void handleColour() {
  uint8_t r=0, g=0, b=0;

  for (int i=0; i<server.args(); i++) {
    if (server.argName(i).equals("r")) {
      r = server.arg(i).toInt();
    } else if (server.argName(i).equals("g")) {
      g = server.arg(i).toInt();
    } else if (server.argName(i).equals("b")) {
      b = server.arg(i).toInt();
    } else {
      Serial.println("Unknown argument: " + server.argName(i) + ": " + server.arg(i));
    }
  }

  String response = String("Setting color to ") + r + ", " + g + ", " + b + ".";
  Serial.println(response);

  setColour(r, g, b);

  server.send(200, "text/plain", response);
}

void setColour(uint8_t r, uint8_t g, uint8_t b) {
  for(int i=0; i<NUM_LEDS; i++) {
    pixels[i].R = r;
    pixels[i].G = g;
    pixels[i].B = b;
  }
}


void loop() {
  // Receive NTP packet
  if (udp.parsePacket()) {
    Serial.print("packet received after ");
    Serial.print(millis() - lastNTPRequest);
    Serial.print("ms... ");
    setTime(readNTPpacket(settings.timeZone));
    Serial.println("DONE");
    lastNTPSync = millis();
  }

  // Update lights
  ledstrip.show(pixels);

  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    setStatus(255, 0, 0);
    flash(3);
    Serial.println("Connecting to wifi...");
    delay(1000);
    return;
  }

  if ((lastNTPSync == 0) || (millis() - lastNTPSync > ntpSyncInterval * 10)) {
    // NTP has not run in the last 10 attempts (or ever)
    setStatus(255, 255, 0);
  } else {
    // Run CRON algorithm
    if (elapsedSecsToday(now()) > settings.offTime) {
      if (alarmOn) {
        alarmOn = 0;
        sunriseStartTime = 0;
        setColour(0, 0, 0);
        analogWrite(WHITE_PIN, 0);
      }
    } else if (elapsedSecsToday(now()) > settings.onTime) {
      if (!alarmOn) {
        alarmOn = 1;
        sunriseStartTime = millis();
      }
    }
  }

  if (alarmOn && (millis() - lastLightUpdate > lightUpdateInterval)) {
    drawFrame();

    lastLightUpdate = millis();
  }


  // Output serial status
  if (millis() - lastSerialStatus  > serialStatusInterval) {
    lastSerialStatus = millis();
  }

  // Send NTP packet
  if ((millis() - lastNTPRequest > ntpSyncInterval) || ((lastNTPSync == 0) && millis() - lastNTPRequest > 1000)) {
    flash(2);
    lastNTPRequest = millis();
    sendNTPpacket();
  }
}

void flash(int times) {
  for (int i=0; i<times; i++) {
    digitalWrite(RED_LED_PIN, HIGH);
    delay(50);
    digitalWrite(RED_LED_PIN, LOW);
    delay(50);
  }
}

void setStatus(uint8_t r, uint8_t g, uint8_t b) {
  pixels[0].R = r;
  pixels[0].G = g;
  pixels[0].B = b;
}



