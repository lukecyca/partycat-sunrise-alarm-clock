#include <WiFiUdp.h>
#include <TimeLib.h>

WiFiUDP udp;
unsigned int localPort = 2390;
IPAddress timeServerIP;
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket() {
  IPAddress address;
  WiFi.hostByName(ntpServerName, address);

  Serial.print("sending NTP packet to ");
  Serial.print(address);
  Serial.print("... ");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  Serial.println("DONE");
}

// read an NTP response and convert it to a time_t in the configured timezone
time_t readNTPpacket(int timeZone) {

  udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  unsigned long secsSince1900;
  // convert four bytes starting at location 40 to a long integer
  secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
  secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
  secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
  secsSince1900 |= (unsigned long)packetBuffer[43];

  return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
}


