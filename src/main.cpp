// Update command: curl -u USER:PASSWORD -F "image=@.pio/build/esp12e/firmware.bin" smartmeter-1/update

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <SoftwareSerial.h>
#include <AddrList.h>

#include "config.h"

#if !LWIP_IPV6
#error Please select a lwIP variant with IPv6 support.
#endif

ESP8266WebServer _webServer{WEBSERVER_PORT};
ESP8266HTTPUpdateServer _updater{true};

SoftwareSerial _irSerial;

char _lineBuffer[64];
size_t _lineBufferPos{0};

// Stats
char _statOwnershipNumber[21];
double _statEnergyUsed{0};
double _statEnergyProduced{0};
double _statPowerL1{0};
double _statPowerL2{0};
double _statPowerL3{0};
double _statPowerTotal{0};
uint8_t _statStatusCode{0};
char _statSerialNumber[21];

void connectToWiFi()
{
  Serial.printf("Connecting to wifi SSID \"%s\" ...", WIFI_STA_SSID);

  // Configure WiFi/network stack
  WiFi.mode(WIFI_STA);
  WiFi.hostname(SYSTEM_HOSTNAME);

  // Begin connecting
  if (WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD) == WL_CONNECT_FAILED)
  {
    Serial.println(" BEGIN FAILED");
    ESP.restart();
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

void configureMDNS()
{
  Serial.print("Configuring mDNS ...");

  if (!MDNS.begin(SYSTEM_HOSTNAME))
  {
    Serial.println(" BEGIN FAILED");
    ESP.restart();
  }

  MDNS.addService("http", "tcp", WEBSERVER_PORT);

  Serial.println(" OK");
}

void handleRootRequest()
{
  _webServer.send(200, "text/plain; charset=utf-8",
                  "ESP8266 SmartMeter Exporter for Prometheus\n\n"
                  "Metrics URL: /metrics\n"
                  "Update URL: /update\n");
}

void handleMetricsRequest()
{
  static const char *metricsTemplate =
      "# HELP smartmeter_total_energy_used The total energy used in kWh.\n"
      "# TYPE smartmeter_total_energy_used counter\n"
      "smartmeter_total_energy_used{sn=\"%s\"} %f\n\n"
      "# HELP smartmeter_total_energy_produced The total energy produced in kWh.\n"
      "# TYPE smartmeter_total_energy_produced counter\n"
      "smartmeter_total_energy_produced{sn=\"%s\"} %f\n\n"
      "# HELP smartmeter_power_per_phase The current power per phase in W.\n"
      "# TYPE smartmeter_power_per_phase gauge\n"
      "smartmeter_power_per_phase{sn=\"%s\",phase=\"1\"} %f\n"
      "smartmeter_power_per_phase{sn=\"%s\",phase=\"2\"} %f\n"
      "smartmeter_power_per_phase{sn=\"%s\",phase=\"3\"} %f\n\n"
      "# HELP smartmeter_power_all_phases The current power on all phases in W.\n"
      "# TYPE smartmeter_power_all_phases gauge\n"
      "smartmeter_power_all_phases{sn=\"%s\"} %f\n\n"
      "# HELP smartmeter_status_code The current status of the smartmeter.\n"
      "# TYPE smartmeter_status_code gauge\n"
      "smartmeter_status_code{sn=\"%s\"} %d\n";

  // Return an empty response if no device data is available yet
  if (_statSerialNumber[0] == '\0')
  {
    _webServer.send(204, "text/plain; charset=utf-8");
    return;
  }

  char responseBuffer[1024];
  snprintf(responseBuffer, sizeof(responseBuffer), metricsTemplate,
           _statSerialNumber, _statEnergyUsed,
           _statSerialNumber, _statEnergyProduced,
           _statSerialNumber, _statPowerL1,
           _statSerialNumber, _statPowerL2,
           _statSerialNumber, _statPowerL3,
           _statSerialNumber, _statPowerTotal,
           _statSerialNumber, _statStatusCode);

  _webServer.send(200, "text/plain; charset=utf-8", responseBuffer);
}

void startWebServer()
{
  Serial.print("Starting web server ...");

  _webServer.on("/", handleRootRequest);
  _webServer.on("/metrics", handleMetricsRequest);
  _webServer.begin();

  Serial.println(" OK");
}

void configureUpdater()
{
  Serial.print("Configuring updater ...");

  _updater.setup(&_webServer, "/update", UPDATER_USERNAME, UPDATER_PASSWORD);

  Serial.println(" OK");
}

void initializeReader()
{
  Serial.print("Initializing reader ...");

  // Use a large enough buffer to store a whole status transmission in case the loop is busy.
  _irSerial.begin(9600, SWSERIAL_7E1, IR_SERIAL_RX, IR_SERIAL_TX, false, 512);

  Serial.println(" OK");
}

void readStringValue(char *str, char *out, size_t maxLength)
{
  if (strlen(str) > maxLength)
    return;
  strncpy(out, str, maxLength);
}

void readDoubleValue(char *str, double *out)
{
  // Remove *UNIT from the string
  str = strtok(str, "*");

  // Parse double
  *out = strtod(str, NULL);
}

void readByteValue(char *str, uint8_t *out)
{
  // Remove *UNIT from the string
  str = strtok(str, "*");

  // Parse byte
  *out = (uint8_t)strtoul(str, NULL, 10);
}

void processDataLine()
{
  // Extract key and value from the line
  char *key = strtok(_lineBuffer, "(");
  char *value = strtok(NULL, ")");

  if (key == NULL || value == NULL)
    return;

  if (strcmp(key, "1-0:0.0.0*255") == 0)
    readStringValue(value, _statOwnershipNumber, 20);
  else if (strcmp(key, "1-0:1.8.0*255") == 0)
    readDoubleValue(value, &_statEnergyUsed);
  else if (strcmp(key, "1-0:2.8.0*255") == 0)
    readDoubleValue(value, &_statEnergyProduced);
  else if (strcmp(key, "1-0:21.7.0*255") == 0)
    readDoubleValue(value, &_statPowerL1);
  else if (strcmp(key, "1-0:41.7.0*255") == 0)
    readDoubleValue(value, &_statPowerL2);
  else if (strcmp(key, "1-0:61.7.0*255") == 0)
    readDoubleValue(value, &_statPowerL3);
  else if (strcmp(key, "1-0:1.7.0*255") == 0)
    readDoubleValue(value, &_statPowerTotal);
  else if (strcmp(key, "1-0:96.5.5*255") == 0)
    readByteValue(value, &_statStatusCode);
  else if (strcmp(key, "0-0:96.1.255*255") == 0)
    readStringValue(value, _statSerialNumber, 20);
}

void readIrData()
{
  bool received = false;

  while (_irSerial.available() > 0)
  {
    // Turn builtin LED on while receiving, because it looks cool ;)
    digitalWrite(LED_BUILTIN, LOW);
    received = true;

    // Read next char
    char nextChar = _irSerial.read();
    switch (nextChar)
    {
    case '\0':
    case '\r':
      // Ignore
      break;

    case '\n':
      // Process finished line
      processDataLine();

      // Clear the buffer
      _lineBuffer[_lineBufferPos = 0] = '\0';
      break;

    default:
      // Buffer full (omit one byte for the \0)?
      if (_lineBufferPos >= sizeof(_lineBuffer) - 1)
      {
        Serial.println("Reading line failed. Is the line buffer too small?");

        // Skip the data. All the following data will be invalid until the next \n.
        _lineBuffer[_lineBufferPos = 0] = '\0';
        break;
      }

      // Add character to buffer
      _lineBuffer[_lineBufferPos++] = nextChar;
      _lineBuffer[_lineBufferPos] = '\0';
      break;
    }
  }

  if (received)
    digitalWrite(LED_BUILTIN, HIGH);
}

void setup()
{
  // Turn builtin LED on during setup
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // Active low

  Serial.begin(74880);
  Serial.setDebugOutput(ENABLE_DEBUG);
  Serial.println();

  Serial.printf("-- SmartMeter Exporter (%s, %s) --\n", SYSTEM_HOSTNAME, WiFi.macAddress().c_str());
  Serial.println(ESP.getFullVersion());
  Serial.println();

  connectToWiFi();
  configureMDNS();
  startWebServer();
  configureUpdater();
  initializeReader();

  Serial.println("Ready.");
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  _webServer.handleClient();
  MDNS.update();
  readIrData();
}