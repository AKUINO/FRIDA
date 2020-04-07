#define CONNECT_TIMEOUT   30      // Seconds
#define CONNECT_OK        0       // Status of successful connection to WiFi
#define CONNECT_FAILED    (-99)   // Status of failed connection to WiFi
// #define _TASK_TIMECRITICAL     // Enable monitoring scheduling overruns
//#define _TASK_SLEEP_ON_IDLE_RUN // Enable 1 ms SLEEP_IDLE powerdowns between tasks if no callback methods were invoked during the pass
#define _TASK_STATUS_REQUEST      // Compile with support for StatusRequest functionality - triggering tasks on status change events in addition to time only
// #define _TASK_WDT_IDS          // Compile with support for wdt control points and task ids
// #define _TASK_LTS_POINTER      // Compile with support for local task storage pointer
// #define _TASK_PRIORITY         // Support for layered scheduling priority
// #define _TASK_MICRO_RES        // Support for microsecond resolution
// #define _TASK_STD_FUNCTION     // Support for std::function (ESP8266 and ESP32 ONLY)
// #define _TASK_DEBUG            // Make all methods and variables public for debug purposes
// #define _TASK_INLINE           // Make all methods "inline" - needed to support some multi-tab, multi-file implementations
// #define _TASK_TIMEOUT          // Support for overall task timeout
// #define _TASK_OOCallBackS     // Support for dynamic callback method binding
// #define _TASK_DEFINE_MILLIS    // Force forward declaration of millis() and micros() "C" style
// #define _TASK_EXPOSE_CHAIN     // Methods to access tasks in the task chain
// Debug and Test options
#define _DEBUG_
//#define _TEST_
#ifdef _DEBUG_
#define PP_(a) Serial.print(a);
#define PL_(a) Serial.println(a);
#else
#define PP_(a)
#define PL_(a)
#endif
#define CONNECT_TIMEOUT   30      // Seconds
#define CONNECT_OK        0       // Status of successful connection to WiFi
#define CONNECT_FAILED    (-99)   // Status of failed connection to WiFi

/*
//  /!\ LES DEFINE RELATIFS AUX THREADS DOIVENT ETRE AVANT "include <TaskScheduler>" SINON APPAREMMENT CA PLANTE LES TASKS !
*/
#include <TaskScheduler.h>
#include "Adafruit_SHT31.h"
#include "AdafruitIO_WiFi.h"
#include <M5Stack.h>
#include <Wire.h>            // Used to establish serial communication on the I2C bus
#include <SparkFun_TMP117.h> // Used to send and receive specific information from our sensor
#include <MCP41xxx.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <IotWebConf.h>

Scheduler ts;

//NETWORK
char* WIFI_SSID;
char* WIFI_PASS;
WiFiClient myWiFiClient;                                //CLIENT USED TO SEND HTTP REQUESTS TO ELSA
const long utcOffsetInSeconds = 3600;                   //GMT +1
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
HttpClient client = HttpClient(myWiFiClient, "totototo.be", 80); //= HttpClient(myWiFiClient, "phenics.gembloux.ulg.ac.be", 80);

String ElsaServerURL;
String ElsaTemperatureSensorName;
String ElsaHumiditySensorName;
bool ValidElsaCredentials = false;

bool WiFiConnected = false;
uint16_t elsaTimeoutCounter = 0;
enum PlatformUsed { UNDEF, AdafruitIO, Elsa };

//SENSORS
Adafruit_SHT31 sht31 = Adafruit_SHT31();                //Humidity & Temperature sensor
TMP117 sensor48;                                        //Initalize Temperature-only sensor
int i2cAddr = 0x48;                                     //Address of the SHT31 sensor
bool foundSHT31 = false;
bool foundTMP117 = false;
float SHT31_T;
float SHT31_H;
float TMP117_T;

//DISPLAY
bool blank = false;
float prv_SHT31_T = 0.0;
float prv_SHT31_H = 0.0;
int prvColT = WHITE;
int prvColH = WHITE;
int row = 100;
int col = 80;
bool rewrite;
float prv_TMP117_T = 0.0;
int prvCol48 = WHITE;


// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char initialThingName[] = "FRIDA";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "1234";

#define STRING_LEN 128
#define NUMBER_LEN 32

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "Alph"

// -- Callback method declarations.
void configSaved();
boolean formValidator();

DNSServer dnsServer;
WebServer server(80);

char ElsaHostnameValue[STRING_LEN];
char ElsaSensorNameValue_Temperature[STRING_LEN];
char ElsaSensorNameValue_Humidity[STRING_LEN];


IotWebConf iotWebConf(initialThingName, &dnsServer, &server, wifiInitialApPassword);

IotWebConfSeparator Separator1 = IotWebConfSeparator();

