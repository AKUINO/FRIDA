#define IO_USERNAME "Insert AIO USERNAME HERE"
#define IO_KEY "Insert AIO KEY HERE"

#define WIFI_SSID "Insert WIFI SSID HERE"
#define WIFI_PASS "Insert WIFI PWD HERE"

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////VARIABLES DECLARATION///////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Scheduler ts;                                           //TASK SCHEDULER
//NETWORK
WiFiClient myWiFiClient;                                //CLIENT USED TO SEND HTTP REQUESTS TO ELSA
const long utcOffsetInSeconds = 3600;                   //GMT +1
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
HttpClient client = HttpClient(myWiFiClient, "phenics.gembloux.ulg.ac.be", 80);

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS); //Since this declaration allows to easily do a WiFi.begin, we will only need to connect once with "io.connect" for communications with both Adafruit and Elsa
AdafruitIO_Feed *feedSHT31_T = io.feed("SHT31_sonde température");
AdafruitIO_Feed *feedSHT31_H = io.feed("SHT31_sonde humidité");
AdafruitIO_Feed *feedTMP117 = io.feed("TMP117_sonde température");

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////TASKS AND CALLBACK METHODS/////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//SENSORS & DISPLAY CALLBACK METHODS
void readSensorCallBack();

void readButtonsStateCallBack();
void shutScreenOffCallBack();

//NETWORK CALLBACK METHODS

void connectInitCallBack();
void aioTimeUpdBackgroundCallback();
void initElsaCallBack();
void initAdaCallBack();


//SENSORS & DISPLAY TASKS
Task tReadSensor(5 * TASK_SECOND, TASK_FOREVER, &readSensorCallBack, &ts, true);

Task tReadButtonsState(200, TASK_FOREVER, &readButtonsStateCallBack, &ts, true); //200 milliseconds
Task tShutScreenOff(TASK_MINUTE, 1, &shutScreenOffCallBack, &ts, false);

//NETWORK TASKS

