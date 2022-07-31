# dumb-smart-watch
Learning electrical engineering the hard way

## What is this?
This is a watch built from scratch using an Arduino. It's "smart" because it has a GPS, but it's also "dumb" because it doesn't report heartrate, steps, or anything else a smartwatch would normally report. It's just a GPS watch. It was originally a personal project to help me learn about microcontrollers, but I decided to upload the code and schematics in case anyone else wanted to make something similar.

## Overview
All components run off a single CR123A battery. The battery is connected to a voltage divider that divides the voltage into 1/4 of the original, which allows the Arduino to measure it against the internal reference voltage and predict how much power is left based on voltage drop off charts. The watch treats 4v as 100% charged and 3v as 0% charged.

The Arduino receives data from a GPS, and certain data is sent to the OLED display depending on which "mode" the watch is currently in. The watch has 4 "modes":
- Time Mode - displays date, time, and battery in large text.
- Location Mode - displays latitude, longitude, and altitude.
- Navigation Mode - displays latitude, longitude, direction, and speed.
- Run Mode - stopwatch that tracks time elapsed and distance traveled.

The watch contains 3 buttons which are used for changing modes, configuring units, and starting/stopping/resetting the stopwatch (more on that later), as well as a switch that opens the circuit and turns the watch off.

## Hardware
Components:
- 1 Arduino Pro Mini
- 1 BN-220 GPS module
- 1 128x64 OLED display
- 3 1k resistors
- 1 CR123A battery
- 1 switch
- 3 push buttons
A picture of the schematic has been provided.

## Software
This watch uses the Arduino programming language. The code can be viewed by clicking on the Arduino file.

## Controls
Time Mode:
- Middle button - adjust time zone
  - Bottom button - move forward 1 hour
  - Middle button - stop adjusting time zone
  - Top button - move back 1 hour
- Top button - next mode

Location Mode:
- Bottom button - display altitude in other units (ft, m, mi, or km)
- Middle button - save the current unit as default
- Top button - next mode

Navigation Mode:
- Bottom button - display speed in other units (mph, m/s, km/h, or kn)
- Middle button - save the current unit as default
- Top button - next mode

Run Mode:
- Bottom button - start/stop the stopwatch
- Middle button - reset the stopwatch if stopwatch is paused, otherwise changes units between mi and km
- Top button - next mode