IotWebConfParameter ElsaHostName = IotWebConfParameter("Elsa Hostname", "ElsaHostNameID", ElsaHostnameValue, STRING_LEN);
IotWebConfParameter ElsaSensorName_Temperature = IotWebConfParameter("Elsa temperature sensor name", "ElsaSensorName_Temperature_ID", ElsaSensorNameValue_Temperature, STRING_LEN);
IotWebConfParameter ElsaSensorName_Humidity = IotWebConfParameter("Elsa optional humidity sensor name", "ElsaSensorName_Humidity_ID", ElsaSensorNameValue_Humidity, STRING_LEN);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////TASKS AND CALLBACK METHODS/////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void webConfLoopCallBack();
void checkWebConfStatusCallBack();
void getElsaCredentialsCallBack();

// -- IotWebConf Tasks
Task tWebConfLoop(TASK_SECOND, TASK_FOREVER, &webConfLoopCallBack, &ts, true);
Task tCheckWebConfStatus(TASK_SECOND, TASK_FOREVER, &checkWebConfStatusCallBack, &ts, true);

// -- NETWORK : SENDING DATA
Task tSendToElsa(10 * TASK_SECOND, TASK_FOREVER, &getElsaCredentialsCallBack, &ts, false);

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

    /* USELESS SINCE THE AP WILL FALLBACK TO WIFI BY HIMSELF AFTER SOME TIME
    if(M5.BtnA.read() == true && M5.BtnB.read() == true && M5.BtnC.read() == true )
    {
        //rename ThingName with initial Thing Name
    }

    if(iotWebConf.getThingNameParameter() != initialThingName)
    {
        iotWebConf.skipApStartup();
    }
    */

    // -- Set up required URL handlers on the web server.
    server.on("/", handleRoot);
    server.on("/config", []{ iotWebConf.handleConfig(); });
    server.onNotFound([](){ iotWebConf.handleNotFound(); });

    Serial.println("Ready.");
}

void loop()
{
    ts.execute();

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////START OF IotWebConf CALLBACK METHODS///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////END OF IotWebConf CALLBACK METHODS/////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void webConfLoopCallBack()
{
    PL_("WebConf Loop");
    // -- doLoop should be called as frequently as possible.
    iotWebConf.doLoop();
}

void checkWebConfStatusCallBack()
{
    if(iotWebConf.getState() == 4)
    {
        PL_("WebConf Status == 4 => We are connected");
        tWebConfLoop.setInterval(3*TASK_SECOND);
        tSendToElsa.enableDelayed();
        tCheckWebConfStatus.disable();
    }
    else{
        PL_("WebConf Status != 4 => We are NOT connected");
    }
}

void getElsaCredentialsCallBack()
{
    PL_("getElsaCredentialsCallBack");

    WIFI_PASS = iotWebConf.getWifiPasswordParameter()->valueBuffer;
    WIFI_SSID = iotWebConf.getWifiSsidParameter()->valueBuffer;
    PP_("WIFI credentials : ");
    PP_(WIFI_SSID);
    PP_(" + ");
    PL_(WIFI_PASS);
    if(WiFi.status() == WL_CONNECTED)
    {
        PL_("WiFi connected : WiFi.status == WL_CONNECTED");
        WiFiConnected = true;
        ElsaTemperatureSensorName = ElsaSensorNameValue_Temperature;
        ElsaHumiditySensorName = ElsaSensorNameValue_Humidity;
        if(isValidInput(ElsaHostnameValue))//redundant as the web form already asks for a minimum of 5 characters.
        {
            PP_("Elsa credentials : ");
            PP_(ElsaHostnameValue);
            PP_(" + ");
            PP_(ElsaTemperatureSensorName);
            PP_(" + ");
            PP_(ElsaHumiditySensorName);
            PL_(" .")
            ValidElsaCredentials = true;
            ElsaServerURL = ElsaHostnameValue;
             client = HttpClient(myWiFiClient, ElsaServerURL, 80);

            tSendToElsa.set(10*TASK_SECOND, TASK_FOREVER, &sendDataToElsaCallBack);
            //tSendToElsa.yield();
        }
        else{
            PL_("Invalid Elsa credentials");
            PP_("Elsa credentials : ");
            PP_(ElsaHostnameValue);
            PP_(" + ");
            PP_(ElsaTemperatureSensorName);
            PP_(" + ");
            PP_(ElsaHumiditySensorName);
            PL_(" .")
        }
    }
    else
    {
        PL_("Trying to connect to WiFi");
        WiFiConnected = false;
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_SSID);
    }

}



