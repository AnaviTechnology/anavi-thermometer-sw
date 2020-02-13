// -*- mode: c++; indent-tabs-mode: nil; c-file-style: "stroustrup" -*-
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

// Username and password to use for MQTT.  If these are defined, they
// will override whatever is supplied on the wifi configuration page.
// If MQTT_USERNAME and MQTT_PASSWORD are set to 0, an anonymous MQTT
// connection will be used.

// #define MQTT_USERNAME "user"
// #define MQTT_PASSWORD "password"

// Server to connect to.  If defined, this overrides the setting on
// the wifi configuration page.

// #define MQTT_SERVER "mydomain.duckdns.example.org"

// If HOME_ASSISTANT_DISCOVERY is defined, the Anavi Thermometer will
// publish MQTT messages that makes Home Assistant auto-discover the
// device.  See https://www.home-assistant.io/docs/mqtt/discovery/.
//
// This requires PubSubClient 2.7.

#define HOME_ASSISTANT_DISCOVERY 1

// If PUBLISH_CHIP_ID is defined, the Anavi Thermometer will publish
// the chip ID using MQTT.  This can be considered a privacy issue,
// and is disabled by default.
#undef PUBLISH_CHIP_ID

// Should Over-the-Air upgrades be supported?  They are only supported
// if this define is set, and the user configures an OTA server on the
// wifi configuration page (or defines OTA_SERVER below).  If you use
// OTA you are strongly adviced to use signed builds (see
// https://arduino-esp8266.readthedocs.io/en/2.5.0/ota_updates/readme.html#advanced-security-signed-updates)
//
// You perform an OTA upgrade by publishing a MQTT command, like
// this:
//
//   mosquitto_pub -h mqtt-server.example.com \
//     -t cmnd/$MACHINEID/update \
//     -m '{"file": "/anavi.bin", "server": "www.example.com", "port": 8080 }'
//
// The port defaults to 80.
#define OTA_UPGRADES 1
// #define OTA_SERVER "www.example.com"

// The GPIO2 pin is connected to two things on the ANAVI Thermometer:
//
// - Internally on the ESP-12E module, it is connected to a blue
//   status LED, that lights up whenever the pin is driven low.
//
// - On the ANAVI Thermometer board, it is connected to the DHT22
//   temperature and humidity sensor.  Whenever a measurements is
//   made, both the ESP8266 on the ESP-12E module and the DHT22 sensor
//   drives the pin low.  One measurement drives it low several times:
//
//      - At least 1 ms by the ESP8266 to start the measurement.
//      - 80 us by the DHT22 to acknowledge
//      - 50 us by the DHT22 for each bit transmitted, a total of 40 times
//
//   In total, this means it is driven low more than 3 ms for each
//   measurement.  This results in a blue flash from the status LED
//   that is clearly visible, especially in a dark room.
//
// Unfortunately, there seems to be no way to get rid of this blue
// flash in software.  But we can mask it by turning on the blue LED
// all the time.  The human eye won't detect that it is turned off
// briefly during the measurement.  Technically, we still follow the
// letter of the data sheet, as that only requires the start pulse to
// be "at least 1-10 ms", and 10 seconds is clearly "at least 10
// ms". :-)
//
// This hack will likely increase the power consumption slightly.
// #define ESP12_BLUE_LED_ALWAYS_ON

// In the ANAVI Thermometer, GPIO12 is designed to be connected to the
// external DS18B20 waterproof temperature sensor using a 1-Wire bus.
// If you don't connect any 1-Wire sensor, you can instead use that
// pin as an input pin for a button.  Connect a normally-closed switch
// in series with a 470 ohm resistor between the DATA and GND of the
// DS18B20 connector to use this.  The 470 ohm resistor ensures we
// don't overload the GPIO port when it probes for 1-Wire devices.
//
// Whenever the switch is pressed (so that DATA is driven high), the
// ANAVI Thermometer will publish an MQTT message to
// $WORKGROUP/$MACHINEID/button/1 with the value
//
//     { "pressed": "ON" }
//
// Once the button is released, a new MQTT message will be sent:
//
//     { "pressed": "OFF" }
//
// The BUTTON_INTERVAL defines the minimum delay (in milliseconds)
// between sending the above messages.  This serves two purposes:
// ensure the button is debounced, and ensure the listener have time
// to react to the message.
//
// The autodetection code only works if the button is not pressed
// while the ANAVI Thermometer is started.
#define BUTTON_INTERVAL 100

// By default, you can perform a factory reset on the ANAVI
// Thermometer in a few ways:
//
//  - press and hold the button while the ANAVI Thermometer is
//    connected to a WiFi network
//
//  - press and hold the button within 2 seconds of powering up the
//    ANAVI Thermometer
//
// If the ANAVI Thermometer fails to connect to a WiFi network, you
// will also be able to reconfigure it via its web interface.
//
// By defining OTA_RESET you will also be able to issue a factory
// reset by sending cmnd/$MACHINEID/factory-reset to the device.  This
// is disabled by default, as it is too easy to unconfigure the device
// by mistake.
#undef OTA_FACTORY_RESET

// Define to PUBLISH_FREE_HEAP publish the amount of free heap space
// to MQTT, as <workgroup>/<machineid>/free-heap, with a value on
// this format:
//
//     { "bytes": 32109 }
#define PUBLISH_FREE_HEAP

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESP8266httpUpdate.h>

//needed for library
#include <DNSServer.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>        //https://github.com/knolleary/pubsubclient

#include <MD5Builder.h>
// For DHT22 temperature and humidity sensor
#include <DHT.h>
// For OLED display
#include <U8g2lib.h>
#include <Wire.h>
// For DS18B20 (waterproof) temperature sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Adafruit_HTU21DF.h"
#include "Adafruit_APDS9960.h"
// For BMP180
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>

#define PRODUCT "ANAVI Thermometer"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

#define DHTPIN  2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define ONE_WIRE_BUS 12
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

Adafruit_HTU21DF htu = Adafruit_HTU21DF();

Adafruit_APDS9960 apds;

//Configure supported I2C sensors
const int sensorHTU21D =  0x40;
const int sensorBH1750 = 0x23;
const int sensorBMP180 = 0x77;
const int i2cDisplayAddress = 0x3c;

// Configure pins
const int pinAlarm = 16;
const int pinButton = 0;

unsigned long sensorPreviousMillis = 0;
const long sensorInterval = 10000;

bool haveButton = false;
bool buttonState = false;
unsigned long buttonPreviousMillis = 0;

unsigned long mqttConnectionPreviousMillis = millis();
const long mqttConnectionInterval = 60000;

