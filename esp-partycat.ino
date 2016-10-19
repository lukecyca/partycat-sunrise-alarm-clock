#include <ESP8266WiFi.h>
#include <ws2812_i2s.h>
#include "ntp.h"


// Interval in ms to check sync
#define ntpSyncInterval (600000)
unsigned long lastNTPRequest = 0;
unsigned long lastNTPSync = 0;

#define serialStatusInterval (10000)
unsigned long lastSerialStatus = 0;

#define animationInterval (10)
unsigned long lastAnimationUpdate = 0;
char alarmOn = 0;
unsigned long animationStartTime = 0;

#define ANIM_STATE_OFF (0)
#define ANIM_STATE_SUNRISE (1)
#define ANIM_STATE_FADEOUT (2)
int animationState = ANIM_STATE_OFF;


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
#define PWM_RANGE_FULL (1024)


#define SETTINGS_VERSION "s4d3"
struct Settings {
  // Time in seconds after midnight to begin sunrise
  int onTime;

  // Time in seconds after midnight to turn off the light
  int offTime;

  // How quickly to advance the sunrise frames (default 1)
  int fps;

  // Timezone offset
  int timeZone;
} settings = {
  23400,
  28800,
  1,
  -7
};

#include "libdcc/webserver.h"
#include "libdcc/settings.h"
#include "animation.h"

String formatSettings() {
  return \
    String("onTime=") + String(settings.onTime) + \
    String(",offTime=") + String(settings.offTime) + \
    String(",fps=") + String(settings.fps) + \
    String(",timeZone=") + String(settings.timeZone);
}

void handleSettings() {
  REQUIRE_AUTH;

  for (int i=0; i<server.args(); i++) {
    if (server.argName(i).equals("onTime")) {
      settings.onTime = server.arg(i).toFloat();
    } else if (server.argName(i).equals("offTime")) {
      settings.offTime = server.arg(i).toFloat();
    } else if (server.argName(i).equals("fps")) {
      settings.fps = server.arg(i).toInt();
    } else if (server.argName(i).equals("timeZone")) {
      settings.timeZone = server.arg(i).toInt();
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
  analogWriteRange(PWM_RANGE_FULL);
  pinMode(WHITE_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  Serial.begin(115200);

  loadSettings();

  ledstrip.init(NUM_LEDS);
  setColour(0, 0, 0);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Internet Lightbox", WEBSERVER_PASSWORD);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  server.on("/settings", handleSettings);
  server.on("/restart", handleRestart);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

void loop() {
  // Connect to wifi if not already
  if (WiFi.status() != WL_CONNECTED) {
    flash(3);
    Serial.println("Connecting to wifi...");
    delay(1000);
    return;
  }

  // Handle incoming NTP packet
  if (udp.parsePacket()) {
    Serial.print("packet received after ");
    Serial.print(millis() - lastNTPRequest);
    Serial.print("ms... ");
    setTime(readNTPpacket(settings.timeZone));
    Serial.println("DONE");
    lastNTPSync = millis();
  }

  // Handle web request
  server.handleClient();

  // Run CRON algorithm, but only if we have sync'd to NTP
  if (lastNTPSync != 0) {
    if (elapsedSecsToday(now()) > settings.offTime) {
      if (animationState == ANIM_STATE_SUNRISE) {
        Serial.println("Start fadeout");
        animationState = ANIM_STATE_FADEOUT;
        animationStartTime = millis();
      }
    } else if (elapsedSecsToday(now()) > settings.onTime) {
      if (animationState == ANIM_STATE_OFF) {
        Serial.println("Start sunrise");
        animationState = ANIM_STATE_SUNRISE;
        animationStartTime = millis();
      }
    }
  }

  // Animate
  if (animationState == ANIM_STATE_SUNRISE && (millis() - lastAnimationUpdate > animationInterval)) {
    drawSunriseFrame();
    lastAnimationUpdate = millis();
  }
  if (animationState == ANIM_STATE_FADEOUT && (millis() - lastAnimationUpdate > animationInterval)) {
    if (!drawFadeoutFrame()) {
      Serial.println("Finished fadeout");
      animationState = ANIM_STATE_OFF;
      animationStartTime = 0;
    }
    lastAnimationUpdate = millis();
  }

  // Output serial status periodically
  if (millis() - lastSerialStatus  > serialStatusInterval) {
    lastSerialStatus = millis();
  }

  // Send NTP packet at regular intervals,
  // or more frequently if we haven't had a successful sync in the last 10 intervals (or ever)
  if (
    (millis() - lastNTPRequest > ntpSyncInterval) ||
    (((lastNTPSync == 0) || (millis() - lastNTPSync > ntpSyncInterval * 10)) && millis() - lastNTPRequest > 1000)
  ) {
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
    delay(100);
  }
}

void setColour(uint8_t r, uint8_t g, uint8_t b) {
  for(int i=0; i<NUM_LEDS; i++) {
    pixels[i].R = r;
    pixels[i].G = g;
    pixels[i].B = b;
  }
  ledstrip.show(pixels);
}
