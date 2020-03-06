#include "Adafruit_SHT31.h"
#include "AdafruitIO_WiFi.h"
#include <M5Stack.h>
#include <Wire.h>            // Used to establish serial communication on the I2C bus
#include <SparkFun_TMP117.h> // Used to send and recieve specific information from our sensor
#include <MCP41xxx.h>
#include <WiFi.h>
#include <NTPClient.h>

const long utcOffsetInSeconds = 3600;//GMT +1
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
Adafruit_SHT31 sht31 = Adafruit_SHT31(); //Humidity & Temperature sensor
TMP117 sensor48; // Initalize Temperature-only sensor
int i2cAddr = 0x48;//Address of the SHT31 sensor
bool foundSHT31 = false;
bool foundTMP117 = false;
#define WIFI_SSID "TP-LINK_1BFC"
#define WIFI_PASS "59384748"
/*
#define WIFI_SSID "lennyRasp"
#define WIFI_PASS "teteb1234"*/
#define IO_USERNAME "MaximeSamyn_LA172155"
#define IO_KEY "aio_IMaG61dRu9UX94qbOBfw1ggvoxqi"
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
#define IO_LOOP_DELAY 15000
unsigned long lastUpdate;

// set up the 'counter' feed
AdafruitIO_Feed *feedSHT31_T = io.feed("SHT31_sonde température");
AdafruitIO_Feed *feedSHT31_H = io.feed("SHT31_sonde humidité");
AdafruitIO_Feed *feedTMP117 = io.feed("TMP117_sonde température");

void setup() {

    M5.begin(true,false,true); // init lcd, serial, but don't init sd card
    M5.Power.begin();
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setRotation(3); //Invert the display

    //Attempts at muting that awkward sound produced by the ESP didn't work
    //M5.Speaker.mute();
    //dacWrite(25,0);
    //ledcDetachPin(SPEAKER_PIN);
    //pinMode(SPEAKER_PIN, INPUT);

    Serial.begin(115200);

    M5.Lcd.clear();
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println("Searching sensors...");

    //Sensors detection//
    if(sht31.begin(0x44)) //Test if Humidity sensor is there
    {
        foundSHT31 = true;
        Serial.println("Found SHT31");
        M5.Lcd.println("Found SHT31");
        delay(5000);
        M5.Lcd.clear();
    }
    //Test if Temperature-Only sensor is there
    else if(sensor48.begin(i2cAddr) == true) // Function to check if the sensor will correctly self-identify with the proper Device ID/Address
    {
        foundTMP117 = true;
        sensor48.setContinuousConversionMode();
        Serial.println("Found TMP117");
        M5.Lcd.println("Found TMP117");
        delay(2000);
        M5.Lcd.clear();
    }
    else //If nothing was detected then shut down
    {
        Serial.println("Couldn't find SHT31");
        Serial.println("Couldn't find TMP117");
        M5.Lcd.println("Couldn't find SHT31");
        M5.Lcd.println("Couldn't find TMP117");
        foundTMP117 = false;
        foundSHT31 = false;
        delay (10000);
        M5.Lcd.clear();
        M5.Lcd.setBrightness(0);
        while (1);
    }

    Serial.print("Connecting to "); //Connexion WiFi pour le client HTTP vers Elsa
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if(WiFi.status()==WL_CONNECTED)
    {
      Serial.println("Successfully connected to WiFi");
    }
    Serial.print("Connecting to Adafruit IO");
    M5.Lcd.clear();
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println("Connecting to Adafruit IO");
    M5.Lcd.println("...");
    // connect to io.adafruit.com
    io.connect();
    /* Only needed to subscribe to a feed (MQTT)
    feedSHT31->onMessage(handleSHT31_feed());
    feedTMP117->onMessage(handleTMP117_feed());
    */
    // wait for a connection
    while(io.status() < AIO_CONNECTED) {
        Serial.print(".");
        delay(400);
        M5.Lcd.clear();
        M5.Lcd.setCursor(0,0);
        delay(100);
        M5.Lcd.println("Connecting to Adafruit IO");
        M5.Lcd.println("...");
    }
    // we are connected
    Serial.println();
    Serial.println(io.statusText());
    M5.Lcd.println(io.statusText());
    delay(1500);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
    M5.Lcd.setBrightness(100);

    //NTP :
    timeClient.begin();

    Serial.println("END of SETUP");
}
    //Déclaration des varaibles
    float SHT31_T;
    float SHT31_H;
    float TMP117_T;

    bool dataReady = true;// Data Ready is a flag for the conversion modes - in continous conversion the dataReady flag should always be high
    int i = 0;
    int looping = 0;
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