// Set temperature coefficient for calibration depending on an empirical research with
// comparison to DS18B20 and other temperature sensors. You may need to adjust it for the
// specfic DHT22 unit on your board
float temperatureCoef = 0.9;

// Similar, for the DS18B20 sensor.
float dsTemperatureCoef = 1.0;

// The BMP180 sensor can measure the air pressure.  If the sea-level
// pressure is known, you can then compute the altitude.  You can tell
// the ANAVI Thermometer the current sea-level pressure via the
// cmnd/<machineid>/sea-level-pressure MQTT topic.  The value should
// be a floating point number (such as "1028.4").  Negative numbers
// are interpreted as "unknown".
//
// If the sea-level pressure is known, the height above sea level (in
// meters) will be published to the MQTT topic:
//
//     <workgroup>/<machineid>/BMPaltitude
//
// using the format
//
//     { altitude: 72.3 }
float configured_sea_level_pressure = -1;

// If configured_altitude is set to value below this, it will be
// treated as "unknown".  We can't use 0, because e.g. the surface of
// the Dead Sea is more than 400 meters below the sea level (and
// dropping lower every year).  Man has drilled more than 12 km below
// the surface at Kola superdeep borehole.  Using a limit of -20000
// should be low enough.
#define MIN_ALTITUDE (-20000)

// The BMP180 sensor can measure the air pressure.  If the altitude is
// known, you can then compute the sea-level pressure.  You can tell
// the ANAVI Thermometer the current altitude via the
// cmnd/<machineid>/altitude MQTT topic.  The value should be a
// floating point number (such as "72.3").  Numbers below -20000 are
// interpreted as "unknown".  Use a retained MQTT message unless you
// want to re-publish it every time the thermometer restarts.
//
// If the altitude is known, the sea-level pressure (in hPa) will be
// published to the MQTT topic:
//
//     <workgroup>/<machineid>/BMPsea-level-pressure
//
// using the format
//
//     { pressure: 1028.4 }
//
// (Note that you can tell the ANAVI Thermometer both the sea-level
// pressure and the altitude.  It will then compute both the altitude
// based on the sea-level pressure, and the sea-level pressure based
// on the altitude.  This can give you an idea of how accurate these
// calculations are, but is probably seldom useful.)
float configured_altitude = MIN_ALTITUDE - 2;

float dhtTemperature = 0;
float dhtHumidity = 0;
float dsTemperature = 0;
float sensorTemperature = 0;
float sensorHumidity = 0;
uint16_t sensorAmbientLight = 0;

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "mqtt.eclipse.org";
char mqtt_port[6] = "1883";
char workgroup[32] = "workgroup";
// MQTT username and password
char username[20] = "";
char password[20] = "";
#ifdef HOME_ASSISTANT_DISCOVERY
char ha_name[32+1] = "";        // Make sure the machineId fits.
#endif
#ifdef OTA_UPGRADES
char ota_server[40];
#endif
char temp_scale[40] = "celsius";

// Set the temperature in Celsius or Fahrenheit
// true - Celsius, false - Fahrenheit
bool configTempCelsius = true;


// MD5 of chip ID.  If you only have a handful of thermometers and use
// your own MQTT broker (instead of iot.eclips.org) you may want to
// truncate the MD5 by changing the 32 to a smaller value.
char machineId[32+1] = "";

//flag for saving data
bool shouldSaveConfig = false;

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

#ifdef OTA_UPGRADES
char cmnd_update_topic[12 + sizeof(machineId)];
#endif

#ifdef OTA_FACTORY_RESET
char cmnd_factory_reset_topic[19 + sizeof(machineId)];
#endif

char cmnd_restart_topic[13 + sizeof(machineId)];

char cmnd_slp_topic[5 + 19 + sizeof(machineId)];
char cmnd_altitude_topic[5 + 9 + sizeof(machineId)];

char line1_topic[11 + sizeof(machineId)];
char line2_topic[11 + sizeof(machineId)];
char line3_topic[11 + sizeof(machineId)];
char cmnd_temp_coefficient_topic[14 + sizeof(machineId)];
char cmnd_ds_temp_coefficient_topic[20 + sizeof(machineId)];
char cmnd_temp_format[16 + sizeof(machineId)];

// The display can fit 26 "i":s on a single line.  It will fit even
// less of other characters.
char mqtt_line1[26+1];
char mqtt_line2[26+1];
char mqtt_line3[26+1];

String sensor_line1;
String sensor_line2;
String sensor_line3;

bool need_redraw = false;

char stat_temp_coefficient_topic[14 + sizeof(machineId)];
char stat_ds_temp_coefficient_topic[20 + sizeof(machineId)];

struct Uptime
{
    // d, h, m, s and ms record the current uptime.
    int d;                      // Days (0-)
    int h;                      // Hours (0-23)
    int m;                      // Minutes (0-59)
    int s;                      // Seconds (0-59)
    int ms;                     // Milliseconds (0-999)

    // The value of millis() the last the the above was updated.
    // Note: this value will wrap after slightly less than 50 days.
    // In contrast, the above values won't wrap for more than 5
    // million years.
    unsigned long last_millis;
};

struct Uptime uptime;

//callback notifying us of the need to save config
void saveConfigCallback ()
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void drawDisplay(const char *line1, const char *line2 = "", const char *line3 = "", bool smallSize = false)
{
    // Write on OLED display
    // Clear the internal memory
    u8g2.clearBuffer();
    // Set appropriate font
    if ( true == smallSize)
    {
      u8g2.setFont(u8g2_font_ncenR10_tr);
      u8g2.drawStr(0,14, line1);
      u8g2.drawStr(0,39, line2);
      u8g2.drawStr(0,60, line3);
    }
    else
    {
      u8g2.setFont(u8g2_font_ncenR14_tr);
      u8g2.drawStr(0,14, line1);
      u8g2.drawStr(0,39, line2);
      u8g2.drawStr(0,64, line3);
    }
    // Transfer internal memory to the display
    u8g2.sendBuffer();
}

void load_calibration()
{
    if (!SPIFFS.exists("/calibration.json"))
        return;
    File configFile = SPIFFS.open("/calibration.json", "r");
    if (!configFile)
        return;
    const size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    DynamicJsonDocument jsonBuffer(1024);
    auto error = deserializeJson(jsonBuffer, buf.get());
    Serial.print("Loading /calibration.json: ");
    serializeJson(jsonBuffer, Serial);
    Serial.println("");
    if (error)
        return;
    const char *val = jsonBuffer["dht22_temp_mult"];
    temperatureCoef = atof(val);
    val = jsonBuffer["ds18b20_temp_mult"];
    dsTemperatureCoef = atof(val);
    configFile.close();
    Serial.print("DHT22: ");
    Serial.println(temperatureCoef);
    Serial.print("DS18B20: ");
    Serial.println(dsTemperatureCoef);
}

