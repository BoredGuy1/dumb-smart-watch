/* I have decided to rewire some things to make the final circuit less messy. This is the result
OLED SCL -> A5
OLED SDA -> A4
Power -> 3K resistor -> 1K resistor -> GND
Power -> 3K resistor -> A0
GPS RX (Green) -> 12
GPS TX (White) -> 10
Power -> VCC
GND -> "Mode" button -> 7
GND -> "Lock" button -> 8
GND -> "Edit" button -> 9
*/

// Libraries
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <EEPROM.h> // Storing config options. I didn't use wear leveling since I thought that was overkill
#include <TimeLib.h>

// Pins
#define TXPin 12 // FROM GPS, TO PC
#define RXPin 10 // TO GPS, FROM PC
#define modePin 7 // Button for changing the "mode" (e.g. location mode to time mode)
#define lockPin 8 // Button for "locking" changes (e.g. enabling you to change units)
#define editPin 9 // Button for config (e.g. changing units, incrementing time)
#define batteryPin 14
#define GPS_BAUD 9600
#define SERIAL_MONITOR_BAUD 115200

// GPS serial setup
TinyGPSPlus gps; // The TinyGPSPlus object
SoftwareSerial ss(RXPin, TXPin); // The serial connection to the GPS device

// Voltage monitor setup
#define NUM_READINGS 64
#define READ_INTERVAL 10
#define REFERENCE_VOLTS 1.118
unsigned int readings[NUM_READINGS];
byte readIndex = 0;
unsigned long lastmsVolts = millis();