void loop() {
    //Serial.println("START of LOOP");
    // process messages and keep connection alive
    io.run();

    timeClient.update();

    looping ++;
    //Serial.println(looping);
    if (looping > 50)//works as a makeshift delay : only take a new measure after we've counted 100 times.
    {
        //Serial.println("INSIDE of IF(looping)");
        looping = 0;
        i+=1;

        if (foundSHT31 == true)
        {
            if(dataReady == true)// Function to make sure that there is data ready to be printed, only prints temperature values when data is ready
            {
                processValuesSHT31();
            }
        }
        else if(foundTMP117 == true)
        {
            if (sensor48.dataReady() == true)
            {
                processValuesTMP117();
            }
        }
    }
    //Serial.println("OUT of IFs && M5.UPDATE");
    delay(10);
    M5.update();
    if (M5.BtnA.wasReleased() || M5.BtnB.wasReleased() || M5.BtnC.wasReleased()) {
        i = 0;
        blank = false;
        M5.Lcd.setBrightness(100);
    }
    else if ( i > 60 ) { //the screen "shuts down" after having shown the temperature 60 times.
        i = 0;
        blank = true;
        M5.Lcd.clear();
        M5.Lcd.setBrightness(0);
    }
    //avoid publishing until IO_LOOP_DELAY milliseconds have passed.
    if(millis() > (lastUpdate + IO_LOOP_DELAY) && i > 1)//i>1 to prevent an awkward very first message of value "0" to AdafruitIO which would ruin the monitoring
    {
        publishData();
        lastUpdate = millis();
    }
    //Serial.println("OUT of LOOP");
}

void processValuesSHT31()
{
    //Serial.println("INSIDE of IF(dataReady)");
    //on lit les informations de ce capteur
    SHT31_T = sht31.readTemperature();
    SHT31_T = floor(100*SHT31_T)/100;
    SHT31_H = sht31.readHumidity();
    SHT31_H = floor(100*SHT31_H)/100;
    debugSHT31_ReadSHT31Values(SHT31_T, SHT31_H);

    rewrite = !blank;

    SHT31_DesignateDisplayColor(SHT31_T, SHT31_H);

    if (rewrite) {
        SHT31_DisplayValues(SHT31_T, SHT31_H);
    }
    prv_SHT31_T = SHT31_T;
    prv_SHT31_H = SHT31_H;
}

void debugSHT31_ReadSHT31Values(float SHT31_T, float SHT31_H)
{
    if (!isnan(SHT31_T)) {  // check if 'is not a number'
        if (true/*Serial1*/) {
        Serial.print(";I;t = ");
        Serial.print(SHT31_T);
        }
    }
    else {
        if (true/*Serial1*/)
        {
          Serial.print(";W;t = null");
        }
    }

    if (!isnan(SHT31_H)) {  // check if 'is not a number'
        if (true/*Serial1*/) {
        Serial.print(";H = ");
        Serial.println(SHT31_H);
        }

    }
    else {
        if (true/*Serial1*/) {
          Serial.println(";W;H = null");
        }
    }
}