void sendDataToElsaCallBack()
{
    foundTMP117 = true;
    if (foundSHT31)
    {
        //sendToElsa_SHT31();
    }
    if (foundTMP117)
    {
        if(TMP117_T>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            sendToElsa_TMP117();
        }
    }
}
void sendToElsa_TMP117()
{
    PL_("sendToElsa_TMP117");
    PL_("Before if WiFi = connected");

    if(WiFi.status() == WL_CONNECTED && isValidInput(ElsaTemperatureSensorName))//NOT REDUNDANT AS THE TASK WILL ALWAYS RESTART FROM sendToAdaCallBack() METHOD.
    {
        Serial.println("Sending HTTP request...");
        // Make a HTTP request:
        String url;
        /*url = '/api.put!s_' + String(ElsaTemperatureSensorName) + '?value ='+ String(TMP117_T)
            + "&control=" + String(calculateCheckSUM(ElsaTemperatureSensorName, TMP117_T));*/
        url = "/api/put/!s_TMP117?value=" + String(25.00)
            + "&control=" + String(calculateCheckSUM("TMP117", 25.00));
        PP_("Sending data : GET ");
        PL_(url);
        client.get(url);
        int statusCode = client.responseStatusCode();
        String response = client.responseBody();
        PP_("Status code: ");
        PL_(statusCode);
        PP_("Response: ");
        PL_(response);

        if(statusCode <199 || statusCode>299)
        {
            if(statusCode == 403)
            {
                PL_("403 forbidden, bad checksum.");
            }
            else
            {
                elsaTimeoutCounter ++;
                PL_("Elsa TimeOut");
                if(elsaTimeoutCounter >=10)
                {
                    PL_("10 elsa Timeouts, trying to reconnect");
                    elsaTimeoutCounter = 0;
                    tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &getElsaCredentialsCallBack); //this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
                    tSendToElsa.enableDelayed();
                }
                return;
            }
        }
        PL_();
        PL_("closing connection");
        elsaTimeoutCounter = 0;
    }
    else{
        PL_("WiFi Status != WL_CONNECTED");
        WiFiConnected = false;
        resetTaskForWiFiConnection(tSendToElsa, Elsa);
    }
    client.stop();
    tSendToElsa.yield(&getElsaCredentialsCallBack);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////END OF DATA SENDING METHODS////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// -- Generic methods
bool isValidInput(String param) //If we don't put anything or if we type "NULL" then we don't want this parameter
{
    if(param != "" && param != "NULL")
    {
        return true;
    }
    else{
        return false;
    }
}

void resetTaskForWiFiConnection (Task& dataSendingTask, PlatformUsed platformUsed)
{
    switch (platformUsed) //Using a switch in case we want to add new possibilities in a future development. Btw, switch only allows integer/char so we use an ENUM which is basically hidden integers++++++++++
    {
        case AdafruitIO:
            //dataSendingTask.disable();
            //dataSendingTask.setCallback(&initAdaCallBack);
            //dataSendingTask.waitFor(tConnect.getInternalStatusRequest()); //connectToAdaCallBack() will only start after the connection to the WiFi is successful => depends on the statusRequest tConnect sends.
            break;
        case Elsa:
            //dataSendingTask.disable();
            dataSendingTask.setCallback(&getElsaCredentialsCallBack);
            //dataSendingTask.waitFor(tConnect.getInternalStatusRequest()); //connectToElsaCallBack() will only start after the connection to the WiFi is successful => depends on the statusRequest tConnect sends.
            break;
        default:
            PL_("resetTaskForWiFiConnection DEFAULT SWITCH, an error happened !");
            //We might want to add all Tasks in charge of sending data to an online platform in an array and reset all these tasks here ?
            break;
    }
    //tConnect.setCallback(&connectInitCallBack); //Restarting WiFi connection from tConnect Task. If Adafruit or WiFi keeps failing, then this Task will relaunch the tBeforeConnectionStarts Task to check for new credentials.
    //tConnect.enableDelayed();
}

int calculateCheckSUM(String SensorName, float Value)
{
    Serial.println("Inside CheckSUM calculation");
    String data = "!s"+SensorName+String(Value,2); //Conversion de float en arrondissant à la deuxième décimale.
    Serial.println(data);
    char charArray[data.length()+1];//[data.length()+1]//+1 car on a un caractère de fin de tableau. Seems useless tho?
    data.toCharArray(charArray, data.length()+1);//copy content from string to charArray
    for (int i = 0; i<data.length(); i++)
    {
         Serial.print(charArray[i]);
    }
    Serial.println();
    int result = 0xABCD;
    for (int i = 0; i<data.length(); i++)
    {
        result ^= (int)charArray[i];//Transformation en unicode : conversion du char en int.
    }
    Serial.println(result);
    Serial.println("Outside CheckSUM calculation");
    return result; //&& 0xFF;//On ne garde que le premier? octet.
}