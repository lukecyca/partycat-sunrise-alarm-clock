#include <ESP8266WiFi.h>
#include <ws2812_i2s.h>
#include "ntp.h"



// Interval in ms to check sync
#define ntpSyncInterval (30000)
unsigned long lastNTPSync = 0;


#define NUM_LEDS 119
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

#include "libdcc/webserver.h"

void setup() {
  pinMode(WHITE_PIN, OUTPUT);

  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  Serial.begin(115200);

  ledstrip.init(NUM_LEDS);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Internet Fridge", WEBSERVER_PASSWORD);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

  for(int i=0; i<NUM_LEDS; i++) {
    pixels[i].R = r;
    pixels[i].G = g;
    pixels[i].B = b;
  }

  ledstrip.show(pixels);

  server.send(200, "text/plain", response);
}


void loop() {
  // Receive NTP packet
  if (udp.parsePacket()) {
    Serial.print("packet received after ");
    Serial.print(millis() - lastNTPSync);
    Serial.print("ms... ");
    setTime(readNTPpacket());
    Serial.println("DONE");
    return;
  }

  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    flash(3);
    Serial.println("Connecting to wifi...");
    delay(1000);
    return;
  }
  Serial.println("Wifi connected to " + WiFi.SSID() + " IP:" + WiFi.localIP().toString());



  // Send NTP packet
  if ((millis() - lastNTPSync > ntpSyncInterval)) {
    flash(2);
    lastNTPSync = millis();
    sendNTPpacket();
  }

  delay(100);

  Serial.println(elapsedSecsToday(now()));
}

void flash(int times) {
  for (int i=0; i<times; i++) {
    digitalWrite(RED_LED_PIN, HIGH);
    delay(50);
    digitalWrite(RED_LED_PIN, LOW);
    delay(50);
  }
}