void SHT31_DesignateDisplayColor(float SHT31_T, float SHT31_H)
{
    if (SHT31_T > prv_SHT31_T) {
        prvColT = RED;
    }
    else if (SHT31_T < prv_SHT31_T) {
        prvColT = BLUE;
    }
    else {
        if (prvColT != WHITE && !blank) rewrite = true;
        prvColT = WHITE;
    }

    if (SHT31_H > prv_SHT31_H) {
        prvColH = RED;
    }
    else if (SHT31_H < prv_SHT31_H) {
        prvColH = BLUE;
    }
    else {
        if (prvColH != WHITE && !blank) rewrite = true;
        prvColH = WHITE;
    }
}

void SHT31_DisplayValues(float SHT31_T, float SHT31_H)
{
      M5.Lcd.clear();
  //Display NTP
    M5.Lcd.setCursor(0,130);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.print(timeClient.getFormattedTime());

    M5.Lcd.setCursor(col+100,row);
    if(SHT31_T<-30.00)//Set to yellow for errors as we consider anything below -30 as a bad input
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
    if(SHT31_H<-30.00)//Set to yellow for errors as we consider anything below -30 as a bad input
    {
        M5.Lcd.setTextColor(TFT_YELLOW);
    }
    else
    {
        M5.Lcd.setTextColor(prvColH);
    }
    M5.Lcd.print(SHT31_H);
    M5.Lcd.println("'H");
}

void processValuesTMP117()
{
   TMP117_T = sensor48.readTempC();
   TMP117_T = floor(100*TMP117_T)/100;
   if(TMP117_T <= 0 && TMP117_T > -1.0 && sensor48.begin(i2cAddr) == false)// unplugging TMP117 gives negative value close to 0 whereas unplugging SHT31 gives NAN so we're checking that.
   {
        TMP117_T = NAN;
   }
    // Print temperature in °C
    bool rewrite = !blank;
    TMP117_DesignateDisplayColor(TMP117_T);

    if (rewrite) {
      TMP117_DisplayValues(TMP117_T);
    }
    prv_TMP117_T = TMP117_T;
}
void debugTMP117_ReadSHT31Values(float TMP117_T)
{
    Serial.print("48: ");
    Serial.print(TMP117_T);
    Serial.println("°C");
}
void TMP117_DesignateDisplayColor(float TMP117_T)
{
    if (TMP117_T > prv_TMP117_T) {
        prvCol48 = RED;
    }
    else if (TMP117_T < prv_TMP117_T) {
        prvCol48 = BLUE;
        }
    else {
        if (prvCol48 != WHITE && !blank) rewrite = true;
        prvCol48 = WHITE;
    }
}
void TMP117_DisplayValues(float TMP117_T)
{
  M5.Lcd.clear();

  //Display NTP
  M5.Lcd.setCursor(0,130);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.print(timeClient.getFormattedTime());


    M5.Lcd.setCursor(col+100,130);
    if(TMP117_T<-30.00)//Set to yellow for errors as we consider anything below -30 as a bad input
    {
        M5.Lcd.setTextColor(TFT_YELLOW);
    }
    else
    {
        M5.Lcd.setTextColor(prvCol48);
    }
    M5.Lcd.print(TMP117_T);
    M5.Lcd.println("'C");
}

void publishData()//Sends data to Adafruit AND Elsa
{
    if(foundSHT31)
    {
        publishFeedSHT31(SHT31_T, SHT31_H);//Publish to Adafruit
        sendToElsaSHT31(SHT31_T, SHT31_H);//Publish to Elsa
    }
    else if (foundTMP117)
    {
        if(TMP117_T>=-30.00)
        {
        publishFeedTMP117(TMP117_T);//Publish to Adafruit
        sendToElsaTMP117(TMP117_T);//Publish to Elsa
        }
    }
}

void publishFeedSHT31(float SHT31_T, float SHT31_H)
{

    if(SHT31_T>=-30.00)//Only send actual data without errors due to missing Vcc cable.
    {
    Serial.print("sending -> SHT31_T ");
    Serial.println(SHT31_T);
    feedSHT31_T->save(SHT31_T);
    }
    if(SHT31_H>=-30.00)//Only send actual data without errors due to missing Vcc cable.
    {
    Serial.print("sending -> SHT31_H ");
    Serial.println(SHT31_H);
    feedSHT31_H->save(SHT31_H);
    }
}