// OLED display setup
#define RST_PIN -1 // Define proper RST_PIN if required
#define I2C_ADDRESS 0x3C
SSD1306AsciiAvrI2c display;
// Avoiding switch statements at the expense of dynamic RAM
const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* CARD[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "N"};
const char* WKDAY[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Button setup to avoid clattering
byte modeLastState = HIGH;
byte lockLastState = HIGH;
byte editLastState = HIGH;
byte modeState;
byte lockState;
byte editState;

// Retrieve config settings from EEPROM
#define LOC_ADDR 0 // Location (altitude) unit address
#define NAV_ADDR 1 // Navigation (speed) unit address
#define OFF_ADDR 2 // Time offset address
byte locUnits = (EEPROM.read(LOC_ADDR) == 0) ? 1 : EEPROM.read(LOC_ADDR); // If the EEPROM says 0 (ie hasn't been written to) assign default value
byte navUnits = (EEPROM.read(NAV_ADDR) == 0) ? 1 : EEPROM.read(NAV_ADDR);
int hourOffset = (EEPROM.read(OFF_ADDR) == 0) ? 0 : (EEPROM.read(OFF_ADDR) - 127); // Subtract by 127 to convert byte to signed int
byte runUnits = 1; // 1 means mi, 2 means km

// Variables to avoid constant resyncing. I find that constant resyncing causes issues
#define SYNC_INTERVAL 60000 // Time between each resync attempt (in ms). Default is every minute
unsigned long lastSync = millis(); // Counter that tells the time of last sync
bool recentlySynced = false; // Flag that gets enabled when changing time zones

// Run mode variables
unsigned long startTime; // When the stopwatch is started
unsigned long elapsedTime = 0; // Time of last pause (in s)
unsigned long totalTime; // Total (displayed) time (in s)
byte runState = 0; // Whether or not "run mode" is active (run mode's clocks and such will update in the background)
float totalDistance = 0; // Total distance in km
float lastLat; // Used to calculate total distance
float lastLon; // with current coordinates and distance formula

// Other random stuff
#define KM_TO_MI 0.6213712 // For conversion
byte configTimeMode = false;
byte mode = 1; // The "mode" that the display will show (location mode shows altitude, navigation mode shows speed and course, etc)

void setup()
{
  if (RST_PIN >= 0) display.begin(&Adafruit128x64, I2C_ADDRESS, RST_PIN);
  else display.begin(&Adafruit128x64, I2C_ADDRESS);
  display.setFont(Adafruit5x7);
  analogReference(INTERNAL);
  pinMode(modePin, INPUT_PULLUP);
  pinMode(lockPin, INPUT_PULLUP);
  pinMode(editPin, INPUT_PULLUP);
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = analogRead(batteryPin); // Initialize to whatever A0 is, we can smooth later
  Serial.begin(SERIAL_MONITOR_BAUD);
  ss.begin(GPS_BAUD);
}

void loop()
{
  updateBatteryVoltage();
  if (((millis() - lastSync) > SYNC_INTERVAL) || (recentlySynced == false)) updateClock();
  updateRunStats();
  if ((checkButton(modePin, modeState, modeLastState) == true) && (configTimeMode == false)) {
    if (mode >= 4) mode = 1;
    else mode = mode + 1;
    display.clear();
  }
  while (ss.available() > 0)
    gps.encode(ss.read());
  switch (mode) {
    case 1:
      timeMode();
      break;
    case 2:
      locMode();
      break;
    case 3:
      navMode();
      break;
    case 4:
      runMode();
      break;
  }

}





bool checkButton(byte pin, byte &state, byte &lastState)
{
  // Only trigger code once, even if button is held
  state = digitalRead(pin);
  if(lastState == HIGH && state == LOW)
  {
    lastState = state;
    return(true);
  }
  else
  {
    lastState = state;
    return(false);
  }
}





void updateBatteryVoltage()
{
  if ((millis() - lastmsVolts) > READ_INTERVAL)
  {
    readings[readIndex] = analogRead(batteryPin);
    readIndex = readIndex + 1;
    if (readIndex >= NUM_READINGS) readIndex = 0;
    lastmsVolts = millis();
  }
}




float batteryVoltage()
{
  unsigned int avgVoltVal = 0;
  for (int i = 0; i < NUM_READINGS; i++) avgVoltVal = avgVoltVal + readings[i];
  avgVoltVal = avgVoltVal/NUM_READINGS;
  float batteryVolts = (avgVoltVal / 255.75) * REFERENCE_VOLTS; // divided by (1023/4) instead of (1023) because voltage is 1/4 of original
  return batteryVolts;
}





void showTopBar() // Shows day, month, and time to nearest minute
{
  display.set1X();
  display.setCursor(0, 0);
  if (gps.date.isValid() && (timeStatus() != timeNotSet))
  {
    display.print(WKDAY[weekday() - 1]);
    display.print(F(", "));
    if (day() < 10) display.print(F("0"));
    display.print(day());
    display.print(F(" "));
    display.print(MONTHS[month() - 1]);
  }
  else    
  {
    display.print(F("..........."));
  }
  
  display.setCursor(96, 0);
  if (gps.time.isValid() && (timeStatus() != timeNotSet))
  {
    if (hour() < 10) display.print(F("0"));
    display.print(hour());
    display.print(F(":"));
    if (minute() < 10) display.print(F("0"));
    display.println(minute());
  }
  else
  {
    display.println(F("....."));
  }
  display.println(F("---------------------"));
}





void showBottomBar() // Shows number of satellites in use and battery level
{
  display.set1X();
  display.setCursor(0, 48);
  display.println(F("---------------------"));
  display.print(F("Bat:"));
  int bat = int((batteryVoltage() - 3.0)*100); // Operating range is 3-4v, 4v=max (100%) and 3v=dead (0%)
  if (bat > 100) bat = 100; // Rounding in case battery exceeds 4v by a bit
  if (bat < 0) bat = 0; // Same as above but with 3v
  display.print(bat);
  display.print(F("%  "));
  
  display.setCursor(84, 56);
  if (gps.satellites.value() < 10) display.print(F(" "));
  display.print(gps.satellites.value());
  if (gps.satellites.value() == 1) display.print(F(" Sat "));
  else display.print(F(" Sats"));
}





void locMode()
{
  if (checkButton(editPin, editState, editLastState) == true)
  {
    if (locUnits >= 4) locUnits = 1;
    else locUnits = locUnits + 1;
  }

  showTopBar();
  
  if (gps.location.isValid())
  {
    display.print(F("Lat:"));
    display.print(gps.location.lat(), 5);
    display.println(F("    "));
    display.print(F("Lon:"));
    display.print(gps.location.lng(), 5);
    display.println(F("    "));
  }
  else
  {
    display.println(F("Lat:INVALID    "));
    display.println(F("Lon:INVALID    "));
  }

  display.print(F("Alt:"));
  if (checkButton(lockPin, lockState, lockLastState) == true)
  {
    EEPROM.update(LOC_ADDR, locUnits);
    display.print(F("Units saved"));
    delay(1000);
    display.setCursor(24, 32);
    display.println(F("           "));
  }
  else
  {
    if (gps.altitude.isValid())
    {
      switch (locUnits) {
        case 1:
          display.print(gps.altitude.meters());
          display.println(F("m    "));
          break;
        case 2:
          display.print(gps.altitude.miles());
          display.println(F("mi    "));
          break;
        case 3:
          display.print(gps.altitude.kilometers());
          display.println(F("km    "));
          break;
        case 4:
          display.print(gps.altitude.feet());
          display.println(F("ft    "));
          break;
        }
    }
    else
    {
      display.println(F("INVALID    "));
    }
  }

  display.println();
  showBottomBar();
}





void navMode()
{
  if (checkButton(editPin, editState, editLastState) == true)
  {
    if (navUnits >= 4) navUnits = 1;
    else navUnits = navUnits + 1;
  }

  showTopBar();
  
  if (gps.location.isValid())
  {
    display.print(F("Lat:"));
    display.print(gps.location.lat(), 5);
    display.println(F("    "));
    display.print(F("Lon:"));
    display.print(gps.location.lng(), 5);
    display.println(F("    "));
  }
  else
  {
    display.println(F("Lat:INVALID    "));
    display.println(F("Lon:INVALID    "));
  }

  display.print(F("Course:"));
  if (gps.course.isValid())
  {
    display.print(gps.course.deg());
    display.print(F("("));
    display.print(CARD[int((gps.course.deg()/45)+.5)]);
    display.println(F(")    "));
  }
    else
  {
    display.println(F("INVALID"));
  }

  display.print(F("Speed:"));
  if (checkButton(lockPin, lockState, lockLastState) == true)
  {
    EEPROM.update(NAV_ADDR, navUnits);
    display.print(F("Units saved"));
    delay(1000);
    display.setCursor(36, 40);
    display.println(F("           "));
  }
  else
  {
    if (gps.speed.isValid())
    {
      switch (navUnits) {
        case 1:
          display.print(gps.speed.knots());
          display.println(F("kn     "));
          break;
        case 2:
          display.print(gps.speed.mph());
          display.println(F("mph     "));
          break;
        case 3:
          display.print(gps.speed.mps());
          display.println(F("m/s     "));
          break;
        case 4:
          display.print(gps.speed.kmph());
          display.println(F("km/h     "));
          break;}
    }
    else
    {
      display.println(F("INVALID"));
    }
  }
  showBottomBar();
}





void timeMode()
{
  display.setCursor(0, 0);
  display.set2X();
  
  if (gps.date.isValid() && (timeStatus() != timeNotSet))
  {
    display.print(F("  "));
    if (day() < 10) display.print(F("0"));
    display.print(day());
    display.print(F(" "));
    display.println(MONTHS[month() - 1]);
    display.print(F(" "));
    display.print(WKDAY[weekday() - 1]);
    display.print(F(" "));
    display.println(year());

  }
  else
  {
    display.println(F("  ------"));
    display.println(F(" --------"));
  }

  if (gps.time.isValid() && (timeStatus() != timeNotSet))
  {
    display.print(F(" "));
    if (hour() < 10) display.print(F("0"));
    display.print(hour());
    display.print(F(":"));
    if (minute() < 10) display.print(F("0"));
    display.print(minute());
    display.print(F(":"));
    if (second() < 10) display.print(F("0"));
    display.print(second());
    display.println(F(" "));
  }
  else
  {
    display.println(F(" --:--:-- "));
  }



  // Config is only used to set time
  if (checkButton(lockPin, lockState, lockLastState) == true)
  {
    if (configTimeMode == false) configTimeMode = true; // Enable config mode
    else
    {
      EEPROM.update(OFF_ADDR, (hourOffset + 127)); // Add 127 because EEPROM can only store bytes, not unsigned ints
      recentlySynced = false;
      configTimeMode = false;
    }
  }
  
  if (configTimeMode == true)
  {
    if (checkButton(modePin, modeState, modeLastState) == true)
    {
      if (hourOffset <= -12) hourOffset = 14;
      else hourOffset = hourOffset - 1;
      recentlySynced = false;
    }
    if (checkButton(editPin, editState, editLastState) == true)
    {
      if (hourOffset >= 14) hourOffset = -12;
      else hourOffset = hourOffset + 1;
      recentlySynced = false;
    }
    if ((hourOffset < 10) && (hourOffset > -10)) display.setCursor(6, 48);
    display.print(F(" (UTC"));
    if (hourOffset >= 0) display.print(F("+"));
    display.print(hourOffset);
    display.print(F(")  "));
  }
  else
  {
  int bat = int((batteryVoltage() - 3.0)*100); // Operating range is 3-4v, 4v=max (100%) and 3v=dead (0%)
  if (bat > 100) bat = 100; // Rounding in case battery exceeds 4v by a bit
  if (bat < 0) bat = 0; // Same as above but with 3v
  
  if (bat == 100) display.print(F(" Bat:"));
  else if (bat < 10) display.print(F("  Bat:"));
  else
  {
    display.setCursor(6, 48);
    display.print(F(" Bat:"));
  }
  
  display.print(bat);
  display.print(F("%  "));
  }
}





void runMode()
{
  if (checkButton(editPin, editState, editLastState) == true)
  {
    if (runState == 0)
    {
      startTime = now();
      runState = 1; // Start
    }
    else
    {
      elapsedTime = totalTime;
      runState = 0; // Stop
    }
  }
  if (checkButton(lockPin, lockState, lockLastState) == true)
  {
    if ((runState == 0) && (totalTime != 0)) // Reset if timers aren't running AND stats aren't already at 0
    {
      elapsedTime = 0;
      totalTime = 0;
      totalDistance = 0;
      display.clear();
    }
    else // Otherwise change units
    {
      if (runUnits == 1) runUnits = 2;
      else runUnits = 1;
    }
  }



  // These are here to hold the stopwatch data in HH:MM:SS notation but I will use the minutes and seconds later as well
  unsigned long hours = totalTime / 3600;
  unsigned long minutes = (totalTime % 3600) / 60;
  unsigned long seconds = ((totalTime % 3600) % 60);

  display.setCursor(0, 0);
  display.set1X();
  display.print(F("Time "));
  display.set2X();
  
  if (hours == 0)
  {
    if (minutes < 10) display.print(F("0"));
    display.print(minutes);
    display.print(F(":"));
    if (seconds < 10) display.print(F("0"));
    display.println(seconds);
  }
  else
  {
    if (hours < 10) display.print(F("0"));
    display.print(hours);
    display.print(F(":"));
    if (minutes < 10) display.print(F("0"));
    display.println(minutes);
  }



  display.set1X();
  display.print(F("Dist "));
  display.set2X();
  if (runUnits == 1)
  {
    if (totalDistance * KM_TO_MI < 10) display.print(F("0"));
    display.print(totalDistance * KM_TO_MI);
  }
  else
  {
    if (totalDistance < 10) display.print(F("0"));
    display.print(totalDistance);
  }
  display.set1X();
  display.println();  
  display.setCursor(90, 16);
  if (runUnits == 1) display.println(F("mi"));
  else display.println(F("km"));



  float pace = 1/gps.speed.mps(); // Seconds per meter
  pace = pace*1000; // Seconds per km
  if (runUnits == 1) pace = pace/KM_TO_MI; // Seconds per mi
  int paceInt = int(pace + .5); // Makeshift rounding with type casting
  minutes = paceInt/60;
  seconds = paceInt%60;

  display.print(F("Pace "));
  display.set2X();
  if (minutes > 99)
  {
    display.print(F("--:--"));
  }
  else
  {
    if (minutes < 10) display.print(F("0"));
    display.print(minutes);
    display.print(F(":"));
    if (seconds < 10) display.print(F("0"));
    display.print(seconds);
  }
  display.set1X();
  display.println();
  display.setCursor(90, 32);
  if (runUnits == 1) display.println(F("/mi"));
  else display.println(F("/km"));



  float avgPace = totalTime/totalDistance; // Seconds per km
  if (runUnits == 1) avgPace = avgPace/KM_TO_MI; // Seconds per mi
  int avgPaceInt = int(avgPace + .5); // Makeshift rounding with type casting
  minutes = avgPaceInt/60; 
  seconds = avgPaceInt%60;

  display.print(F("Avg. "));
  display.set2X();
  if (minutes > 99)
  {
    display.print(F("--:--"));
  }
  else
  {
    if (minutes < 10) display.print(F("0"));
    display.print(minutes);
    display.print(F(":"));
    if (seconds < 10) display.print(F("0"));
    display.print(seconds);
  }
  display.set1X();
  display.println(); 
  display.print(F("Pace "));
  display.setCursor(90, 48);
  if (runUnits == 1) display.println(F("/mi"));
  else display.println(F("/km"));
}





void updateClock()
{
  if (gps.time.isValid() && gps.date.isValid() && (gps.time.age() < 1000) && (gps.time.age() < 1000))
  {
    setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), gps.date.day(), gps.date.month(), gps.date.year());
    adjustTime(3600 * hourOffset);
    recentlySynced = true;
    lastSync = millis();
  }
}





void updateRunStats()
{
  if (runState == 1)
  {
  totalTime = elapsedTime + (now() - startTime);
  totalDistance = totalDistance + (TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), lastLat, lastLon)/1000);
  }
  lastLat = gps.location.lat();
  lastLon = gps.location.lng();
}
