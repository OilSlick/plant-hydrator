//Board = Adafruit ESP32 Feather

#include "config.h"                   //Needed for Adafruit IO
#include <SPI.h>                      //For OLED
#include <Wire.h>                     //For OLED
#include <Adafruit_GFX.h>             //For OLED
#include <Adafruit_SSD1306.h>         //For OLED

//Begin OLED shite
#define OLED_RESET 13
Adafruit_SSD1306 display(OLED_RESET);


#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

//end OLED shite


const int PumpPin = 21;               //Controls the LCLV glass
const int hygroPin = A2;              //analog in from hygrometer
int soilMoisture;                     //soil moisture reading from hygrometer
int lowestRaw = 2047;                 //used while calibrating
int rawReading;                       //the raw reading from FC-28

bool debug = false;                   //Enable debugging with "1"
bool WiFiError = false;               //Track WiFi connection error
bool IOconnERROR = false;             //Track connection failures
bool timeError = false;               //If can't get time, keep pump off
bool pumpOn = false;                  //track pump status
int pumpTime = 35;                    //How many seconds of pumping
int pumpTimeOn;                       //Track when pump was turned on
int thisSecond;                       //right.this.second
int lastPumpOn = 0;                   //prevent over-watering
int PumpOnceInHours = 24;             //example; 24 = pump only once every 24 hours
bool recentlyPumped = false;          //Resets at currenthour + PumpOnceInHours

//NTP settings
//From: https://github.com/espressif/arduino-esp32/issues/821
const char* NTP_SERVER = "sg.pool.ntp.org";
const char* TZ_INFO    = "SGT-8:00:00"; //sets SGT to 8 hours ahead of UTC

struct tm timeinfo;
int tmSecond;
int tmMinute;
int tmHour;
int tmDay;
int tmMonth;
int tmYear;
int tmWeekday;

//For Adafruit IO
AdafruitIO_Feed *nightmodeButtonFeed = io.feed("alarm-nightmode");
AdafruitIO_Feed *logFeed = io.feed("alarm-log");

void setup() {
  analogReadResolution(11); 
  analogSetAttenuation(ADC_6db);
  pinMode(PumpPin, OUTPUT);          //Pin controls pump
  digitalWrite(PumpPin, LOW);       //keep pump off 

  // init OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
  display.display();
  delay(2000);

  // Clear the buffer.
  display.clearDisplay();
  display.display();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Display Ready");
  display.display();
  display.clearDisplay();
  
  Serial.begin(115200);
  if ( debug == 1)
  {
    Serial.setDebugOutput(true);
    if ( Serial )
    {
      Serial.println("Debug enabled");
    }
  }
  //Connect to WiFi
  //wdt_reset();
  Connect();
  
  if ( WiFiError == 0)
  {
    Serial.println("Connecting to Adafruit IO");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connecting logs");
    display.display();
    display.clearDisplay();
    // connect to io.adafruit.com
    // io.connect();
    // attach message handler for the each feed

    logFeed->onMessage(handleMessage);

    // wait for a connection
    int IOconnAttempt = 0;
    while (io.status() < AIO_CONNECTED && IOconnAttempt <= 60)
    {
      Serial.print(".");
      display.print(".");
      display.display();
      display.clearDisplay();
      delay(500);
      IOconnAttempt++;
    }
    if ( IOconnAttempt > 60 )
    {
      IOconnERROR = 1;
      if ( Serial )
      {
        Serial.println("");
        Serial.println("Aborting connection attempt to Adafruit IO");
        Serial.print("IP address:   ");
        Serial.println(WiFi.localIP()); 
      }
      display.setCursor(0,0);
      display.println("Aborting log cnx");
      display.display();
      display.clearDisplay();
    }
    IOconnAttempt = 0;

    // we are connected
    if ( Serial );
    {
      Serial.println();
      Serial.println(io.statusText());
    }
    display.setCursor(0,0);
    display.println(io.statusText());
    display.display(); 
    display.clearDisplay();
  }
  printLocalTime();
  getTimeValues();
  display.setTextSize(3);
  display.setCursor(0,0);
  display.print(tmHour);
  display.print(":");
  display.print(tmMinute);
  display.display(); 
  display.clearDisplay();
}