void publishFeedTMP117(float TMP117_T)
{
    Serial.print("sending -> TMP117_T ");
    Serial.println(TMP117_T);
    feedTMP117->save(TMP117_T);
}

void sendToElsaSHT31(float SHT31_T, float SHT31_H)
{
    if(SHT31_T >=-30.00 || SHT31_H >=-30.00)//Only send actual data without errors due to missing Vcc cable.
    {
        WiFiClient client;// = WiFiClient("http://phenics.gembloux.ulg.ac.be", (uint16_t)80);
        while((!client.connect("139.165.227.10", 80))) {
          Serial.println("connection failed, trying again");
          delay(500);
        }
        Serial.println("Before if WiFi status == Connected");
        if(WiFi.status() == WL_CONNECTED)
        {
                Serial.println("Connecting to HTTP Client...");
                if(SHT31_T>=-30.00)//Only send actual data without errors due to missing Vcc cable.
                {
                    client.print("GET /api/put/!s_SHT31T?value=");
                    client.print(SHT31_T);
                    client.print("&control=");
                    client.print(calculateCheckSUM("SHT31T", SHT31_T));
                    client.println(" HTTP/1.1");
                    client.println("");
                    unsigned long timeout = millis();
                    while (client.available() == 0) {
                        if (millis() - timeout > 5000) {
                            Serial.println(">>> Client Timeout !");
                            client.stop();
                            return;
                        }
                    }
                    // Read all the lines of the reply from server and print them to Serial
                    while(client.available()) {
                        String line = client.readStringUntil('\r');
                        Serial.print(line);
                    }
                }
                delay(1000);
                if(SHT31_H>=-30.00)//Only send actual data without errors due to missing Vcc cable.
                {
                    client.print("GET /api/put/!s_SHT31H?value=");
                    client.print(SHT31_H);
                    client.print("&control=");
                    client.println(calculateCheckSUM("SHT31H", SHT31_H));
                    client.println(" HTTP/1.1");
                    client.println("");
                    unsigned long timeout = millis();
                    while (client.available() == 0) {
                        if (millis() - timeout > 5000) {
                            Serial.println(">>> Client Timeout !");
                            client.stop();
                            return;
                        }
                    }
                    // Read all the lines of the reply from server and print them to Serial
                    while(client.available()) {
                        String line = client.readStringUntil('\r');
                        Serial.print(line);
                    }
                }
                Serial.println();
                Serial.println("closing connection");
        }
    }
}
void sendToElsaTMP117 (float TMP117_T)
{
    WiFiClient client;// = WiFiClient("http://phenics.gembloux.ulg.ac.be", (uint16_t)80);
    while((!client.connect("139.165.227.10", 80))) {
      Serial.println("connection failed, trying again");
      delay(500);
    }
    Serial.println("Before if WiFi status == Connected");

    if(WiFi.status() == WL_CONNECTED)
    {
            Serial.println("Connecting to HTTP Client...");

            // Make a HTTP request:
            client.print("GET /api/put/!s_TMP117?value=");
            client.print(TMP117_T);
            client.print("&control=");
            client.print(calculateCheckSUM("TMP117", TMP117_T));
            client.println(" HTTP/1.1");
            //host: phenics...
            client.println("");

            unsigned long timeout = millis();
            while (client.available() == 0) {
                if (millis() - timeout > 5000) {
                    Serial.println(">>> Client Timeout !");
                    client.stop();
                    return;
                }
            }
            // Read all the lines of the reply from server and print them to Serial
            while(client.available()) {
                String line = client.readStringUntil('\r');
                Serial.print(line);
            }
            Serial.println();
            Serial.println("closing connection");
    }
    else{
        Serial.println("WiFi Status != WL_CONNECTED");
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