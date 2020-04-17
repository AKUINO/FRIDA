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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////VARIABLES DECLARATION///////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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


// -- Configuration specific key. The value should be modified if config structure was changed.
//#define CONFIG_VERSION "Alph"
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////TASKS AND CALLBACK METHODS/////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// -- Callback method declarations.
//SENSORS & DISPLAY CALLBACK METHODS
void readSensorCallBack();
void readButtonsStateCallBack();
void shutScreenOffCallBack();

//NETWORK CALLBACK METHODS
void aioTimeUpdBackgroundCallback();
void initElsaCallBack();

void webConfLoopCallBack();
void checkWebConfStatusCallBack();
void getElsaCredentialsCallBack();

// -- TASKS
//SENSORS & DISPLAY TASKS
Task tReadSensor(5 * TASK_SECOND, TASK_FOREVER, &readSensorCallBack, &ts, true);
Task tReadButtonsState(200, TASK_FOREVER, &readButtonsStateCallBack, &ts, true); //200 milliseconds delay
Task tShutScreenOff(TASK_MINUTE, 1, &shutScreenOffCallBack, &ts, false);

//IotWebConf
Task tWebConfLoop(TASK_SECOND, TASK_FOREVER, &webConfLoopCallBack, &ts, true);
Task tCheckWebConfStatus(TASK_SECOND, TASK_FOREVER, &checkWebConfStatusCallBack, &ts, true);

