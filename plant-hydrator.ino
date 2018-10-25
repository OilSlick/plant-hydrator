//Board = Adafruit ESP32 Feather

#include "config.h"                   //Needed for Adafruit IO
#include <SPI.h>                      //For OLED
#include <Wire.h>                     //For OLED
#include <Adafruit_GFX.h>             //For OLED
#include <Adafruit_SSD1306.h>         //For OLED
#include <esp32-hal-bt.c>             //For Bluetooth (not yet utilized October 25, 2018)

//enable deep sleep from http://educ8s.tv/esp32-deep-sleep-tutorial/
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  3360        /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int bootCount = 0;

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

bool debug = true;                   //Enable debugging with "true"
bool debugPrinted = false;            //track if we've printed debug data (don't spam serial console)

bool WiFiError = false;               //Track WiFi connection error
bool IOconnERROR = false;             //Track connection failures
bool timeError = false;               //If can't get time, keep pump off
bool pumpOn = false;                  //track pump status
int pumpTime = 35;                    //How many seconds of pumping
int pumpTimeOn;                       //Track when pump was turned on
int thisSecond;                       //right.this.second
int lastPumpOn = 0;                   //prevent over-watering
int PumpOnceInHours = 24;              //example; 24 = pump only once every 24 hours
bool recentlyPumped = false;          //Resets at currenthour + PumpOnceInHours
int lastPumpHour;                     //track hour of last pump time
int minMoisture = 30;                 //Minimum moisture percentage before pump turns on (or after-which it turns off)

bool loggedMoisture = false;          //log moisture to io.Adafruit only once an hour
bool loggedPump = false;              //log pump action to io.Adafruit only once per action

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
AdafruitIO_Feed *moistureFeed = io.feed("moisture-log");

void setup() {
  btStop();  //shut down bluetooth and save some power from: https://desire.giesecke.tk/index.php/2018/02/05/switch-off-bluetooth-and-wifi/
  
  //Increment boot number and print it every reboot
  ++bootCount;
  
  Serial.println("Boot number: " + String(bootCount));
  //configure sleep time
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  
  analogReadResolution(12); //default is 12 (bits)
  analogSetAttenuation(ADC_11db);
  //2 settings above from: https://github.com/espressif/arduino-esp32/issues/683#issuecomment-336681899
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
  if ( debug == true )
  {
    Serial.setDebugOutput(true);
    if ( Serial )
    {
      Serial.println("Debug enabled");
    }
  }
  //Connect to WiFi
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
    io.connect();
    // attach message handler for the each feed

    moistureFeed->onMessage(handleMessage);

    // wait for a connection
    int IOconnAttempt = 0;
    while (io.status() < AIO_CONNECTED && IOconnAttempt <= 60)
    {
      Serial.print(".");
      display.print(".");
      display.display();
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
  moistureFeed->save("DEVICE: ready");
}

void loop() {
  if ( WiFiError != 0)   //If pervious wifi error, re-attempt connection
  {
    Connect();
  }
  cycleCheck();                       //checks if we're within a pump cycle (if inside, not okay to pump)
  if ( debug == true && debugPrinted == false && ( tmSecond % 15 == 0) )  //every 15 seconds, print debug data
  {
    debugPrinted = true;
    Serial.println(" ");
    printLocalTime();
    Serial.print("WiFiError: ");
    Serial.println( WiFiError );
    Serial.print("IOconnERROR: ");
    Serial.println( IOconnERROR );
    Serial.print("recently pumped: ");
    Serial.println( recentlyPumped );
    Serial.print("lastPumpHour: ");
    Serial.println( lastPumpHour );
    Serial.print("minMoisture: ");
    Serial.println( minMoisture );
    Serial.println(" ");
    Serial.print("soilMoisture: ");
    Serial.println(soilMoisture);

    Serial.println("=======================");
  } else if ( debugPrinted == true && ( tmSecond % 15 != 0) )
  {
    debugPrinted = false;
  }
  readMoisture();
  if ( tmMinute == 0 && loggedMoisture == false )               //top of every hour, log moisture
  {
   moistureFeed->save( soilMoisture );
   loggedMoisture = true;
  } else if ( tmMinute != 0 && loggedMoisture == true )          //reset conditions so we can log again next hour
  {
    loggedMoisture = false;
  }
  if ( soilMoisture > minMoisture && pumpOn == true )    //failsafe so we don't over-water if pump timer fails or soil hydrated
  { 
    turnPumpOff(); 
  }
  
  if ( rawReading < lowestRaw )
  {
    lowestRaw = rawReading;
    Serial.print("New low (raw): ");
    Serial.println(rawReading);
    displayTimeMoisture();
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
  
  if ( soilMoisture < minMoisture && timeError == false && recentlyPumped == false ) 
  {
    recentlyPumped = true;
    lastPumpHour = tmHour;
    pumpTimeOn = ( tmHour * 3600 ) + ( tmMinute * 60 ) + tmSecond; //get seconds of the day to measure pump duration

    if ( Serial )
    {
      Serial.println("Pump On");
    }

    digitalWrite(PumpPin, HIGH);       //turn pump on 
    pumpOn = true;
    display.setTextSize(2);
    display.setCursor(10,0);
    display.println("Pump on");
    display.display(); 
    display.clearDisplay();
    moistureFeed->save("PUMP: On");
  }
  if ( pumpOn == true && thisSecond > pumpTimeOn + pumpTime )  //basic pump timer
  {
    turnPumpOff();
  }
  if ( soilMoisture > minMoisture && pumpOn == false && debug == false && tmMinute == 0 )  //if moisture is good and pump is off and it's top of the hour, take a nap
  {
    if ( Serial )
    {
      Serial.println("snoozer");
    }
    //moistureFeed->save("DEVICE: sleep");
    display.clearDisplay();
    display.display();
    delay(1000);
    esp_deep_sleep_start();  //take a snoozer
  }
  else if ( soilMoisture > minMoisture && pumpOn == false && debug == false && ( tmMinute % 5 == 0 ) )
  {
    esp_sleep_enable_timer_wakeup(240 * uS_TO_S_FACTOR);  //take a four minute snoozer. 
    display.clearDisplay();
    display.display();
    delay(1000);
    esp_deep_sleep_start();  //take a snoozer
  }
}
void turnPumpOff()
{
  digitalWrite(PumpPin, LOW);       //turn pump off 
  pumpOn = false;
  moistureFeed->save("PUMP: Off");
  if ( Serial )
  {
    Serial.println("Pump Off");
  }
}
void readMoisture()
{
  soilMoisture = analogRead(hygroPin);
  rawReading = soilMoisture;
  soilMoisture = constrain(soilMoisture, 1200, 4095);
  soilMoisture = map(soilMoisture, 1200, 4095, 100, 0); //Low voltage/analog read = high moisture content
}
void cycleCheck()
{
  getTimeValues();
  if ( lastPumpHour > tmHour )        //compensate for "round the clock" or past midnight issue
  {
    tmHour += 24;                     //add 24 hours to compensate for rounding the clock 
  }
  if ( tmHour > lastPumpHour + PumpOnceInHours)
  { 
    if ( recentlyPumped == true )    //outside pump cycle; okay to pump
    {
      recentlyPumped = false;
      moistureFeed->save("New pump cycle");
    }
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
  if ( debug == true );
  {
    display.setCursor(90,20);
    display.print("Debug");
  }
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