void loop() {
  if ( WiFiError != 0)
  {
    Connect();
  }
  
  soilMoisture = analogRead(hygroPin);
  rawReading = soilMoisture;
  //soilMoisture = constrain(soilMoisture, 700, 2047);
  soilMoisture = map(soilMoisture, 1200, 2047, 100, 0);
  //soilMoisture = map(soilMoisture,550,0,0,100); //map analog vals 0 - 1023 to 0-100
  
  if ( rawReading < lowestRaw )
  {
    lowestRaw = rawReading;
    Serial.print("Raw reading: ");
    Serial.println(rawReading);
    displayTimeMoisture();
  }
  
  if ( debug == true )
  {
    printLocalTime();
  }
  if ( WiFiError == 0 && IOconnERROR == 0 )
  {
    io.run();
  }
   
  getTimeValues();
  
  thisSecond = ( tmHour * 3600 ) + ( tmMinute * 60 ) + tmSecond; //get seconds of the day
  if ( pumpOn == false ) //display new time every minute
  {
    displayTimeMoisture();
  }
  
  if ( soilMoisture < 30 && timeError == false && recentlyPumped == false ) 
  {
    recentlyPumped = true;
    pumpTimeOn = ( tmHour * 3600 ) + ( tmMinute * 60 ) + tmSecond; //get seconds of the day to measure pump duration

    digitalWrite(PumpPin, HIGH);       //turn pump on 
    pumpOn = true;
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println("Pump on");
    display.display(); 
    display.clearDisplay();
  }
  if ( pumpOn == true && thisSecond > pumpTimeOn + pumpTime )  //basic pump timer
  {
    digitalWrite(PumpPin, LOW);       //turn pump off 
    pumpOn = false;
  }
}
void displayTime()
{
  getTimeValues();
  display.setTextSize(3);
  display.setCursor(0,0);
  display.print(&timeinfo, "%H:%M");
  display.display(); 
  display.clearDisplay();
}
void displayTimeMoisture()
{
  getTimeValues();
  display.setTextSize(3);
  display.setCursor(0,0);
  display.print(&timeinfo, "%H:%M");
  display.setTextSize(1);
  display.setCursor(90,0);
  display.print("Moist: ");
  display.setCursor(90,10);
  display.print(soilMoisture);
  display.print("%");
  display.setCursor(0,25);
  display.print("Lowest raw: ");
  display.print( lowestRaw );
  if ( recentlyPumped == true )
  {
    display.setTextSize(1);
    display.setCursor(121,25);
    display.print("*");
  }
  display.display(); 
  display.clearDisplay();
}
void getTimeValues()
{
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    timeError = true;
    return;
  } else {
    timeError = false;
  }
  tmSecond = timeinfo.tm_sec;
  tmMinute = timeinfo.tm_min;
  tmHour = timeinfo.tm_hour;
  tmDay = timeinfo.tm_mday;
  tmMonth = timeinfo.tm_mon + 1;
  tmYear = timeinfo.tm_year + 1900;
  tmWeekday = timeinfo.tm_wday + 1;
}
void printLocalTime()
{
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    timeError = true;
    return;
  } else {
    timeError = false;
  }
  Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
  //Parameters see: http://www.cplusplus.com/reference/ctime/strftime/
}
void Connect()
{
  if ( WiFiError == 0 )
  {
    if (Serial)
    {
      Serial.print("Connecting to ");
      Serial.println(WIFI_SSID);
    }
    display.setCursor(0,0);
    display.println("Connecting to ");
    display.println(WIFI_SSID);
    display.display();
    display.clearDisplay();

    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      int Attempt = 0;
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(500);
        Attempt++;
        Serial.print(".");
        if (Attempt == 60)
        {
          if (Serial)
          {
            Serial.println();
            Serial.println("WiFi connection failed");
          }
          display.setCursor(0,0);
          display.println("WiFi connection failed to ");
          display.println(WIFI_SSID);
          display.display();
          display.clearDisplay();
          WiFiError = 1;
          IOconnERROR = 1; //If no WiFi connection, prevent connection to Adafruit IO
          timeError = true;  //if true, pump won't turn on (because it can't turn off without time)
          return;
        }
      }
    }
    if (Serial)
          {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
          }
    display.setCursor(0,0);
    display.println("WiFi connected");
    display.println("IP address: ");
    display.println(WiFi.localIP());
    display.display(); 
  }
  configTzTime(TZ_INFO, NTP_SERVER);
  if (getLocalTime(&timeinfo, 10000)) {  // wait up to 10sec to sync
    Serial.println(&timeinfo, "Time set: %B %d %Y %H:%M:%S (%A)");
  } else {
    Serial.println("Time not set");
  }
  
}
