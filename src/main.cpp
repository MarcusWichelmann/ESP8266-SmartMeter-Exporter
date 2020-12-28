#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AddrList.h>

#include "config.h"

#if !LWIP_IPV6
#error Please select a lwIP variant with IPv6 support.
#endif

ESP8266WebServer _webServer(WEBSERVER_PORT);

void connectToWiFi()
{
  Serial.printf("Connecting to wifi SSID \"%s\" ..", WIFI_STA_SSID);

  // Configure WiFi/network stack
  WiFi.mode(WIFI_STA);
  WiFi.hostname(SYSTEM_HOSTNAME);

  // Begin connecting
  if (WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD) == WL_CONNECT_FAILED)
  {
    Serial.println(" BEGIN FAILED");
    return ESP.restart();
  }

  // Wait for IPv4 & IPv6 addresses other than link-local
  bool hasV4 = false, hasv6 = false;
  while (!hasV4 || !hasv6)
  {
    for (auto entry : addrList)
    {
      IPAddress addr = entry.addr();
      if (addr.isLocal())
        continue;

      if (!hasV4)
        hasV4 = addr.isV4();
      if (!hasv6)
        hasv6 = addr.isV6();
    }

    Serial.print('.');
    delay(500);
  }

  Serial.println(" OK");
  Serial.println();

  // Print ip addresses
  Serial.println("IP addresses:");
  for (auto entry : addrList)
  {
    Serial.printf("[%s] %s", entry.ifname().c_str(), entry.toString().c_str());

    if (entry.isLegacy())
      Serial.printf(", mask: %s, gateway: %s", entry.netmask().toString().c_str(), entry.gw().toString().c_str());

    if (entry.isLocal())
      Serial.print(" (link-local)");

    Serial.println();
  }
  Serial.println();

  // Print dns server list
  Serial.print("DNS servers:");
  for (int i = 0; i < DNS_MAX_SERVERS; i++)
  {
    IPAddress dns = WiFi.dnsIP(i);
    if (dns.isSet())
      Serial.printf(" %s", dns.toString().c_str());
  }
  Serial.println();
  Serial.println();
}

void setup()
{
  Serial.begin(74880);
  Serial.setDebugOutput(ENABLE_DEBUG);
  Serial.println();

  Serial.printf("-- SmartMeter Exporter (%s, %s) --\n", SYSTEM_HOSTNAME, WiFi.macAddress().c_str());
  Serial.println(ESP.getFullVersion());
  Serial.println();

  connectToWiFi();

  Serial.println("Ready.");
}

void loop()
{
}