Task tConnect(TASK_SECOND, TASK_FOREVER, &connectInitCallBack, &ts, true);
Task tIoTimeUpd(10 * TASK_SECOND, TASK_FOREVER, &aioTimeUpdBackgroundCallback, &ts, false);
Task tSendToElsa(&initElsaCallBack, &ts);
Task tSendToAda(&initAdaCallBack, &ts);

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
    //TASKS
    tSendToElsa.waitFor(tConnect.getInternalStatusRequest()); //initElsaCallBack() will only start after the connection to the WiFi is successful => depends on the statusRequest tConnect sends.
    tSendToAda.waitFor(tConnect.getInternalStatusRequest()); //Same with sendToAdaCallBack().
    //M5STACK & SENSORS
    M5.begin(true,false,true); // init lcd, serial, but don't init sd card
    M5.Power.begin();
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setTextSize(2);
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
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////END OF SETUP////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////START OF MAIN LOOP//////////////////////////////////////////////////
//  - Main loop should only execute the task schedulers for a well functioning MultiThreading
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop ()
{
  ts.execute();
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////END OF MAIN LOOP////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        M5.Lcd.println("'H");
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
//////////////////////////////////////////END OF DISPLAY METHODS/////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////CONNECTION METHODS////////////////////////////////////////////////////
//  - connectInitCallBack : Initializes WiFi connection then sets tConnect to connectCheckCallBack()
//  - connectCheckCallBack : Periodically check if connected to WiFi, Re-request connection every 5 seconds,
//                   Stop trying after a timeout. Sets the StatusRequest for tSendToElsa
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void connectInitCallBack()
{
    Serial.println("WiFi connectInitCallBack");
    //WiFi.begin(WIFI_SSID, WIFI_PASS);
    //M5.Lcd.clear();
    M5.Lcd.setCursor(0,20);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.println("Connecting to WiFi...");
    tReadSensor.delay(2000);//Delay this task to allow us to actually read what we wrote on the screen.
    //Since this method already does a WiFi.begin, we will only need to connect once with "io.connect" for communications with both Adafruit and Elsa
    PL_("Connecting to Adafruit IO");
    io.connect();
    //NTP :
    timeClient.begin();
    //yield(); //this is for another esp i think
    tConnect.yield(&connectCheckCallBack); // This will pass control back to Scheduler and then continue with connection checking
}

void connectCheckCallBack()
{
    Serial.println("WiFi connectCheckCallBack");
    if (WiFi.status() == WL_CONNECTED) {                // Connection established
        WiFiConnected = true;
        Serial.println("WiFi Connected : WL_CONNECTED");
        PL_();
        bool connectedToAda = false;
        if(io.status() < AIO_CONNECTED)
        {
            PP_(io.statusText());
            PP_(" CONNECTING to AdafruitIO...");
            connectedToAda = false;
            for(int i = 0; i<50; i++)
            {
               PP_(".");
               if(i == 49)
               {
                   PL_();
               }
            }
        }
        else{
            connectedToAda = true;
        }
        if(connectedToAda == true)
        {
            PL_(io.statusText());
            M5.Lcd.setCursor(0,50);
            M5.Lcd.setTextSize(1);
            M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
            M5.Lcd.setTextColor(TFT_WHITE);
            M5.Lcd.println(io.statusText());
            tReadSensor.delay(2000);//Delay this task to allow us to actually read what we wrote on the screen.
            tIoTimeUpd.enableDelayed();
            tConnect.disable(); //Task will no longer be executed
        }
    }
    else{
        Serial.println("WiFi Not Connected : !WL_CONNECTED");
        if (tConnect.getRunCounter() % 5 == 0) {          // re-request connection every 5 seconds
            WiFi.disconnect(true);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
        if (tConnect.getRunCounter() == CONNECT_TIMEOUT) {  // Connection Timeout
            Serial.println("WiFi Failed : CONNECT_TIMEOUT");
            tConnect.getInternalStatusRequest()->signal(CONNECT_FAILED);  // Signal unsuccessful completion
        }
    }
}

void aioTimeUpdBackgroundCallback()
{
    if(WiFi.status() == WL_CONNECTED)
    {
        PL_("tIoTimeUpd");
        io.run();
        timeClient.update();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////END OF CONNECTION METHODS//////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////START OF DATA SENDING METHODS//////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//ADAFRUIT IO
void initAdaCallBack()
{
    Serial.println("Connection attempt To Ada : initElsaCallBack()");
    if ( tConnect.getInternalStatusRequest()->getStatus() != CONNECT_OK ) {  // Check status of the tConnect Task
        Serial.println("Cannot send to Ada -> not connected : WiFi !CONNECT_OK");
        WiFiConnected = false;
        resetTaskForWiFiConnection(tSendToAda, AdafruitIO);
        return;
    }
    tSendToAda.set(TASK_MINUTE, TASK_FOREVER, &sendToAdaCallBack);
    tSendToAda.enableDelayed();
}

void sendToAdaCallBack()
{
    if (foundSHT31)
    {
        sendToAda_SHT31();
    }
    if (foundTMP117)
    {
        if(TMP117_T>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            sendToAda_TMP117();
        }
    }
}

void sendToAda_SHT31()
{
    if(WiFi.status() == WL_CONNECTED)//Redundant as is is already done in initAdaCallBack(). We may want to remove this.
    {
        if(SHT31_T>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            Serial.print("sending -> SHT31_T ");
            Serial.println(SHT31_T);
            feedSHT31_T->save(SHT31_T);
        }
        if(SHT31_H>=-30.00)//Only send actual data without errors (due to missing Vcc cable ?).
        {
            Serial.print("sending -> SHT31_H ");
            Serial.println(SHT31_H);
            feedSHT31_H->save(SHT31_H);
        }
    }
    else //Adafruit IO's methods to check connectivity just seem to check the WiFi connection so this should be enough protection for now.
    {
        PL_("WiFi Status != WL_CONNECTED");
        WiFiConnected = false;
        resetTaskForWiFiConnection(tSendToAda, AdafruitIO);
    }
    client.stop();
    tSendToAda.yield(&initAdaCallBack);
}

void sendToAda_TMP117()
{
    if(WiFi.status() == WL_CONNECTED)//Redundant as is is already done in initAdaCallBack(). We may want to remove this.
    {
        Serial.print("sending -> TMP117_T ");
        Serial.println(TMP117_T);
        feedTMP117->save(TMP117_T);
    }
    else //Adafruit IO's methods to check connectivity just seem to check the WiFi connection so this should be enough protection for now.
    {
        PL_("WiFi Status != WL_CONNECTED");
        WiFiConnected = false;
        resetTaskForWiFiConnection(tSendToAda, AdafruitIO);
    }
    client.stop();
    tSendToAda.yield(&initAdaCallBack);
}

//ELSA
void initElsaCallBack()
{
    Serial.println("Connection attempt To Elsa : initElsaCallBack()");
    if ( tConnect.getInternalStatusRequest()->getStatus() != CONNECT_OK ) {  // Check status of the tConnect Task
        Serial.println("Cannot send to Elsa -> not connected : WiFi !CONNECT_OK");
        WiFiConnected = false;
        resetTaskForWiFiConnection(tSendToElsa, Elsa);
        return;
    }
    tSendToElsa.set(TASK_MINUTE, TASK_FOREVER, &sendDataToElsaCallBack);
    tSendToElsa.enableDelayed();
}

void sendDataToElsaCallBack()
{
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
            url = "/api/put/!s_SHT31T?value=" + String(SHT31_T)
                + "&control=" + String(calculateCheckSUM("SHT31T", SHT31_T));
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
                        tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &initElsaCallBack);//this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
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
            url = "/api/put/!s_SHT31H?value=" + String(SHT31_H)
                + "&control=" + String(calculateCheckSUM("SHT31H", SHT31_H));
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
                        tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &initElsaCallBack);//this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
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
    tSendToElsa.yield(&initElsaCallBack);
}

void sendToElsa_TMP117()
{
    PL_("sendToElsa_TMP117");
    PL_("Before if WiFi = connected");

    if(WiFi.status() == WL_CONNECTED)//this is redundant, we may want to remove this protection as it is already made in initElsaCallBack().
    {
        Serial.println("Sending HTTP request...");
        // Make a HTTP request:
        String url;
        url = "/api/put/!s_TMP117?value=" + String(TMP117_T)
            + "&control=" + String(calculateCheckSUM("TMP117", TMP117_T));
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
                    tSendToElsa.set(5 * TASK_SECOND, TASK_FOREVER, &initElsaCallBack); //this means the Task will re check for WiFi connection anyway. Not sure if it is very useful though.
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
    tSendToElsa.yield(&initElsaCallBack);
}


void resetTaskForWiFiConnection (Task& dataSendingTask, PlatformUsed platformUsed)
{
    switch (platformUsed) //Using a switch in case we want to add new possibilities in a future development. Btw, switch only allows integer/char so we use an ENUM which is basically hidden integers++++++++++
    {
        case AdafruitIO:
            dataSendingTask.disable();
            dataSendingTask.setCallback(&initAdaCallBack);
            dataSendingTask.waitFor(tConnect.getInternalStatusRequest()); //connectToAdaCallBack() will only start after the connection to the WiFi is successful => depends on the statusRequest tConnect sends.
            break;
        case Elsa:
            dataSendingTask.disable();
            dataSendingTask.setCallback(&initElsaCallBack);
            dataSendingTask.waitFor(tConnect.getInternalStatusRequest()); //connectToElsaCallBack() will only start after the connection to the WiFi is successful => depends on the statusRequest tConnect sends.
            break;
        default:
            PL_("resetTaskForWiFiConnection DEFAULT SWITCH, an error happened !");
            //We might want to add all Tasks in charge of sending data to an online platform in an array and reset all these tasks here ?
            break;
    }
    tConnect.setCallback(&connectInitCallBack); //Restarting WiFi connection
    tConnect.enableDelayed();
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
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////END OF DATA SENDING METHODS////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