void save_calibration()
{
    DynamicJsonDocument jsonBuffer(1024);
    char buf_a[40];
    char buf_b[40];
    snprintf(buf_a, sizeof(buf_a), "%g", temperatureCoef);
    snprintf(buf_b, sizeof(buf_b), "%g", dsTemperatureCoef);
    jsonBuffer["dht22_temp_mult"] = buf_a;
    jsonBuffer["ds18b20_temp_mult"] = buf_b;

    File configFile = SPIFFS.open("/calibration.json", "w");
    if (!configFile)
    {
        Serial.println("failed to open calibration file for writing");
        return;
    }

    serializeJson(jsonBuffer, Serial);
    Serial.println("");
    serializeJson(jsonBuffer, configFile);
    configFile.close();
}

void saveConfig()
{
    Serial.println("Saving configurations to file.");
    DynamicJsonDocument json(1024);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["workgroup"] = workgroup;
    json["username"] = username;
    json["password"] = password;
    json["temp_scale"] = temp_scale;
#ifdef HOME_ASSISTANT_DISCOVERY
    json["ha_name"] = ha_name;
#endif
#ifdef OTA_UPGRADES
    json["ota_server"] = ota_server;
#endif

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
        Serial.println("ERROR: failed to open config file for writing");
        return;
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
}

void checkDisplay()
{
    Serial.print("Mini I2C OLED Display at address ");
    Serial.print(i2cDisplayAddress, HEX);
    if (isSensorAvailable(i2cDisplayAddress))
    {
        Serial.println(": OK");
    }
    else
    {
        Serial.println(": N/A");
    }
}

void apWiFiCallback(WiFiManager *myWiFiManager)
{
    String configPortalSSID = myWiFiManager->getConfigPortalSSID();
    // Print information in the serial output
    Serial.print("Created access point for configuration: ");
    Serial.println(configPortalSSID);
    // Show information on the display
    String apId = configPortalSSID.substring(sizeof(PRODUCT));
    String configHelper("AP ID: "+apId);
    drawDisplay("Thermometer", "Please configure", configHelper.c_str(), true);
}

// If the ESP8266 restarts while the DHT22 is idle, the DHT22 ends up
// in a state where it no longer responds to requests for reading the
// temperature.  Exactly why this happens is not known, but perhaps
// the way the bootloader blinks with the blue LED (which is also
// connected to GPIO2) violates the DHT22 specification too much.
//
// We can work around this issue by starting a measurement immediately
// prior to the restart.  That way, the DHT22 sensor is busy sending a
// measurement while the EPS8266 restarts, and since it doesn't listen
// to its one-wire bus while it sends data to it, it apparently
// doesn't care about what is on it.  This explanation may not be
// true, but the fact is that this workaround somehow seems to work.
void nice_restart()
{
    // Ensure any previous measurement has time to finish (which takes
    // at most 5 ms), and ensure we allow the DHT22 to sit idle for at
    // least 2 seconds, as it needs to do between measurements.
    pinMode(DHTPIN, INPUT_PULLUP);
    delay(2005);

    // Send the "start measurement" pulse, which needs to be 1-10 ms.
    // We make it 2 ms.
    pinMode(DHTPIN, OUTPUT);
    digitalWrite(DHTPIN, LOW);
    delay(2);
    pinMode(DHTPIN, INPUT_PULLUP);

    // Wait a short while to give the DHT22 time to start responding.
    delayMicroseconds(70);

    ESP.restart();

    // The restart function seems to sometimes return; the actual
    // restart happens a short while later.  Enter an eternal loop
    // here to avoid doing any more work until the restart actually
    // happens.
    while (1)
        ;
}

void setup()
{
    uptime.d = 0;
    uptime.h = 0;
    uptime.m = 0;
    uptime.s = 0;

    // put your setup code here, to run once:
    strcpy(mqtt_line1, "");
    strcpy(mqtt_line2, "");
    strcpy(mqtt_line3, "");
    need_redraw = true;
    Serial.begin(115200);
    Serial.println();

    Wire.begin();
    checkDisplay();

    timeClient.begin();
    u8g2.begin();
    dht.begin();
    if (oneWire.reset())
    {
        sensors.begin();
    }
    else
    {
        pinMode(ONE_WIRE_BUS, INPUT);
        delay(1);
        if (false == digitalRead(ONE_WIRE_BUS))
        {
            haveButton = true;
            buttonPreviousMillis = millis();
        }
        else
        {
            sensors.begin();
        }
    }

    delay(10);

    //LED
    pinMode(pinAlarm, OUTPUT);
    //Button
    pinMode(pinButton, INPUT);

    waitForFactoryReset();

    // Machine ID
    calculateMachineId();

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (SPIFFS.begin())
    {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile)
            {
                Serial.println("opened config file");
                const size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonDocument json(1024);
                if (DeserializationError::Ok == deserializeJson(json, buf.get()))
                {
                    serializeJson(json, Serial);
                    Serial.println("\nparsed json");

                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(workgroup, json["workgroup"]);
                    strcpy(username, json["username"]);
                    strcpy(password, json["password"]);
                    {
                        const char *s = json["temp_scale"];
                        if (!s)
                            s = "celsius";
                        strcpy(temp_scale, s);
                    }
#ifdef HOME_ASSISTANT_DISCOVERY
                    {
                        const char *s = json["ha_name"];
                        if (!s)
                            s = machineId;
                        snprintf(ha_name, sizeof(ha_name), "%s", s);
                    }
#endif
#ifdef OTA_UPGRADES
                    {
                        const char *s = json["ota_server"];
                        if (!s)
                            s = ""; // The empty string never matches.
                        snprintf(ota_server, sizeof(ota_server), "%s", s);
                    }
#endif
                }
                else
                {
                    Serial.println("failed to load json config");
                }
            }
        }
        load_calibration();
    }
    else
    {
        Serial.println("failed to mount FS");
    }
    //end read

    // Set MQTT topics
    sprintf(line1_topic, "cmnd/%s/line1", machineId);
    sprintf(line2_topic, "cmnd/%s/line2", machineId);
    sprintf(line3_topic, "cmnd/%s/line3", machineId);
    sprintf(cmnd_temp_coefficient_topic, "cmnd/%s/tempcoef", machineId);
    sprintf(stat_temp_coefficient_topic, "stat/%s/tempcoef", machineId);
    sprintf(cmnd_ds_temp_coefficient_topic, "cmnd/%s/water/tempcoef", machineId);
    sprintf(stat_ds_temp_coefficient_topic, "stat/%s/water/tempcoef", machineId);
    sprintf(cmnd_temp_format, "cmnd/%s/tempformat", machineId);