//NETWORK
Task tIoTimeUpd(10 * TASK_SECOND, TASK_FOREVER, &aioTimeUpdBackgroundCallback, &ts, true);
Task tSendToElsa(10 * TASK_SECOND, TASK_FOREVER, &getElsaCredentialsCallBack, &ts, false);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////START OF SETUP//////////////////////////////////////////////////////
//  - Launches Serial connection to computer
//  - Starts the M5Stack components
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup()
{
    #if defined(_DEBUG_) || defined(_TEST_)
    Serial.begin(115200);
    #endif
    Serial.println("Starting up...");
    //M5STACK & SENSORS
    M5.begin(true,false,true); // init lcd, serial, but don't init sd card
    M5.Power.begin();
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
    M5.Lcd.setCursor(0,20);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setRotation(3); //Invert the display
    M5.Lcd.setBrightness(100);
    M5.Lcd.clear();
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println("Searching sensors...");

    if(sht31.begin(0x44)) //Test if Humidity sensor is there
    {
        foundSHT31 = true;
        PL_("Found SHT31");
        M5.Lcd.println("Found SHT31");
        delay(5000);
        M5.Lcd.clear();
    }
    //Test if Temperature-Only sensor is there
    else if(sensor48.begin(i2cAddr) == true) // Function to check if the sensor will correctly self-identify with the proper Device ID/Address
    {
        foundTMP117 = true;
        sensor48.setContinuousConversionMode();
        PL_("Found TMP117");
        M5.Lcd.println("Found TMP117");
        delay(2000);
        M5.Lcd.clear();
    }
    else //If nothing was detected then shut down
    {
        PL_("Couldn't find SHT31");
        PL_("Couldn't find TMP117");
        M5.Lcd.println("Couldn't find SHT31");
        M5.Lcd.println("Couldn't find TMP117");
        foundTMP117 = false;
        foundSHT31 = false;
        delay (10000);
        M5.Lcd.clear();
        M5.Lcd.setBrightness(0);
        while (1);
    }

    //Access Point
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////START OF MAIN LOOP//////////////////////////////////////////////////
//  - Main loop should only execute the task schedulers for a well functioning MultiThreading
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop()
{
    ts.execute();

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////START OF SENSORS METHODS///////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void readSensorCallBack()
{
    PL_("tReadSensor");
    if (foundSHT31 == true)
    {
        tReadSensor.setCallback(&processValuesSHT31);
        /*Not quite sure if these should or shouldn't be callbacks.
        They probably should in the case where we allow multiple sensors to be connected.
        Then we would simply have IFs and not ELSE IFs right below here.
        */
    }
    else if(foundTMP117 == true)
    {
        PL_("readSensorCallBack -- foundTMP117 == true");
        tReadSensor.yield(&processValuesTMP117);
    }
}

void processValuesSHT31()
{
    PL_("tReadSensor -- processValuesSHT31");
    //on lit les informations de ce capteur
    SHT31_T = sht31.readTemperature();
    SHT31_T = floor(100*SHT31_T)/100; //rounding things to the second decimal.
    SHT31_H = sht31.readHumidity();
    SHT31_H = floor(100*SHT31_H)/100;
    debugReadSHT31();
    rewrite = !blank; //if blank = false then rewrite = true => if we are displaying stuff then we want to rewrite the value on the lcd (updates the value on screen)
    tReadSensor.setCallback(&SHT31_DesignateDisplayColor);
}

void debugReadSHT31()
{
    if (!isnan(SHT31_T)) {  // check if 'is not a number'
        Serial.print("SHT31_T = ");
        Serial.print(SHT31_T);
    }
    else {
        Serial.print("SHT31_T = null");
    }

    if (!isnan(SHT31_H)) {  // check if 'is not a number'
        Serial.print("SHT31_H = ");
        Serial.print(SHT31_H);
    }
    else {
        Serial.print("SHT31_H = null");
    }
}

void processValuesTMP117()
{
    PL_("tReadSensor -- processValuesTMP117");
    //on lit les informations de ce capteur
    TMP117_T = sensor48.readTempC();
    TMP117_T = floor(100*TMP117_T)/100; //rounding things to the second decimal.
    debugReadTMP117();
    if(TMP117_T <= 0 && TMP117_T > -1.0 && sensor48.begin(i2cAddr) == false)// unplugging TMP117 gives negative value close to 0 whereas unplugging SHT31 gives NAN so we're checking that.
    {
        TMP117_T = NAN;
    }
    rewrite = !blank; //if blank == false then rewrite = true => if we are displaying stuff then we want to rewrite the value on the lcd (updates the value on screen)
    tReadSensor.yield(&TMP117_DesignateDisplayColor);
}

void debugReadTMP117()
{
    Serial.print("TMP117_T = ");
    if(!isnan(TMP117_T))
        PP_(TMP117_T);
    PL_("°C");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////START OF DISPLAY METHODS///////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//SHT31
void SHT31_DesignateDisplayColor()
{
    if (SHT31_T > prv_SHT31_T) {
        prvColT = RED;
    }
    else if (SHT31_T < prv_SHT31_T) {
        prvColT = BLUE;
    }
    else {
        if (prvColT != WHITE && blank == false) rewrite = true;
        //else rewrite = false; // If the values didn't change, then we don't want to update the LCD
        prvColT = WHITE;
    }

    if (SHT31_H > prv_SHT31_H) {
        prvColH = RED;
    }
    else if (SHT31_H < prv_SHT31_H) {
        prvColH = BLUE;
    }
    else {
        if (prvColH != WHITE && blank == false) rewrite = true;
        //else rewrite = false; // If the values didn't change, then we don't want to update the LCD
        prvColH = WHITE;
    }
    tReadSensor.setCallback(&SHT31_DisplayValues);
}

void SHT31_DisplayValues()
{
    if(rewrite)
    {
        M5.Lcd.clear();
        //Display wifiFailedStatus
        displayWiFiFailed();
        //Display NTP
        M5.Lcd.setCursor(0,130);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(&FreeSansBold18pt7b);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.print(timeClient.getFormattedTime());
        //Display values
        M5.Lcd.setCursor(col+100,row);
        if(SHT31_T<-30.00 || isnan(SHT31_T))//Set to yellow for errors as we consider anything below -30 as a bad input
        {
            M5.Lcd.setTextColor(TFT_YELLOW);
        }
        else
        {
            M5.Lcd.setTextColor(prvColT);
        }
        M5.Lcd.print(SHT31_T);
        M5.Lcd.println("'C");
        M5.Lcd.setCursor(col+100,150);
        if(SHT31_H<-30.00 || isnan(SHT31_H))//Set to yellow for errors as we consider anything below -30 as a bad input
        {
            M5.Lcd.setTextColor(TFT_YELLOW);
        }
        else
        {
            M5.Lcd.setTextColor(prvColH);
        }
        M5.Lcd.print(SHT31_H);
        M5.Lcd.println("%H");
        //Color designation depends on the previously DISPLAYED value, not the previously measured one.
        prv_SHT31_T = SHT31_T; //Used to know if the next measure is gonna be > or < current measure.
        prv_SHT31_H = SHT31_H; //Used to know if the next measure is gonna be > or < current measure.
    }
    if (tReadSensor.getRunCounter() <= 1)
    {
        //Enable the tShutScreenOff task on the first run AFTER we show stuff to avoid having it just shut the screen off immediately.
        tShutScreenOff.enableDelayed(TASK_MINUTE);
    }
    //tReadSensor.yield(&processValuesSHT31); //passes to the scheduler, next pass will be skipping the first step since we now know what sensor we are using.
    tReadSensor.setCallback(&processValuesSHT31); //Looping the task
    tReadSensor.delay(5 * TASK_SECOND);           //Same
}
//TMP117
void TMP117_DesignateDisplayColor()
{
    if (TMP117_T > prv_TMP117_T) {
        prvCol48 = RED;
    }
    else if (TMP117_T < prv_TMP117_T) {
        prvCol48 = BLUE;
        }
    else {
        if (prvCol48 != WHITE && blank == false) rewrite = true;
        //else rewrite = false; // If the values didn't change, then we don't want to update the LCD
        prvCol48 = WHITE;
    }
    tReadSensor.yield(&TMP117_DisplayValues);
}

void TMP117_DisplayValues()
{
    if(rewrite)
    {
        M5.Lcd.clear();
        //Display wifiFailedStatus
        displayWiFiFailed();
        //Display NTP
        M5.Lcd.setCursor(0,130);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(&FreeSansBold18pt7b);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.print(timeClient.getFormattedTime());

        M5.Lcd.setCursor(col+100,130);
        if(TMP117_T<-30.00 || (TMP117_T <= -0.009 && TMP117_T >= -0.011) || isnan(TMP117_T))//Set to yellow for errors as we consider anything below -30 as a bad input. -0.01 being the value when sensor is not correctly connected.
        {
            M5.Lcd.setTextColor(TFT_YELLOW);
        }
        else
        {
            M5.Lcd.setTextColor(prvCol48);
        }
        M5.Lcd.print(TMP117_T);
        M5.Lcd.println("'C");
        //Color designation depends on the previously DISPLAYED value, not the previously measured one.
        prv_TMP117_T = TMP117_T; //Used to know if the next measure is gonna be > or < current measure.
    }
    if (tReadSensor.getRunCounter() <= 1)
    {
        //Enable the tShutScreenOff task on the first run AFTER we show stuff to avoid having it just shut the screen off immediately.
        tShutScreenOff.enableDelayed(TASK_MINUTE);
    }
    //tReadSensor.yield(&processValuesTMP117); //passes to the scheduler, next pass will be skipping the first step since we now know what sensor we are using.
    tReadSensor.setCallback(&processValuesTMP117); //Looping the task
    tReadSensor.delay(5 * TASK_SECOND);            //Same
}

void displayWiFiFailed()
{
    if(WiFiConnected != true)
    {
        M5.Lcd.setCursor(M5.Lcd.width()-170,M5.Lcd.height()-10);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
        M5.Lcd.setTextColor(TFT_YELLOW);
        M5.Lcd.println("WiFi not connected.");
    }
}

void readButtonsStateCallBack()
{
    //PL_("T2ReadButtonsState");
    M5.update();
    if (M5.BtnA.wasReleased() || M5.BtnB.wasReleased() || M5.BtnC.wasReleased()) {
        blank = false;
        M5.Lcd.setBrightness(100);
        tShutScreenOff.restartDelayed(TASK_MINUTE);
    }
}

void shutScreenOffCallBack()
{
    PL_("tShutScreenOff");
    //the screen "shuts down" after having shown the temperature for 60 seconds.
    blank = true;
    M5.Lcd.clear();
    M5.Lcd.setBrightness(0);
    //t3DisplayValuesLcd.disable();
    tShutScreenOff.disable();//Do not repeat so that we are sure to always wait for 60 seconds
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////START OF IotWebConf CALLBACK METHODS///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleRoot()// Handle web requests to "/" path.
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
///////////////////////////////////////////CONNECTION METHODS////////////////////////////////////////////////////
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
        M5.Lcd.setCursor(0,20);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.println("Connected to WiFi...");
        tReadSensor.delay(2000);//Delay this task to allow us to actually read what we wrote on the screen.
        timeClient.begin();
        tWebConfLoop.setInterval(3*TASK_SECOND);
        tSendToElsa.enableDelayed();
        tCheckWebConfStatus.disable();
    }
    else{
        PL_("WebConf Status != 4 => We are NOT connected");
        M5.Lcd.setCursor(0,20);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.println("Trying to connect to WiFi...");
        //tReadSensor.delay(2000);//Delay this task to allow us to actually read what we wrote on the screen.
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

void aioTimeUpdBackgroundCallback()
{
    if(WiFi.status() == WL_CONNECTED)
    {
        PL_("tIoTimeUpd");
        //io.run();
        timeClient.update();
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////START OF DATA SENDING METHODS//////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void sendDataToElsaCallBack()
{
    //foundTMP117 = true;
    if (foundSHT31)
    {
        sendToElsa_SHT31();
    }
    if (foundTMP117)
    {
        if(TMP117_T>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            sendToElsa_TMP117();
        }
    }
}
void sendToElsa_SHT31()
{
    PL_("sendToElsa_SHT31");
    PL_("Before if WiFi status == Connected");
    if(WiFi.status() == WL_CONNECTED)//this is redundant, we may want to remove this protection as it is already made in initElsaCallBack().
    {
        if(SHT31_T>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            Serial.println("Sending HTTP request...");
            // Make a HTTP request:
            String url;
            url = "/api/put/!s_" + ElsaTemperatureSensorName + "?value=" + String(SHT31_T)
                + "&control=" + String(calculateCheckSUM(ElsaTemperatureSensorName, SHT31_T));
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
                        tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &getElsaCredentialsCallBack);//this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
                        tSendToElsa.enableDelayed();
                    }
                    return;
                }
            }
        }
        tSendToElsa.delay(1000);
        if(SHT31_H>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            Serial.println("Sending HTTP request...");
            // Make a HTTP request:
            String url;
            url = "/api/put/!s_" + ElsaHumiditySensorName + "?value=" + String(SHT31_H)
                + "&control=" + String(calculateCheckSUM(ElsaHumiditySensorName, SHT31_H));
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
                        tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &getElsaCredentialsCallBack);//this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
                        tSendToElsa.enableDelayed();
                    }
                    return;
                }
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


void sendToElsa_TMP117()
{
    PL_("sendToElsa_TMP117");
    PL_("Before if WiFi = connected");

    if(WiFi.status() == WL_CONNECTED && isValidInput(ElsaTemperatureSensorName))//NOT REDUNDANT AS THE TASK WILL ALWAYS RESTART FROM sendToAdaCallBack() METHOD.
    {
        Serial.println("Sending HTTP request...");
        // Make a HTTP request:
        String url;
        PP_("ElsaTemperatureSensorName : ");
        PL_(ElsaTemperatureSensorName);
        url = "/api/put/!s_" + ElsaTemperatureSensorName + "?value=" + String(TMP117_T)
            + "&control=" + String(calculateCheckSUM(ElsaTemperatureSensorName, TMP117_T));
        /*url = "/api/put/!s_TMP117?value=" + String(25.00)
            + "&control=" + String(calculateCheckSUM("TMP117", 25.00));*/
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
////////////////////////////////////////////START OF GENERIC METHODS/////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

