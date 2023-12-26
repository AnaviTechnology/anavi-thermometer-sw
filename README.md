# ANAVI Thermometer software

This repository contains the default open source Arduino sketch for the smart WiFi dev board [ANAVI Thermometer](https://anavi.technology/).

You can [build and upload the sketch through the Arduino IDE](https://www.youtube.com/watch?v=HMIkPuz0ZJs) **or** [download a pre-compiled binary of a stable release and flash it with `esptool`](https://blog.anavi.technology/?p=209). It you just want to upload the latest stable release it is easier and recommended to use `esptool.py`.

## User Guide

[ANAVI Thermometer User's Manual](https://github.com/AnaviTechnology/anavi-docs/blob/master/anavi-thermometer/anavi-thermometer.md)

## Dependencies

The default firmware of ANAVI Thermometer depends on the following Arduino libraries, which must be added in the Arduino IDE before compiling the sketch.

* [WiFiManager by tzapu](https://github.com/tzapu/WiFiManager) (version 0.15.0)
* [ArduinoJson by Benoit Blanchon](https://arduinojson.org/) (version 6.21.4)
* [PubSubClient by Nick O'Leary](https://pubsubclient.knolleary.net/) (version 2.7.0)
* [Adafruit HTU21DF Library by Adafruit](https://github.com/adafruit/Adafruit_HTU21DF_Library) (version 1.0.1)
* [Adafruit APDS9960 Library by Adafruit](https://github.com/adafruit/Adafruit_APDS9960) (version 1.0.5)
* [DHT sensor library by Adafruit](https://github.com/adafruit/DHT-sensor-library) (version 1.3.4)
* [U8g2 by oliver](https://github.com/olikraus/u8g2) (version 2.34.22)
* [OneWire](https://github.com/PaulStoffregen/OneWire) (version 2.3.5)
* [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) (version 3.8.0)
* [Adafruit Unified Sensor by Adafruit](https://github.com/adafruit/Adafruit_Sensor) (version 1.0.2)
* [Adafruit BMP085 Unified](https://github.com/adafruit/Adafruit_BMP085_Unified) (version 1.0.0)
* [NTPClient by Fabrice Weinberg](https://github.com/arduino-libraries/NTPClient) (version 3.1.0)