#ifdef OTA_UPGRADES
    sprintf(cmnd_update_topic, "cmnd/%s/update", machineId);
#endif
#ifdef OTA_FACTORY_RESET
    sprintf(cmnd_factory_reset_topic, "cmnd/%s/factory-reset", machineId);
#endif
    sprintf(cmnd_restart_topic, "cmnd/%s/restart", machineId);
    sprintf(cmnd_slp_topic, "cmnd/%s/sea-level-pressure", machineId);
    sprintf(cmnd_altitude_topic, "cmnd/%s/altitude", machineId);

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
#ifndef MQTT_SERVER
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, sizeof(mqtt_server));
#endif
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, sizeof(mqtt_port));
    WiFiManagerParameter custom_workgroup("workgroup", "workgroup", workgroup, sizeof(workgroup));
#ifndef MQTT_USERNAME
    WiFiManagerParameter custom_mqtt_user("user", "MQTT username", username, sizeof(username));
#endif
#ifndef MQTT_PASSWORD
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", password, sizeof(password));
#endif
#ifdef HOME_ASSISTANT_DISCOVERY
    WiFiManagerParameter custom_mqtt_ha_name("ha_name", "Sensor name for Home Assistant", ha_name, sizeof(ha_name));
#endif
#ifdef OTA_UPGRADES
    WiFiManagerParameter custom_ota_server("ota_server", "OTA server", ota_server, sizeof(ota_server));
#endif
    WiFiManagerParameter custom_temperature_scale("temp_scale", "Temperature scale", temp_scale, sizeof(temp_scale));

    char htmlMachineId[200];
    sprintf(htmlMachineId,"<p style=\"color: red;\">Machine ID:</p><p><b>%s</b></p><p>Copy and save the machine ID because you will need it to control the device.</p>", machineId);
    WiFiManagerParameter custom_text_machine_id(htmlMachineId);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
#ifndef MQTT_SERVER
    wifiManager.addParameter(&custom_mqtt_server);
#endif
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_workgroup);
#ifndef MQTT_USERNAME
    wifiManager.addParameter(&custom_mqtt_user);
#endif
#ifndef MQTT_PASSWORD
    wifiManager.addParameter(&custom_mqtt_pass);
#endif
    wifiManager.addParameter(&custom_temperature_scale);
#ifdef HOME_ASSISTANT_DISCOVERY
    wifiManager.addParameter(&custom_mqtt_ha_name);
#endif
#ifdef OTA_UPGRADES
    wifiManager.addParameter(&custom_ota_server);
#endif
    wifiManager.addParameter(&custom_text_machine_id);

    //reset settings - for testing
    //wifiManager.resetSettings();

    //set minimu quality of signal so it ignores AP's under that quality
    //defaults to 8%
    //wifiManager.setMinimumSignalQuality();

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep
    //in seconds
    wifiManager.setTimeout(300);

    digitalWrite(pinAlarm, HIGH);
    drawDisplay("Connecting...", WiFi.SSID().c_str());

    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point
    //and goes into a blocking loop awaiting configuration
    wifiManager.setAPCallback(apWiFiCallback);
    String apId(machineId);
    if (apId.length() > 5)
        apId = apId.substring(apId.length() - 5);
    String accessPointName = PRODUCT " " + apId;
    if (!wifiManager.autoConnect(accessPointName.c_str(), ""))
    {
        digitalWrite(pinAlarm, LOW);
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        nice_restart();
    }
    WiFi.mode(WIFI_STA);

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    digitalWrite(pinAlarm, LOW);

    //read updated parameters
#ifndef MQTT_SERVER
    strcpy(mqtt_server, custom_mqtt_server.getValue());
#endif
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(workgroup, custom_workgroup.getValue());
#ifndef MQTT_USERNAME
    strcpy(username, custom_mqtt_user.getValue());
#endif
#ifndef MQTT_PASSWORD
    strcpy(password, custom_mqtt_pass.getValue());
#endif
    strcpy(temp_scale, custom_temperature_scale.getValue());
#ifdef HOME_ASSISTANT_DISCOVERY
    strcpy(ha_name, custom_mqtt_ha_name.getValue());
#endif
#ifdef OTA_UPGRADES
    strcpy(ota_server, custom_ota_server.getValue());
#endif

    //save the custom parameters to FS
    if (shouldSaveConfig)
    {
        saveConfig();
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    drawDisplay("Connected!", "Local IP:", WiFi.localIP().toString().c_str());
    delay(2000);

    // Sensors
    htu.begin();
    bmp.begin();

    // MQTT
#ifdef MQTT_SERVER
    Serial.print("Hardcoded MQTT Server: ");
    Serial.println(MQTT_SERVER);
#else
    Serial.print("MQTT Server: ");
    Serial.println(mqtt_server);
#endif

    Serial.print("MQTT Port: ");
    Serial.println(mqtt_port);
    // Print MQTT Username
    if (mqtt_username() != 0)
    {
        Serial.print("MQTT Username: ");
        Serial.println(mqtt_username());
    }
    else
    {
        Serial.println("No MQTT username");
    }

    if (mqtt_password() != 0)
    {
        // Hide password from the log and show * instead
        char hiddenpass[20] = "";
        for (size_t charP=0; charP < strlen(mqtt_password()); charP++)
        {
            hiddenpass[charP] = '*';
        }
        hiddenpass[strlen(password)] = '\0';
        Serial.print("MQTT Password: ");
        Serial.println(hiddenpass);
    }
    else
    {
        Serial.println("No MQTT password");
    }

    Serial.print("Saved temperature scale: ");
    Serial.println(temp_scale);
    configTempCelsius = ( (0 == strlen(temp_scale)) || String(temp_scale).equalsIgnoreCase("celsius"));
    Serial.print("Temperature scale: ");
    if (true == configTempCelsius)
    {
      Serial.println("Celsius");
    }
    else
    {
      Serial.println("Fahrenheit");
    }
#ifdef HOME_ASSISTANT_DISCOVERY
    Serial.print("Home Assistant sensor name: ");
    Serial.println(ha_name);
#endif
#ifdef OTA_UPGRADES
    if (ota_server[0] != '\0')
    {
        Serial.print("OTA server: ");
        Serial.println(ota_server);
    }
    else
    {
#  ifndef OTA_SERVER
        Serial.println("No OTA server");
#  endif
    }

#  ifdef OTA_SERVER
    Serial.print("Hardcoded OTA server: ");
    Serial.println(OTA_SERVER);
#  endif

#endif

    const int mqttPort = atoi(mqtt_port);
#ifdef MQTT_SERVER
    mqttClient.setServer(MQTT_SERVER, mqttPort);
#else
    mqttClient.setServer(mqtt_server, mqttPort);
#endif

    mqttClient.setCallback(mqttCallback);

    mqttReconnect();

    Serial.println("");
    Serial.println("-----");
    Serial.print("Machine ID: ");
    Serial.println(machineId);
    Serial.println("-----");
    Serial.println("");

    setupADPS9960();
}

