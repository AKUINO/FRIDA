#include <IotWebConf.h>

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "FRIDA";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "1234";

#define STRING_LEN 128
#define NUMBER_LEN 32

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "Alpha 0.2"

// -- Callback method declarations.
void configSaved();
boolean formValidator();

DNSServer dnsServer;
WebServer server(80);

char ElsaHostnameValue[STRING_LEN];
char ElsaSensorNameValue_Temperature[STRING_LEN];
char ElsaSensorNameValue_Humidity[STRING_LEN];


IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

IotWebConfSeparator Separator1 = IotWebConfSeparator();

IotWebConfParameter ElsaHostName = IotWebConfParameter("Elsa Hostname", "ElsaHostNameID", ElsaHostnameValue, STRING_LEN);
IotWebConfParameter ElsaSensorName_Temperature = IotWebConfParameter("Elsa temperature sensor name", "ElsaSensorName_Temperature_ID", ElsaSensorNameValue_Temperature, STRING_LEN);
IotWebConfParameter ElsaSensorName_Humidity = IotWebConfParameter("Elsa optional humidity sensor name", "ElsaSensorName_Humidity_ID", ElsaSensorNameValue_Humidity, STRING_LEN);

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("Starting up...");

    iotWebConf.addParameter(&Separator1);

    iotWebConf.addParameter(&ElsaHostName);
    iotWebConf.addParameter(&ElsaSensorName_Temperature);
    iotWebConf.addParameter(&ElsaSensorName_Humidity);

    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.setFormValidator(&formValidator);
    iotWebConf.getApTimeoutParameter()->visible = true;
    // -- Initializing the configuration.
    iotWebConf.init();

    // -- Set up required URL handlers on the web server.
    server.on("/", handleRoot);
    server.on("/config", []{ iotWebConf.handleConfig(); });
    server.onNotFound([](){ iotWebConf.handleNotFound(); });

    Serial.println("Ready.");
}

void loop()
{
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 03 Custom Parameters</title></head><body>Hello world!";
  s += "<ul>";

  s += "<li>ElsaHostnameValue : ";
  s += ElsaHostnameValue;
  s += "<li>ElsaSensorNameValue_Temperature : ";
  s += ElsaSensorNameValue_Temperature;
  s += "<li>ElsaSensorNameValue_Humidity : ";
  s += ElsaSensorNameValue_Humidity;

  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void configSaved()
{
  Serial.println("Configuration was updated.");

}

boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;


  int l = server.arg(ElsaHostName.getId()).length();
  if (l < 5)
  {
    ElsaHostName.errorMessage = "Please provide at least 5 characters for this field !";
    valid = false;
  }

  return valid;
}