void setupADPS9960()
{
    if(apds.begin())
    {
        //gesture mode will be entered once proximity mode senses something close
        apds.enableProximity(true);
        apds.enableGesture(true);
    }
}

void waitForFactoryReset()
{
    Serial.println("Press button within 2 seconds for factory reset...");
    for (int iter = 0; iter < 20; iter++)
    {
        digitalWrite(pinAlarm, HIGH);
        delay(50);
        if (false == digitalRead(pinButton))
        {
            factoryReset();
            return;
        }
        digitalWrite(pinAlarm, LOW);
        delay(50);
        if (false == digitalRead(pinButton))
        {
            factoryReset();
            return;
        }
    }
}

void factoryReset()
{
    if (false == digitalRead(pinButton))
    {
        Serial.println("Hold the button to reset to factory defaults...");
        bool cancel = false;
        for (int iter=0; iter<30; iter++)
        {
            digitalWrite(pinAlarm, HIGH);
            delay(100);
            if (true == digitalRead(pinButton))
            {
                cancel = true;
                break;
            }
            digitalWrite(pinAlarm, LOW);
            delay(100);
            if (true == digitalRead(pinButton))
            {
                cancel = true;
                break;
            }
        }
        if (false == digitalRead(pinButton) && !cancel)
        {
            digitalWrite(pinAlarm, HIGH);
            Serial.println("Disconnecting...");
            WiFi.disconnect();

            // NOTE: the boot mode:(1,7) problem is known and only happens at the first restart after serial flashing.

            Serial.println("Restarting...");
            // Clean the file system with configurations
            SPIFFS.format();
            // Restart the board
            nice_restart();
        }
        else
        {
            // Cancel reset to factory defaults
            Serial.println("Reset to factory defaults cancelled.");
            digitalWrite(pinAlarm, LOW);
        }
    }
}

#ifdef OTA_UPGRADES
void do_ota_upgrade(char *text)
{
    DynamicJsonDocument json(1024);
    auto error = deserializeJson(json, text);
    if (error)
    {
        Serial.println("No success decoding JSON.\n");
    }
    else if (!json.containsKey("server"))
    {
        Serial.println("JSON is missing server\n");
    }
    else if (!json.containsKey("file"))
    {
        Serial.println("JSON is missing file\n");
    }
    else
    {
        int port = 0;
        if (json.containsKey("port"))
        {
            port = json["port"];
            Serial.print("Port configured to ");
            Serial.println(port);
        }

        if (0 >= port || 65535 < port)
        {
            port = 80;
        }

        String server = json["server"];
        String file = json["file"];

        bool ok = false;
        if (ota_server[0] != '\0' && !strcmp(server.c_str(), ota_server))
            ok = true;

#  ifdef OTA_SERVER
        if (!strcmp(server.c_str(), OTA_SERVER))
            ok = true;
#  endif

        if (!ok)
        {
            Serial.println("Wrong OTA server. Refusing to upgrade.");
            return;
        }

        Serial.print("Attempting to upgrade from ");
        Serial.print(server);
        Serial.print(":");
        Serial.print(port);
        Serial.println(file);
        ESPhttpUpdate.setLedPin(pinAlarm, HIGH);
        WiFiClient update_client;
        t_httpUpdate_return ret = ESPhttpUpdate.update(update_client,
                                                       server, port, file);
        switch (ret)
        {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;

        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            break;
        }
    }
}
#endif

#ifdef OTA_FACTORY_RESET
void do_ota_factory_reset()
{
    Serial.println("Factory reset issued...");
    WiFi.disconnect();

    Serial.println("Restarting...");

    // Clean the file system with configurations
    SPIFFS.format();

    // Restart the board
    nice_restart();
}
#endif

void processMessageScale(const char* text)
{
    StaticJsonDocument<200> data;
    deserializeJson(data, text);
    // Set temperature to Celsius or Fahrenheit and redraw screen
    Serial.print("Changing the temperature scale to: ");
    if (data.containsKey("scale") && (0 == strcmp(data["scale"], "celsius")) )
    {
        Serial.println("Celsius");
        configTempCelsius = true;
        strcpy(temp_scale, "celsius");
    }
    else
    {
        Serial.println("Fahrenheit");
        configTempCelsius = false;
        strcpy(temp_scale, "fahrenheit");
    }
    // Force default sensor lines with the new format for temperature
    setDefaultSensorLines();
    need_redraw = true;
    // Save configurations to file
    saveConfig();
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    // Convert received bytes to a string
    char text[length + 1];
    snprintf(text, length + 1, "%s", payload);

    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(text);

    if (strcmp(topic, line1_topic) == 0)
    {
        snprintf(mqtt_line1, sizeof(mqtt_line1), "%s", text);
        need_redraw = true;
    }

    if (strcmp(topic, line2_topic) == 0)
    {
        snprintf(mqtt_line2, sizeof(mqtt_line2), "%s", text);
        need_redraw = true;
    }

    if (strcmp(topic, line3_topic) == 0)
    {
        snprintf(mqtt_line3, sizeof(mqtt_line3), "%s", text);
        need_redraw = true;
    }

    if (strcmp(topic, cmnd_temp_coefficient_topic) == 0)
    {
        temperatureCoef = atof(text);
        save_calibration();
    }

    if (strcmp(topic, cmnd_ds_temp_coefficient_topic) == 0)
    {
        dsTemperatureCoef = atof(text);
        save_calibration();
    }

    if (strcmp(topic, cmnd_temp_format) == 0)
    {
        processMessageScale(text);
    }

#ifdef OTA_UPGRADES
    if (strcmp(topic, cmnd_update_topic) == 0)
    {
        Serial.println("OTA request seen.\n");
        do_ota_upgrade(text);
        // Any OTA upgrade will stop the mqtt client, so if the
        // upgrade failed and we get here publishState() will fail.
        // Just return here, and we will reconnect from within the
        // loop().
        return;
    }
#endif

#ifdef OTA_FACTORY_RESET
    if (strcmp(topic, cmnd_factory_reset_topic) == 0)
    {
        Serial.println("OTA factory reset request seen.\n");
        do_ota_factory_reset();
    }
#endif

    if (strcmp(topic, cmnd_restart_topic) == 0)
    {
        Serial.println("OTA restart request seen.\n");
        nice_restart();
    }

    if (strcmp(topic, cmnd_slp_topic) == 0)
    {
        configured_sea_level_pressure = atof(text);
    }

    if (strcmp(topic, cmnd_altitude_topic) == 0)
    {
        configured_altitude = atof(text);
    }

    publishState();
}

void calculateMachineId()
{
    MD5Builder md5;
    md5.begin();
    char chipId[25];
    sprintf(chipId,"%d",ESP.getChipId());
    md5.add(chipId);
    md5.calculate();
    md5.toString().toCharArray(machineId, sizeof(machineId));
}

const char *mqtt_username()
{
#ifdef MQTT_USERNAME
    return MQTT_USERNAME;
#endif

    if (strlen(username) == 0)
        return 0;

    return username;
}

const char *mqtt_password()
{
#ifdef MQTT_PASSWORD
    return MQTT_PASSWORD;
#endif

    if (strlen(password) == 0)
        return 0;

    return password;
}

void mqttReconnect()
{
    char clientId[18 + sizeof(machineId)];
    snprintf(clientId, sizeof(clientId), "anavi-thermometer-%s", machineId);

    // Loop until we're reconnected
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (true == mqttClient.connect(clientId,
                                       mqtt_username(), mqtt_password()))
        {
            Serial.println("connected");

            // Subscribe to MQTT topics
            mqttClient.subscribe(line1_topic);
            mqttClient.subscribe(line2_topic);
            mqttClient.subscribe(line3_topic);
            mqttClient.subscribe(cmnd_temp_coefficient_topic);
            mqttClient.subscribe(cmnd_ds_temp_coefficient_topic);
            mqttClient.subscribe(cmnd_temp_format);
#ifdef OTA_UPGRADES
            mqttClient.subscribe(cmnd_update_topic);
#endif
#ifdef OTA_FACTORY_RESET
            mqttClient.subscribe(cmnd_factory_reset_topic);
#endif
            mqttClient.subscribe(cmnd_restart_topic);
            mqttClient.subscribe(cmnd_slp_topic);
            mqttClient.subscribe(cmnd_altitude_topic);
            publishState();
            break;

        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

#ifdef HOME_ASSISTANT_DISCOVERY
// Publish an MQTT Discovery message for Home Assistant.
//
// Arguments:
//
// - component: the Home Assistant component type of the device, such
//   as "sensor" or "binary_sensor".
//
// - config_key: a string that, when combined with the machineId,
//   creates a unique name for this sensor.  Used both as the
//   <object_id> in the discovery topic, and as part of the unique_id.
//
// - device_class: The device class (see
//   <https://www.home-assistant.io/docs/configuration/customizing-devices/#device-class>).
//   May be 0.
//
// - name_suffix: This will be appended to ha_name to create the
//   human-readable name of this sensor.  Should typically be
//   capitalized.
//
// - state_topic: The topic where this sensor publishes its state.
//   The workgroup and machineId will be prepended to form the actual
//   topic.  This should always start with a slash.
//
// - unit: The unit_of_measurement, or 0.
//
// - value_template: A template to extract a value from the payload.
bool publishSensorDiscovery(const char *component,
                            const char *config_key,
                            const char *device_class,
                            const char *name_suffix,
                            const char *state_topic,
                            const char *unit,
                            const char *value_template)
{
    static char topic[48 + sizeof(machineId)];

    snprintf(topic, sizeof(topic),
             "homeassistant/%s/%s/%s/config", component, machineId, config_key);

    DynamicJsonDocument json(1024);
    if (device_class)
        json["device_class"] = device_class;
    json["name"] = String(ha_name) + " " + name_suffix;
    json["unique_id"] = String("anavi-") + machineId + "-" + config_key;
    json["state_topic"] = String(workgroup) + "/" + machineId + state_topic;
    if (unit)
        json["unit_of_measurement"] = unit;
    json["value_template"] = value_template;

    json["device"]["identifiers"] = machineId;
    json["device"]["manufacturer"] = "ANAVI Technology";
    json["device"]["model"] = PRODUCT;
    json["device"]["name"] = String(ha_name) + " " + name_suffix;
    json["device"]["sw_version"] = ESP.getSketchMD5();

    JsonArray connections = json["device"].createNestedArray("connections").createNestedArray();
    connections.add("mac");
    connections.add(WiFi.macAddress());

    Serial.print("Home Assistant discovery topic: ");
    Serial.println(topic);

    int payload_len = measureJson(json);
    if (!mqttClient.beginPublish(topic, payload_len, true))
    {
        Serial.println("beginPublish failed!\n");
        return false;
    }

    if (serializeJson(json, mqttClient) != payload_len)
    {
        Serial.println("writing payload: wrong size!\n");
        return false;
    }

    if (!mqttClient.endPublish())
    {
        Serial.println("endPublish failed!\n");
        return false;
    }

    return true;
}
#endif

void publishState()
{
    static char payload[80];
    snprintf(payload, sizeof(payload), "%f", temperatureCoef);
    mqttClient.publish(stat_temp_coefficient_topic, payload, true);
    snprintf(payload, sizeof(payload), "%f", dsTemperatureCoef);
    mqttClient.publish(stat_ds_temp_coefficient_topic, payload, true);

#ifdef HOME_ASSISTANT_DISCOVERY
    String homeAssistantTempScale = (true == configTempCelsius) ? "°C" : "°F";
    publishSensorDiscovery("sensor",
                           "temp",
                           "temperature",
                           "Temperature",
                           "/air/temperature",
                           homeAssistantTempScale.c_str(),
                           "{{ value_json.temperature | round(1) }}");

    publishSensorDiscovery("sensor",
                           "humidity",
                           "humidity",
                           "Humidity",
                           "/air/humidity",
                           "%",
                           "{{ value_json.humidity }}");

    if (haveButton)
    {
        publishSensorDiscovery("binary_sensor",
                               "button",
                               0,
                               "Button 1",
                               "/button/1",
                               0,
                               "{{ value_json.pressed }}");
    }
    else if (0 < sensors.getDeviceCount())
    {
        publishSensorDiscovery("sensor",
                               "watertemp",
                               "temperature",
                               "Water Temp",
                               "/water/temperature",
                               "°C",
                               "{{ value_json.temperature }}");
    }

    if (isSensorAvailable(sensorBMP180))
    {
        publishSensorDiscovery("sensor",
                               "bmp180-pressure",
                               "pressure",
                               "BMP180 Air Pressure",
                               "/BMPpressure",
                               "hPa",
                               "{{ value_json.BMPpressure }}");

        publishSensorDiscovery("sensor",
                               "bmp180-temp",
                               "temperature",
                               "BMP180 Temperature",
                               "/BMPtemperature",
                               homeAssistantTempScale.c_str(),
                               "{{ value_json.BMPtemperature }}");

        if (configured_sea_level_pressure > 0)
        {
            publishSensorDiscovery("sensor",
                                   "bmp180-altitude",
                                   0, // No support for "altitude" in
                                      // Home Assistant, so we claim
                                      // to be a generic sensor.
                                   "BMP180 Altitude",
                                   "/BMPaltitude",
                                   "m",
                                   "{{ value_json.altitude }}");
        }

        if (configured_altitude >= -20000)
        {
            publishSensorDiscovery("sensor",
                                   "bmp180-slp",
                                   "pressure",
                                   "BMP180 Sea-Level Pressure",
                                   "/BMPsea-level-pressure",
                                   "hPa",
                                   "{{ value_json.pressure }}");
        }
    }

    if (isSensorAvailable(sensorBH1750))
    {
        publishSensorDiscovery("sensor",
                              "light",
                              "illuminance",
                              "Light",
                              "/light",
                              "Lux",
                              "{{ value_json.light }}");
    }
#endif
}

void publishSensorData(const char* subTopic, const char* key, const float value)
{
    StaticJsonDocument<100> json;
    json[key] = value;
    char payload[100];
    serializeJson(json, payload);
    char topic[200];
    sprintf(topic,"%s/%s/%s", workgroup, machineId, subTopic);
    mqttClient.publish(topic, payload, true);
}

void publishSensorData(const char* subTopic, const char* key, const String& value)
{
    StaticJsonDocument<100> json;
    json[key] = value;
    char payload[100];
    serializeJson(json, payload);
    char topic[200];
    sprintf(topic,"%s/%s/%s", workgroup, machineId, subTopic);
    mqttClient.publish(topic, payload, true);
}

bool isSensorAvailable(int sensorAddress)
{
    // Check if I2C sensor is present
    Wire.beginTransmission(sensorAddress);
    return 0 == Wire.endTransmission();
}

void handleHTU21D()
{
    // Check if temperature has changed
    const float tempTemperature = htu.readTemperature();
    if (1 <= abs(tempTemperature - sensorTemperature))
    {
        // Print new temprature value
        sensorTemperature = tempTemperature;
        Serial.print("Temperature: ");
        Serial.println(formatTemperature(sensorTemperature));

        // Publish new temperature value through MQTT
        publishSensorData("temperature", "temperature", convertTemperature(sensorTemperature));
    }

    // Check if humidity has changed
    const float tempHumidity = htu.readHumidity();
    if (1 <= abs(tempHumidity - sensorHumidity))
    {
        // Print new humidity value
        sensorHumidity = tempHumidity;
        Serial.print("Humidity: ");
        Serial.print(sensorHumidity);
        Serial.println("%");

        // Publish new humidity value through MQTT
        publishSensorData("humidity", "humidity", sensorHumidity);
    }
}

void sensorWriteData(int i2cAddress, uint8_t data)
{
    Wire.beginTransmission(i2cAddress);
    Wire.write(data);
    Wire.endTransmission();
}

void handleBH1750()
{
    //Wire.begin();
    // Power on sensor
    sensorWriteData(sensorBH1750, 0x01);
    // Set mode continuously high resolution mode
    sensorWriteData(sensorBH1750, 0x10);

    uint16_t tempAmbientLight;

    Wire.requestFrom(sensorBH1750, 2);
    tempAmbientLight = Wire.read();
    tempAmbientLight <<= 8;
    tempAmbientLight |= Wire.read();
    // s. page 7 of datasheet for calculation
    tempAmbientLight = tempAmbientLight/1.2;

    if (1 <= abs(tempAmbientLight - sensorAmbientLight))
    {
        // Print new brightness value
        sensorAmbientLight = tempAmbientLight;
        Serial.print("Light: ");
        Serial.print(tempAmbientLight);
        Serial.println("Lux");

        // Publish new brightness value through MQTT
        publishSensorData("light", "light", sensorAmbientLight);
    }
}

void detectGesture()
{
    //read a gesture from the device
    const uint8_t gestureCode = apds.readGesture();
    // Skip if gesture has not been detected
    if (0 == gestureCode)
    {
        return;
    }
    String gesture = "";
    switch(gestureCode)
    {
    case APDS9960_DOWN:
        gesture = "down";
        break;
    case APDS9960_UP:
        gesture = "up";
        break;
    case APDS9960_LEFT:
        gesture = "left";
        break;
    case APDS9960_RIGHT:
        gesture = "right";
        break;
    }
    Serial.print("Gesture: ");
    Serial.println(gesture);
    // Publish the detected gesture through MQTT
    publishSensorData("gesture", "gesture", gesture);
}

void handleBMP()
{
  sensors_event_t event;
  bmp.getEvent(&event);
  if (!event.pressure)
  {
    // BMP180 sensor error
    return;
  }
  Serial.print("BMP180 Pressure: ");
  Serial.print(event.pressure);
  Serial.println(" hPa");
  float temperature;
  bmp.getTemperature(&temperature);
  Serial.print("BMP180 Temperature: ");
  Serial.println(formatTemperature(temperature));

  // Publish new pressure values through MQTT
  publishSensorData("BMPpressure", "BMPpressure", event.pressure);
  publishSensorData("BMPtemperature", "BMPtemperature", convertTemperature(temperature));

  if (configured_sea_level_pressure > 0)
  {
      float altitude;
      altitude = bmp.pressureToAltitude(configured_sea_level_pressure, event.pressure, temperature);
      Serial.print("BMP180 Altitude: ");
      Serial.print(altitude);
      Serial.println(" m");
      publishSensorData("BMPaltitude", "altitude", altitude);
  }

  if (configured_altitude >= MIN_ALTITUDE)
  {
      float slp;
      slp = bmp.seaLevelForAltitude(configured_altitude, event.pressure, temperature);
      Serial.print("BMP180 sea-level pressure: ");
      Serial.print(slp);
      Serial.println(" hPa");
      publishSensorData("BMPsea-level-pressure", "pressure", slp);
  }
}

void handleSensors()
{
    if (isSensorAvailable(sensorHTU21D))
    {
        handleHTU21D();
    }
    if (isSensorAvailable(sensorBH1750))
    {
        handleBH1750();
    }
    if (isSensorAvailable(sensorBMP180))
    {
      handleBMP();
    }
}

float convertCelsiusToFahrenheit(float temperature)
{
    return (temperature * 9/5 + 32);
}

float convertTemperature(float temperature)
{
    return (true == configTempCelsius) ? temperature : convertCelsiusToFahrenheit(temperature);
}

String formatTemperature(float temperature)
{
    String unit = (true == configTempCelsius) ? "°C" : "°F";
    return String(convertTemperature(temperature), 1) + unit;
}

void setDefaultSensorLines()
{
    sensor_line1 = "Air " + formatTemperature(dhtTemperature);
    Serial.println(sensor_line1);
    sensor_line2 = "Humidity " + String(dhtHumidity, 0) + "%";
    Serial.println(sensor_line2);
    if (haveButton)
        displayButton();
    else
        sensor_line3 = "";
}

void displayButton()
{
    if (buttonState)
        sensor_line3 = "Button: ON";
    else
        sensor_line3 = "Button: OFF";
}

// Update the uptime information.
//
// As long as you call this once every 20 days or so, or more often,
// it should handle the wrap-around in millis() just fine.
void uptime_loop()
{
    unsigned long now = millis();
    unsigned long delta = now - uptime.last_millis;
    uptime.last_millis = now;

    uptime.ms += delta;

    // Avoid expensive floating point arithmetic if it isn't needed.
    if (uptime.ms < 1000)
        return;

    uptime.s += uptime.ms / 1000;
    uptime.ms %= 1000;

    // Avoid expensive floating point arithmetic if it isn't needed.
    if (uptime.s < 60)
        return;

    uptime.m += uptime.s / 60;
    uptime.s %= 60;

    // We could do an early return here (and before the update of d)
    // as well, but what if the entire loop runs too slowly when we
    // need to update update.d?  Beter to run all the code at least
    // once a minute, so that performance problems have a chance of
    // beeing seen regularly, and not just once per day.

    uptime.h += uptime.m / 60;
    uptime.m %= 60;
    uptime.d += uptime.h / 24;
    uptime.h %= 24;
}

void publish_uptime()
{
    StaticJsonDocument<100> json;
    json["d"] = uptime.d;
    json["h"] = uptime.h;
    json["m"] = uptime.m;
    json["s"] = uptime.s;
    char payload[100];
    serializeJson(json, payload);
    char topic[200];
    sprintf(topic,"%s/%s/uptime", workgroup, machineId);
    mqttClient.publish(topic, payload);
}

void loop()
{
    uptime_loop();

    // put your main code here, to run repeatedly:
    mqttClient.loop();

    // Reconnect if there is an issue with the MQTT connection
    const unsigned long mqttConnectionMillis = millis();
    if ( (false == mqttClient.connected()) && (mqttConnectionInterval <= (mqttConnectionMillis - mqttConnectionPreviousMillis)) )
    {
        mqttConnectionPreviousMillis = mqttConnectionMillis;
        mqttReconnect();
    }

    // Handle gestures at a shorter interval
    if (isSensorAvailable(APDS9960_ADDRESS))
    {
        detectGesture();
    }

    const unsigned long currentMillis = millis();

    // Handle button presses at a shorter interval
    if (haveButton && BUTTON_INTERVAL <= (currentMillis - buttonPreviousMillis))
    {
        bool currentState = digitalRead(ONE_WIRE_BUS);

        if (buttonState != currentState)
        {
            buttonState = currentState;
            publishSensorData("button/1", "pressed",
                              currentState ? "ON" : "OFF");
            buttonPreviousMillis = currentMillis;
            displayButton();
            need_redraw = true;
        }
    }

    if (sensorInterval <= (currentMillis - sensorPreviousMillis))
    {
        publish_uptime();

        sensorPreviousMillis = currentMillis;
        handleSensors();

        // Read temperature and humidity from DHT22/AM2302
        float temp = dht.readTemperature();
        float humidity = dht.readHumidity();

#ifdef ESP12_BLUE_LED_ALWAYS_ON
        pinMode(DHTPIN, OUTPUT);
        digitalWrite(DHTPIN, LOW);
#endif

        if (!isnan(humidity) && !isnan(temp))
        {
            // Adjust temperature depending on the calibration coefficient
            temp = temp*temperatureCoef;

            dhtTemperature = temp;
            dhtHumidity = humidity;
            publishSensorData("air/temperature", "temperature", convertTemperature(temp));
            publishSensorData("air/humidity", "humidity", humidity);

            // Calculate heat index
            float dhtHeatIndex = dht.computeHeatIndex(dhtTemperature, dhtHumidity, false);
            publishSensorData("air/heatindex", "heatindex", convertTemperature(dhtHeatIndex));
            Serial.println("DHT Heat Index: " + formatTemperature(dhtHeatIndex));
        }
        setDefaultSensorLines();

        long rssiValue = WiFi.RSSI();
        String rssi = "WiFi " + String(rssiValue) + " dBm";
        Serial.println(rssi);

        if (haveButton)
        {
            displayButton();
        }
        else if (0 < sensors.getDeviceCount())
        {
            sensors.requestTemperatures();
            float wtemp = sensors.getTempCByIndex(0);
            wtemp = wtemp * dsTemperatureCoef;
            dsTemperature = wtemp;
            publishSensorData("water/temperature", "temperature", convertTemperature(wtemp));
            sensor_line3 = "Water " + formatTemperature(dsTemperature);
            Serial.println(sensor_line3);
        }
        else
        {
            static int select = 0;
            switch (++select%2) {
              case 0:
                timeClient.update();
                sensor_line3 = "UTC: " + timeClient.getFormattedTime();
                break;
              default:
                sensor_line3 = rssi;
                break;
            }
        }

        need_redraw = true;

        publishSensorData("wifi/ssid", "ssid", WiFi.SSID());
        publishSensorData("wifi/bssid", "bssid", WiFi.BSSIDstr());
        publishSensorData("wifi/rssi", "rssi", rssiValue);
        publishSensorData("wifi/ip", "ip", WiFi.localIP().toString());
        publishSensorData("sketch", "sketch", ESP.getSketchMD5());

#ifdef PUBLISH_CHIP_ID
        char chipid[9];
        snprintf(chipid, sizeof(chipid), "%08x", ESP.getChipId());
        publishSensorData("chipid", "chipid", chipid);
#endif

#ifdef PUBLISH_FREE_HEAP
        publishSensorData("free-heap", "bytes", ESP.getFreeHeap());
#endif
    }

    if (need_redraw)
    {
        drawDisplay(mqtt_line1[0] ? mqtt_line1 : sensor_line1.c_str(),
                    mqtt_line2[0] ? mqtt_line2 : sensor_line2.c_str(),
                    mqtt_line3[0] ? mqtt_line3 : sensor_line3.c_str());
        need_redraw = false;
    }

    // Press and hold the button to reset to factory defaults
    factoryReset();
}
