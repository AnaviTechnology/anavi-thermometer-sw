// -*- mode: c++; indent-tabs-mode: nil; c-file-style: "stroustrup" -*-

// This is anavi-thermometer-sw.ino, the default firmware of ANAVI
// Thermometer. The canonical home for this software is:
// https://github.com/AnaviTechnology/anavi-thermometer-sw for more

// === MQTT Summary ===
//
// For convenience, this comment summarizes how this sketch uses MQTT.
// Some of the topics are explained in more detail elsewhere.
//
// == Commands ==
//
// The ANAVI Thermometer listens to the following MQTT commands. The
// command should be prefixed by "cmnd/$MACHINEID/".
//
// | Command            | Args      |
// | sea-level-pressure | float     |
// | altitude           | meters    |
// | line1              | string    |
// | line2              | string    |
// | line3              | string    |
// | tempcoef           | float     |
// | water/tempoef      | float     |
// | tempformat         | { "scale": "celsius" or "fahrenheit" } |
// | restart            | (ignored) |
//
// Some commands are only available if a certain symbol was defined
// when the sketch was compiled:
//
// | Symbol             | Command      | Args      |
// | OTA_UPGRADES       | update       | {"file": f, "server": h, "port": p } |
// | OTA_FACTORY_RESET  |factory-reset | (ignored) |
//
// == Status messages ==
//
// The ANAVI Thermometer publishes these status messages. The topic
// is prefixed with "$WORKGROUP/$MACHINEID/".
//
// | Topic             | Value                        | Requirement
// | status/<sensor>   | "online" or "offline"        | USE_MULTIPLE_STATUS_TOPICS, USE_MULTIPLE_MQTT
// | status/esp8266    | "online" or "offline"        | USE_MULTIPLE_STATUS_TOPICS
// | button/1          | { "pressed": "ON" or "OFF" } | BUTTONSUPPORT
// | free-heap         | { "bytes": b }               | PUBLISH_FREE_HEAP
// | BMPaltitude       | { "altitude": 72.3 }         | Requires sea-level-preassure to be set.
// | BMPsea-level-pressure | { pressure: 1028.4 }     | Requires the altitude to be set
// | temperature       | { temperature: float }
// | humidity          | { humidity: float }
// | light             | { light: float }
// | gesture           | { gesture: "down"/"up"/"left"/"right" }
// | BMPpressure       | { BMPpressure: float }
// | BMPtemperature    | { BMPtemperature: float }
// | air/dht22-temp    | { temperature: float }
// | air/temperature   | { temperature: float }
// | air/humidity      | { humidity: float }
// | air/heatindex     | { heatindex: float }
// | water/temperature | { temperature: float }
// | wifi/ssid         | { ssid: str }
// | wifi/bssid        | { bssid: str }
// | wifi/rssi         | { rssi: float }
// | wifi/ip           | { ip: str }
// | sketch            | { sketch: md5 }
// | chipid            | { chipid: hex }
// | free-heap         | { bytes: int }
//
// Additionally, some status messages are published with the
// "stat/$MACHINEID" prefix:
//
// | tempcoef       | float |
// | water/tempcoef | float |
//
// Additionally, if HOME_ASSISTANT_DISCOVERY is defined, ANAVI
// Thermometer will publish the following discovery messages if the
// corresponding sensor is detected:
//
// homeassistant/$COMPONENT/$MACHINEID/$CONFIG_KEY/config


#include <FS.h> //this needs to be first, or it all crashes and burns...

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
// publish MQTT messages that enable Home Assistant to auto-discover
// the device.  See
// https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
//
// This requires PubSubClient 2.7.
#define HOME_ASSISTANT_DISCOVERY 1

// Enable the logic for detection a button instead of DS18B20 sensor
// By default it is disable due to a bug with detecting OneWire devices
// after OneWire reset command.
// #define BUTTONSUPPORT 1

// By default, the code supports a single DS18B20 sensor.  It will
// report its reading on the $WORKGROUP/$MACHINEID/water/temperature
// MQTT topic.
//
// But the 1-Wire hardware supports connecting multiple DS18B20
// sensors.  Each sensor has a unique factory-assigned ID.  By
// defining MULTI_DS18B20_SUPPORT, each sensor will report its
// temperature on $WORKGROUP/$MACHINEID/ds18b20/$SENSORID/temperature,
// where $SENSORID is the factory-assigned ID (16 hexadecimal digits).
//
// If MULTI_DS18B20_SUPPORT is enabled, the "water/temperature" topic
// won't be used.  Calibration (via cmnd/$MACHINEID/water/tempcoef) is
// also not available in MULTI_DS18B20_SUPPORT mode.
//
// This code has been tested with 14 DS18B20 sensors connected to the
// same system.  It worked fine, but if USE_MULTIPLE_MQTT is defined
// only about 6 kB of free RAM were left.  If you don't define
// USE_MULTIPLE_MQTT you will be able to connect even more sensors.
// (At some point, you might have to add an external power supply.)
#undef MULTI_DS18B20_SUPPORT

// If PUBLISH_CHIP_ID is defined, the Anavi Thermometer will publish
// the chip ID using MQTT.  This can be considered a privacy issue,
// and is disabled by default.
#undef PUBLISH_CHIP_ID

// Should Over-the-Air upgrades be supported?  They are only supported
// if this define is set, and the user configures an OTA server on the
// wifi configuration page (or defines OTA_SERVER below).  If you use
// OTA you are strongly advised to use signed builds (see
// https://arduino-esp8266.readthedocs.io/en/3.0.2/ota_updates/readme.html#advanced-security-signed-updates)
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

// When HOME_ASSISTANT_DISCOVERY is defined, the MQTT Discovery
// messages sent by the ANAVI Thermometer will include an
// "availability_topic" setting.  This refers to a topic where the
// ANAVI Thermometer will publish an "online" message to indicate that
// the sensor is active, and an "offline" message if the ANAVI
// Thermometer detects that the sensor becomes unavailable.  It will
// also set up a "Last Will and Testament" (also known as "will" or
// "LWT") for the MQTT connection, so that the MQTT broker
// automatically sets the status to "offline" if it loses the
// connection with the ANAVI Thermometer.
//
// The MQTT protocol only supports a single will per MQTT connection,
// but the ANAVI Thermometer can have multiple sensors.  We would
// ideally like to have one will per sensor.  That would allow Home
// Assistant to automatically detect that all sensor readings are
// unavailable if you turn off the ANAVI Thermometer.
//
// These status topics are named
// "<workgroup>/<machineid>/status/<sensor>", where <sensor> can be
// "dht22" for the built-in DHT22 temperature and humidity sensor,
// "esp8266" for the ANAVI Thermometer itself, etc.  See the
// mqtt_specs table for a full list.  Status topics will only be
// published for sensors that are detected.
//
// You can select one of three available methods to handle this.  Each
// method has its drawbacks.
//
// The default method uses a single MQTT connection but one
// availability topic per sensor (plus one for the esp8266 board
// itself).  The drawback of this method is that only the esp8266
// status topic will be set to offline if the connection to the ANAVI
// Thermometer is lost.  You can work around this by having something
// else that subscribes to the esp8266 status topic and publishes
// "offline" messages to all other status topics whenever it receives
// an "offline" message.  Is is possible to set up this in Home
// Assistant as an automation similar to this (assuming the
// <machineid> is "deadbeef"):
//
//     - id: '4711'
//       alias: Track ANAVI Thermometer disconnect in dht22status
//       description: ''
//       trigger:
//       - topic: workgroup/deadbeef/status/esp8266
//         payload: offline
//         platform: mqtt
//       condition: []
//       action:
//       - service: mqtt.publish
//         data:
//           topic: workgroup/deadbeef/status/dht22
//           payload: offline
//           qos: 0
//           retain: true
//       - service: mqtt.publish
//         data:
//           topic: workgroup/deadbeef/status/ds18b20
//           payload: offline
//           qos: 0
//           retain: true
//
// The above example assumes that the "deadbeef" ANAVI Thermometer has
// a connected DS18B20 waterproof temperature sensor and a DHT22
// sensor.  You can add more actions as needed.
//
// By commenting out the "#define USE_MULTIPLE_STATUS_TOPICS" line
// below, you can instead select a mode where the ANAVI Thermometer
// only uses a single status topic, named
// "<workgroup>/<machineid>/status/esp8266".  The main drawback of
// this is that the ANAVI Thermometer can no longer inform you of
// individual sensors that are offline, but it may be a decent
// compromise if you don't want to (or can't) set up an automation as
// above or use USE_MULTIPLE_MQTT.
//
// The third method is documented below, above USE_MULTIPLE_MQTT.
#define USE_MULTIPLE_STATUS_TOPICS

// The ANAVI Thermometer can be configured to open several MQTT
// connections to the MQTT broker, so that it can reliably set up LWTs
// for all the status topics.  (This is only relevant if
// USE_MULTIPLE_STATUS_TOPICS is defined).  See mqtt_specs below for a
// full list of connections it may potentially open.  If you don't
// connect any external sensors, it will only open two connections
// (for the DHT22 and the board itself).  Each new sensor you add will
// typically add one more MQTT connection.
//
// These MQTT connections use a small amount of heap memory on the
// ANAVI Thermometer, and they also cause some additional network load
// and load on the MQTT broker, so they are disabled by default.  By
// uncommenting the following line, you can enable multiple MQTT
// connections.
// #define USE_MULTIPLE_MQTT

// Together, USE_MULTIPLE_STATUS_TOPICS and USE_MULTIPLE_MQTT defines
// 3 valid modes (and 1 forbidden mode).  The code below uses the
// MQTT_MODE_X defines to differentiate the selected mode.

#undef MQTT_MODE_MULTIPLE  // N+N: Multiple connections and status topics
#undef MQTT_MODE_MIXED     // 1+N: One connection, multiple status topics
#undef MQTT_MODE_SINGLE    // 1+1: One connection, one status topic

#ifdef USE_MULTIPLE_MQTT
#  ifdef USE_MULTIPLE_STATUS_TOPICS
#    define MQTT_MODE_MULTIPLE
#  else
#    error USE_MULTIPLE_MQTT without USE_MULTIPLE_STATUS_TOPICS is not supported
#  endif
#else
#  ifdef USE_MULTIPLE_STATUS_TOPICS
#    define MQTT_MODE_MIXED
#  else
#    define MQTT_MODE_SINGLE
#  endif
#endif

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
// ensure the button is debounced, and ensure the listener has time
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

// Define PUBLISH_FREE_HEAP to publish the amount of free heap space
// to MQTT, as <workgroup>/<machineid>/free-heap, with a value in
// this format:
//
//     { "bytes": 32109 }
#define PUBLISH_FREE_HEAP

#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <ESP8266httpUpdate.h>

// needed for library
#include <DNSServer.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient

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

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define ONE_WIRE_BUS 12
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

Adafruit_HTU21DF htu = Adafruit_HTU21DF();

Adafruit_APDS9960 apds;

// Configure supported I2C sensors
const int sensorHTU21D = 0x40;
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
// specific DHT22 unit on your board
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
float sensorBaPressure = 0;
enum i2cSensorDetected
{
    NONE = 0,
    BMP = 1,
    BH = 2,
    BOTH = 3
};
i2cSensorDetected i2cSensorToShow = NONE;

// define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "mqtt.eclipseprojects.io";
char mqtt_port[6] = "1883";
char workgroup[32] = "workgroup";
// MQTT username and password
char username[20] = "";
char password[20] = "";
#ifdef HOME_ASSISTANT_DISCOVERY
// The base name of this device.  The device name will be formed by
// appending " ANAVI Thermometer".  The sensor names will be formed by
// appending " temperature", " humidity", and other unique suffixes
// depending on which sensor produces the value.
char ha_name[32 + 1] = ""; // Make sure the machineId fits.
#endif
#ifdef OTA_UPGRADES
char ota_server[40];
#endif
char temp_scale[40] = "celsius";

// Set the temperature in Celsius or Fahrenheit
// true - Celsius, false - Fahrenheit
bool configTempCelsius = true;

// MD5 of chip ID.  If you only have a handful of thermometers and use
// your own MQTT broker (instead of mqtt.eclipseprojects.io) you may want to
// truncate the MD5 by changing the 32 to a smaller value.
char machineId[32 + 1] = "";

// flag for saving data
bool shouldSaveConfig = false;

// MQTT
const char *mqtt_username();
const char *mqtt_password();

enum MQTTName
{
    MQTT_ESP8266,
    MQTT_DHT22,
#ifndef MULTI_DS18B20_SUPPORT
    MQTT_DS18B20,
#endif
    MQTT_BMP180,
    MQTT_BH1750,
    MQTT_HTU21D,
    MQTT_APDS9960,
    MQTT_BUTTON,
#ifdef MQTT_MODE_MULTIPLE
    MQTT_LAST,
#else
    MQTT_LAST_STATUS,
#define MQTT_LAST 1
#endif
};

class MQTTConnection;
MQTTConnection *mqtt(MQTTName name);

#ifdef MQTT_MODE_SINGLE
void call_mqtt_connect_cbs(MQTTConnection *conn);
#endif

struct MQTTSpec
{
    MQTTName name;
    const char *topic;
    void (*connect_cb)(MQTTConnection *);
#ifdef MQTT_MODE_SINGLE
    bool ever_online;
#endif
};

// Represents the availability status of a single sensor.
class MQTTStatus
{
public:
    String availability_topic;

#ifdef MQTT_MODE_MIXED
    // Linked list of all sensors that were ever online.  When the
    // MQTT connection is re-established, we will update the
    // availabiliy of all of them, in case the will has set them to
    // offline.
    MQTTStatus *status_list;
#endif

    MQTTConnection *conn;

    MQTTStatus();
    void set_spec(const MQTTSpec *spec);
    void online(bool board = false);
    void offline();
    const char *topic;
    void (*connect_cb)(MQTTConnection *);
    void publish_online(bool force_update);

protected:
    bool is_online;
    bool last_online;
};

class MQTTConnection
{
public:
    WiFiClient espClient;
    PubSubClient mqttClient;

    bool requested;

    MQTTConnection();
    void set_spec(const MQTTSpec *spec);
    void connect();
    void reconnect();

    // All availability status objects announced over this connection.
    // For MQTT_MODE_SINGLE and MQTT_MODE_MULTIPLE, each connection
    // will have exactly one MQTTStatus, so the "_list" suffix is a
    // bit misleading.  For MQTT_MODE_MIXED, this will actually be a
    // linked list of all the MQTTStatus objects that have ever been
    // announced since the ANAVI Thermometer booted.
    MQTTStatus *status_list;

protected:
    String client_id;
};

MQTTStatus::MQTTStatus()
    :
#ifdef MQTT_MODE_MIXED
    status_list(0),
#endif
    topic(0), connect_cb(0), is_online(false), last_online(false)
{
}

void MQTTStatus::set_spec(const MQTTSpec *spec)
{
    topic = spec->topic;
    connect_cb = spec->connect_cb;
    availability_topic = String(workgroup) + "/" + machineId + "/" + "status" + "/" + topic;
}

void MQTTStatus::online(bool board)
{
#ifdef MQTT_MODE_SINGLE
    // If we use a single MQTT status topic, all the status indicators
    // except for the board itself (MQTT_ESP8266) are disabled.  For
    // simplicity, the online() notification for MQTT_ESP8266 also
    // specifies the board argument.

    if (!board)
        return;
#endif

#if defined(MQTT_MODE_SINGLE) || defined(MQTT_MODE_MIXED)
    if (!conn)
    {
        conn = mqtt(MQTT_ESP8266);

        // Insert this as the second element on the status_list.  The
        // first element must always be the status of the ESP8266
        // itself.
#ifdef MQTT_MODE_MIXED
        status_list = conn->status_list->status_list;
        conn->status_list->status_list = this;
#endif
        (*connect_cb)(conn);
    }
#endif

    is_online = true;

    if (!conn->requested)
        conn->connect();

    publish_online(false);
}

void MQTTStatus::offline()
{
#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)

    is_online = false;

#ifdef MQTT_MODE_MIXED
    if (!conn)
        return;
#endif

    if (conn->requested)
        publish_online(false);

#endif
#ifdef MQTT_MODE_SINGLE

    // If we use a single MQTT status topic, we never publish offline
    // messages.  The offline message is only sent as a result of the
    // MQTT will.  So we don't need to do anything here.

#endif
}

void MQTTStatus::publish_online(bool force_update)
{
    if (is_online != last_online || force_update)
    {
        conn->mqttClient.publish(availability_topic.c_str(),
                                 is_online ? "online" : "offline", true);
        last_online = is_online;
    }
}

MQTTConnection::MQTTConnection()
    : espClient(), mqttClient(espClient), requested(false),
      status_list(0), client_id(PRODUCT)
{
}

String compute_client_id(const char *base)
{
    String minId(machineId);
    if (minId.length() > 5)
    {
        minId = minId.substring(minId.length() - 5);
    }
    return String("anavi-") + minId + "-" + base;
}

void MQTTConnection::set_spec(const MQTTSpec *spec)
{
    client_id = compute_client_id(spec->topic);
    Serial.println("set_spec client_id: " + client_id);
}

void MQTTConnection::connect()
{
    requested = true;
    reconnect();
}

#ifdef MULTI_DS18B20_SUPPORT
// Handle a single connected DS18B20 sensor.
class ConnectedDS18B20
{
public:
    static constexpr int addr_size = 8;

    // All ConnectedDS18B20 objects are linked on a singly linked
    // list.  (While DS18B20Registry::loop() is running, they are
    // actually present on one of two lists.)
    ConnectedDS18B20 *next;

#ifdef MQTT_MODE_MIXED
    void publish_offline();
#endif
#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)
    void publish_online();
#endif

#ifdef MQTT_MODE_MULTIPLE
    void mqtt_loop();
#endif

    ConnectedDS18B20(const uint8_t *addr);
    void handle_temp(float wtemp);
    bool same_addr(const uint8_t (&addr)[addr_size]);
    void publish_discovery();
private:
    static int next_id;

    uint8_t raw_addr[addr_size];
    char hex_addr[2 * addr_size + 1];
    String topic;
#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)
    String availability_topic;
#endif
#ifdef MQTT_MODE_MULTIPLE
    WiFiClient espClient;
    PubSubClient mqttClient;
    String client_id;
    void reconnect();
#endif

    PubSubClient *client();
};

// Handle all connected DS18B20 devices.
class DS18B20Registry
{
public:
    DS18B20Registry();
    bool loop();
    void mqtt_loop();
#ifdef MQTT_MODE_MIXED
    void mqtt_reconnected();
#endif

private:
    // A linked list of all connected devices.
    ConnectedDS18B20 *online;

    // This points to the last entry on the online list, so that we
    // can quickly append a new node without traversing the entire
    // linked list.
    ConnectedDS18B20 **last_online;

    // When a measurement cycle begins, all nodes are moved from the
    // "online" to the "unprobed" list. As we detect the devices, they
    // are moved from "unprobed" to "online". Any device that remains
    // on "unprobed" once this is done is assumed to have been
    // disconnected; the corresponding ConnectedDS18B20 object will be
    // deleted.
    ConnectedDS18B20 *unprobed;

    // Append d last on the online list.
    ConnectedDS18B20 *append_online(ConnectedDS18B20 *d);

    // Look for a particular device on the "unprobed" list. If found,
    // unlink it and append it to the "online" list. If not found,
    // create a new device and apped it to the "online" list.
    ConnectedDS18B20 *find_device(const uint8_t (&addr)[ConnectedDS18B20::addr_size]);
};

DS18B20Registry ds18b20_registry;

#endif

void MQTTConnection::reconnect()
{
    if (!requested || mqttClient.connected())
        return;

    Serial.print("MQTT ");
    Serial.print(status_list->topic);
    Serial.print(" ");

    if (mqttClient.connect(client_id.c_str(),
                           mqtt_username(), mqtt_password(),
                           status_list->availability_topic.c_str(),
                           0, 1, "offline"))
    {
        Serial.println("connection established.");

#ifdef MQTT_MODE_MULTIPLE
        (*status_list->connect_cb)(this);
        status_list->publish_online(true);
#endif
#ifdef MQTT_MODE_MIXED
        MQTTStatus *status = status_list;
        while (status != NULL)
        {
            (*status->connect_cb)(this);
            status->publish_online(true);
            status = status->status_list;
        }
#ifdef MULTI_DS18B20_SUPPORT
        ds18b20_registry.mqtt_reconnected();
#endif
#endif
#ifdef MQTT_MODE_SINGLE
        call_mqtt_connect_cbs(this);
        status_list->publish_online(true);
#endif
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.println(mqttClient.state());
    }
}

MQTTConnection mqtt_connections[MQTT_LAST];

#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_SINGLE)
MQTTStatus mqtt_statuses[MQTT_LAST];
#endif
#if defined(MQTT_MODE_MIXED)
MQTTStatus mqtt_statuses[MQTT_LAST_STATUS];
#endif

#if defined(MQTT_MODE_MULTIPLE)
MQTTConnection *mqtt(MQTTName name)
{
    return &mqtt_connections[name];
}
#endif
#if defined(MQTT_MODE_MIXED) || defined(MQTT_MODE_SINGLE)
MQTTConnection *mqtt(MQTTName name)
{
    return &mqtt_connections[0];
}
#endif

PubSubClient *mqtt_client(MQTTName name)
{
    return &mqtt(name)->mqttClient;
}

#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)
MQTTStatus *mqtt_status(MQTTName name)
{
    return &mqtt_statuses[name];
}
#endif
#if defined(MQTT_MODE_SINGLE)
MQTTStatus *mqtt_status(MQTTName name)
{
    return &mqtt_statuses[0];
}
#endif

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
#ifndef MULTI_DS18B20_SUPPORT
char cmnd_ds_temp_coefficient_topic[20 + sizeof(machineId)];
#endif
char cmnd_temp_format[16 + sizeof(machineId)];

// The display can fit 26 "i":s on a single line.  It will fit even
// less of other characters.
char mqtt_line1[26 + 1];
char mqtt_line2[26 + 1];
char mqtt_line3[26 + 1];

String sensor_line1;
String sensor_line2;
String sensor_line3;

bool need_redraw = false;

char stat_temp_coefficient_topic[14 + sizeof(machineId)];
#ifndef MULTI_DS18B20_SUPPORT
char stat_ds_temp_coefficient_topic[20 + sizeof(machineId)];
#endif

struct Uptime
{
    // d, h, m, s and ms record the current uptime.
    int d;  // Days (0-)
    int h;  // Hours (0-23)
    int m;  // Minutes (0-59)
    int s;  // Seconds (0-59)
    int ms; // Milliseconds (0-999)

    // The value of millis() the last the the above was updated.
    // Note: this value will wrap after slightly less than 50 days.
    // In contrast, the above values won't wrap for more than 5
    // million years.
    unsigned long last_millis;
};

struct Uptime uptime;

void mqtt_esp8266_connected(MQTTConnection *c)
{
    c->mqttClient.subscribe(line1_topic);
    c->mqttClient.subscribe(line2_topic);
    c->mqttClient.subscribe(line3_topic);
#ifdef OTA_UPGRADES
    c->mqttClient.subscribe(cmnd_update_topic);
#endif
#ifdef OTA_FACTORY_RESET
    c->mqttClient.subscribe(cmnd_factory_reset_topic);
#endif
    c->mqttClient.subscribe(cmnd_restart_topic);
    c->mqttClient.subscribe(cmnd_temp_format);
    publishState();
}

void mqtt_dht22_connected(MQTTConnection *c)
{
    c->mqttClient.subscribe(cmnd_temp_coefficient_topic);

#ifdef HOME_ASSISTANT_DISCOVERY

    String homeAssistantTempScale = (true == configTempCelsius) ? "°C" : "F";
    publishSensorDiscovery("sensor",
                           "temp",
                           "temperature",
                           "Temperature",
                           "air/temperature",
                           homeAssistantTempScale.c_str(),
                           "{{ value_json.temperature | round(1) }}",
                           MQTT_DHT22);

    publishSensorDiscovery("sensor",
                           "humidity",
                           "humidity",
                           "Humidity",
                           "air/humidity",
                           "%",
                           "{{ value_json.humidity }}",
                           MQTT_DHT22);
#endif
}

#ifndef MULTI_DS18B20_SUPPORT
void mqtt_ds18b20_connected(MQTTConnection *c)
{
    c->mqttClient.subscribe(cmnd_ds_temp_coefficient_topic);

#ifdef HOME_ASSISTANT_DISCOVERY

    publishSensorDiscovery("sensor",
                           "watertemp",
                           "temperature",
                           "Water Temp",
                           "water/temperature",
                           "°C",
                           "{{ value_json.temperature }}",
                           MQTT_DS18B20);
#endif
}
#endif

void mqtt_bmp180_connected(MQTTConnection *c)
{
    c->mqttClient.subscribe(cmnd_slp_topic);
    c->mqttClient.subscribe(cmnd_altitude_topic);

#ifdef HOME_ASSISTANT_DISCOVERY

    String homeAssistantTempScale = (true == configTempCelsius) ? "°C" : "F";

    publishSensorDiscovery("sensor",
                           "bmp180-pressure",
                           "pressure",
                           "BMP180 Air Pressure",
                           "BMPpressure",
                           "hPa",
                           "{{ value_json.BMPpressure }}",
                           MQTT_BMP180);

    publishSensorDiscovery("sensor",
                           "bmp180-temp",
                           "temperature",
                           "BMP180 Temperature",
                           "BMPtemperature",
                           homeAssistantTempScale.c_str(),
                           "{{ value_json.BMPtemperature }}",
                           MQTT_BMP180);

#endif
}

void mqtt_bh1750_connected(MQTTConnection *c)
{
#ifdef HOME_ASSISTANT_DISCOVERY

    publishSensorDiscovery("sensor",
                           "light",
                           "illuminance",
                           "Light",
                           "light",
                           "Lux",
                           "{{ value_json.light }}",
                           MQTT_BH1750);

#endif
}

void mqtt_htu21d_connected(MQTTConnection *c)
{
    // FIXME: Add SensorDiscovery for this sensor!
}

void mqtt_apds9960_connected(MQTTConnection *c)
{
    // FIXME: Add SensorDiscovery for this sensor!
}

void mqtt_button_connected(MQTTConnection *c)
{
#ifdef HOME_ASSISTANT_DISCOVERY

    publishSensorDiscovery("binary_sensor",
                           "button",
                           0,
                           "Button 1",
                           "button/1",
                           0,
                           "{{ value_json.pressed }}",
                           MQTT_BUTTON);
#endif
}

// The order must match the enum MQTTName.
struct MQTTSpec mqtt_specs[] = {
    {MQTT_ESP8266, "esp8266", mqtt_esp8266_connected},
    {MQTT_DHT22, "dht22", mqtt_dht22_connected},
#ifndef MULTI_DS18B20_SUPPORT
    {MQTT_DS18B20, "ds18b20", mqtt_ds18b20_connected},
#endif
    {MQTT_BMP180, "bmp180", mqtt_bmp180_connected},
    {MQTT_BH1750, "bh1750", mqtt_bh1750_connected},
    {MQTT_HTU21D, "htu21d", mqtt_htu21d_connected},
    {MQTT_APDS9960, "apds9960", mqtt_apds9960_connected},
    {MQTT_BUTTON, "button", mqtt_button_connected},
    {MQTT_ESP8266, 0, 0}, // Sentinel used by call_mqtt_connect_cbs()
};

#ifdef MQTT_MODE_SINGLE
void call_mqtt_connect_cbs(MQTTConnection *conn)
{
    for (MQTTSpec *spec = mqtt_specs; spec->topic != 0; spec++)
    {
        if (spec->ever_online)
            (*spec->connect_cb)(conn);
    }
}
#endif

// callback notifying us of the need to save config
void saveConfigCallback()
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
    if (true == smallSize)
    {
        u8g2.setFont(u8g2_font_ncenR10_tr);
        u8g2.drawStr(0, 14, line1);
        u8g2.drawStr(0, 39, line2);
        u8g2.drawStr(0, 60, line3);
    }
    else
    {
        u8g2.setFont(u8g2_font_ncenR14_tr);
        u8g2.drawStr(0, 14, line1);
        u8g2.drawStr(0, 39, line2);
        u8g2.drawStr(0, 64, line3);
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
#ifndef MULTI_DS18B20_SUPPORT
    Serial.print("DS18B20: ");
    Serial.println(dsTemperatureCoef);
#endif
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
    Serial.println("Saving configuration to file.");
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
    String configHelper("AP ID: " + apId);
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

void mqtt_online(MQTTName name, bool board = false)
{
    mqtt_status(name)->online(board);

#ifdef MQTT_MODE_SINGLE
    MQTTSpec *spec = &mqtt_specs[name];
    if (!spec->ever_online)
    {
        (*spec->connect_cb)(mqtt(name));
        spec->ever_online = true;
    }
#endif
}

void initOneWireSensors()
{
#ifdef BUTTONSUPPORT
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
#else
    sensors.begin();
#endif
}

void setup()
{
    // put your setup code here, to run once:
    uptime.d = 0;
    uptime.h = 0;
    uptime.m = 0;
    uptime.s = 0;

    strcpy(mqtt_line1, "");
    strcpy(mqtt_line2, "");
    strcpy(mqtt_line3, "");
    need_redraw = true;
    Serial.begin(115200);
    Serial.println();

#if defined(MQTT_MODE_MULTIPLE)
    Serial.println("MQTT: N+N: Using multiple connections");
#elif defined(MQTT_MODE_MIXED)
    Serial.println("MQTT: 1+N: Using single connection, multiple status topics");
#else
    Serial.println("MQTT: 1+1: Using single connection, single status topic");
#endif

    Wire.begin();
    checkDisplay();

    timeClient.begin();
    u8g2.begin();
    dht.begin();
    initOneWireSensors();

    delay(10);

    // LED
    pinMode(pinAlarm, OUTPUT);
    // Button
    pinMode(pinButton, INPUT);

    waitForFactoryReset();

    // Machine ID
    calculateMachineId();

    // read configuration from FS json
    Serial.println("mounting FS...");

    if (SPIFFS.begin())
    {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json"))
        {
            // file exists, reading and loading
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
    // end read

    // Set MQTT topics
    sprintf(line1_topic, "cmnd/%s/line1", machineId);
    sprintf(line2_topic, "cmnd/%s/line2", machineId);
    sprintf(line3_topic, "cmnd/%s/line3", machineId);
    sprintf(cmnd_temp_coefficient_topic, "cmnd/%s/tempcoef", machineId);
    sprintf(stat_temp_coefficient_topic, "stat/%s/tempcoef", machineId);
#ifndef MULTI_DS18B20_SUPPORT
    sprintf(cmnd_ds_temp_coefficient_topic, "cmnd/%s/water/tempcoef", machineId);
    sprintf(stat_ds_temp_coefficient_topic, "stat/%s/water/tempcoef", machineId);
#endif
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
    WiFiManagerParameter custom_mqtt_ha_name("ha_name", "Device name for Home Assistant", ha_name, sizeof(ha_name));
#endif
#ifdef OTA_UPGRADES
    WiFiManagerParameter custom_ota_server("ota_server", "OTA server", ota_server, sizeof(ota_server));
#endif
    WiFiManagerParameter custom_temperature_scale("temp_scale", "Temperature scale", temp_scale, sizeof(temp_scale));

    char htmlMachineId[200];
    sprintf(htmlMachineId, "<p style=\"color: red;\">Machine ID:</p><p><b>%s</b></p><p>Copy and save the machine ID because you will need it to control the device.</p>", machineId);
    WiFiManagerParameter custom_text_machine_id(htmlMachineId);

    // WiFiManager
    // Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    // add all your parameters here
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

    // reset settings - for testing
    // wifiManager.resetSettings();

    // set minimum quality of signal so it ignores AP under that quality
    // defaults to 8%
    // wifiManager.setMinimumSignalQuality();

    // sets timeout until configuration portal gets turned off
    // useful to make it all retry or go to sleep
    // in seconds
    wifiManager.setTimeout(300);

    digitalWrite(pinAlarm, HIGH);
    drawDisplay("Connecting...", WiFi.SSID().c_str(), "", true);

    // fetches ssid and pass and tries to connect
    // if it does not connect it starts an access point
    // and goes into a blocking loop awaiting configuration
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
        // reset and try again, or maybe put it to deep sleep
        nice_restart();
    }
    WiFi.mode(WIFI_STA);

    // if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    digitalWrite(pinAlarm, LOW);

    // read updated parameters
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

    // save the custom parameters to FS
    if (shouldSaveConfig)
    {
        saveConfig();
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    drawDisplay("Connected!", "Local IP:", WiFi.localIP().toString().c_str(),
                true);
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
        for (size_t charP = 0; charP < strlen(mqtt_password()); charP++)
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
    configTempCelsius = ((0 == strlen(temp_scale)) || String(temp_scale).equalsIgnoreCase("celsius"));
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
    Serial.print("Home Assistant device name: ");
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
#ifndef OTA_SERVER
        Serial.println("No OTA server");
#endif
    }

#ifdef OTA_SERVER
    Serial.print("Hardcoded OTA server: ");
    Serial.println(OTA_SERVER);
#endif

#endif

    setup_mqtt(mqtt_server, mqtt_port);

    mqtt_online(MQTT_ESP8266, true);

    Serial.println("");
    Serial.println("-----");
    Serial.print("Machine ID: ");
    Serial.println(machineId);
    Serial.println("-----");
    Serial.println("");

    setupADPS9960();
}

void setup_mqtt(const char *mqtt_server, const char *mqtt_port)
{
    const int mqttPort = atoi(mqtt_port);

    for (int n = 0; n < MQTT_LAST; n++)
    {
        if (mqtt_specs[n].name != n)
        {
            Serial.print("FATAL: Bad MQTT spec ");
            Serial.print(n);
            Serial.println(".");
            nice_restart();
        }

        mqtt_connections[n].set_spec(&mqtt_specs[n]);
        mqtt_statuses[n].set_spec(&mqtt_specs[n]);
        mqtt_connections[n].mqttClient.setCallback(mqttCallback);
        mqtt_connections[n].status_list = &mqtt_statuses[n];
        mqtt_statuses[n].conn = &mqtt_connections[n];

#ifdef MQTT_SERVER
        mqtt_connections[n].mqttClient.setServer(MQTT_SERVER, mqttPort);
#else
        mqtt_connections[n].mqttClient.setServer(mqtt_server, mqttPort);
#endif
    }

#ifdef MQTT_MODE_MIXED
    for (int n = 1; mqtt_specs[n].topic != 0; n++)
    {
        mqtt_statuses[n].set_spec(&mqtt_specs[n]);
        mqtt_statuses[n].conn = 0;
    }
#endif
}

void setupADPS9960()
{
    if (apds.begin())
    {
        // gesture mode will be entered once proximity mode senses something close
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
        for (int iter = 0; iter < 30; iter++)
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

#ifdef OTA_SERVER
        if (!strcmp(server.c_str(), OTA_SERVER))
            ok = true;
#endif

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
        drawDisplay("Performing", "OTA upgrade.", "Stand by.");
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
            drawDisplay("OTA OK.", "Rebooting.", "");
            nice_restart();
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

void processMessageScale(const char *text)
{
    StaticJsonDocument<200> data;
    deserializeJson(data, text);
    // Set temperature to Celsius or Fahrenheit and redraw screen
    Serial.print("Changing the temperature scale to: ");
    if (data.containsKey("scale") && (0 == strcmp(data["scale"], "celsius")))
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

void mqttCallback(char *topic, byte *payload, unsigned int length)
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

#ifndef MULTI_DS18B20_SUPPORT
    if (strcmp(topic, cmnd_ds_temp_coefficient_topic) == 0)
    {
        dsTemperatureCoef = atof(text);
        save_calibration();
    }
#endif

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
    sprintf(chipId, "%d", ESP.getChipId());
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
    for (int i = 0; i < MQTT_LAST; i++)
        mqtt_connections[i].reconnect();
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
//   topic.  This should never start with a slash.
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
                            const char *value_template,
                            MQTTName mqtt_name)
{
    return publishSensorDiscovery(component, config_key, device_class,
                                  name_suffix, state_topic, unit,
                                  value_template,
                                  mqtt_status(mqtt_name)->availability_topic,
                                  mqtt_client(mqtt_name));
}

bool publishSensorDiscovery(const char *component,
                            const char *config_key,
                            const char *device_class,
                            const char *name_suffix,
                            const char *state_topic,
                            const char *unit,
                            const char *value_template,
                            String availability_topic,
                            PubSubClient *client)
{
    static char topic[80 + sizeof(machineId)];

    snprintf(topic, sizeof(topic),
             "homeassistant/%s/%s/%s/config", component, machineId, config_key);

    DynamicJsonDocument json(1024);
    if (device_class)
        json["device_class"] = device_class;
    json["name"] = String(ha_name) + " " + name_suffix;
    json["unique_id"] = String("anavi-") + machineId + "-" + config_key;
    json["state_topic"] = String(workgroup) + "/" + machineId + "/" + state_topic;
    if (unit)
        json["unit_of_measurement"] = unit;
    json["value_template"] = value_template;
    json["availability_topic"] = availability_topic;

    json["device"]["identifiers"] = machineId;
    json["device"]["manufacturer"] = "ANAVI Technology";
    json["device"]["model"] = PRODUCT;
    json["device"]["name"] = String(ha_name) + " ANAVI Thermometer";
    json["device"]["sw_version"] = ESP.getSketchMD5();

    JsonArray connections = json["device"].createNestedArray("connections").createNestedArray();
    connections.add("mac");
    connections.add(WiFi.macAddress());

    Serial.print("Home Assistant discovery topic: ");
    Serial.println(topic);

    int payload_len = measureJson(json);
    if (!client->beginPublish(topic, payload_len, true))
    {
        Serial.println("beginPublish failed!\n");
        return false;
    }

    if (serializeJson(json, *client) != payload_len)
    {
        Serial.println("writing payload: wrong size!\n");
        return false;
    }

    if (!client->endPublish())
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
    mqtt_client(MQTT_DHT22)->publish(stat_temp_coefficient_topic, payload, true);
#ifndef MULTI_DS18B20_SUPPORT
    snprintf(payload, sizeof(payload), "%f", dsTemperatureCoef);
    mqtt_client(MQTT_DS18B20)->publish(stat_ds_temp_coefficient_topic, payload, true);
#endif

#ifdef HOME_ASSISTANT_DISCOVERY

    if (isSensorAvailable(sensorBMP180))
    {
        if (configured_sea_level_pressure > 0)
        {
            publishSensorDiscovery("sensor",
                                   "bmp180-altitude",
                                   0, // No support for "altitude" in
                                      // Home Assistant, so we claim
                                      // to be a generic sensor.
                                   "BMP180 Altitude",
                                   "BMPaltitude",
                                   "m",
                                   "{{ value_json.altitude }}",
                                   MQTT_BMP180);
        }

        if (configured_altitude >= -20000)
        {
            publishSensorDiscovery("sensor",
                                   "bmp180-slp",
                                   "pressure",
                                   "BMP180 Sea-Level Pressure",
                                   "BMPsea-level-pressure",
                                   "hPa",
                                   "{{ value_json.pressure }}",
                                   MQTT_BMP180);
        }
    }

#endif
}

void publishSensorData(MQTTName mqtt_name, const char *subTopic,
                       const char *key, const float value)
{
    publishSensorData(mqtt_client(mqtt_name), subTopic,
                      key, value);
}

void publishSensorData(PubSubClient *client, const char *subTopic,
                       const char *key, const float value)
{
    StaticJsonDocument<100> json;
    json[key] = value;
    char payload[100];
    serializeJson(json, payload);
    char topic[200];
    sprintf(topic, "%s/%s/%s", workgroup, machineId, subTopic);
    client->publish(topic, payload, true);
}

void publishSensorData(MQTTName mqtt_name, const char *subTopic,
                       const char *key, const String &value)
{
    publishSensorData(mqtt_client(mqtt_name), subTopic,
                      key, value);
}

void publishSensorData(PubSubClient *client, const char *subTopic,
                       const char *key, const String &value)
{
    StaticJsonDocument<100> json;
    json[key] = value;
    char payload[100];
    serializeJson(json, payload);
    char topic[200];
    sprintf(topic, "%s/%s/%s", workgroup, machineId, subTopic);
    client->publish(topic, payload, true);
}

bool isSensorAvailable(int sensorAddress)
{
    // Check if I2C sensor is present
    Wire.beginTransmission(sensorAddress);
    return 0 == Wire.endTransmission();
}

void handleHTU21D()
{
    mqtt_online(MQTT_HTU21D);

    // Check if temperature has changed
    const float tempTemperature = htu.readTemperature();
    if (1 <= abs(tempTemperature - sensorTemperature))
    {
        // Print new temperature value
        sensorTemperature = tempTemperature;
        Serial.print("Temperature: ");
        Serial.println(formatTemperature(sensorTemperature));

        // Publish new temperature value through MQTT
        publishSensorData(MQTT_HTU21D, "temperature", "temperature",
                          convertTemperature(sensorTemperature));
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
        publishSensorData(MQTT_HTU21D, "humidity", "humidity", sensorHumidity);
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
    mqtt_online(MQTT_BH1750);

    // Wire.begin();
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
    tempAmbientLight = tempAmbientLight / 1.2;

    if (1 <= abs(tempAmbientLight - sensorAmbientLight))
    {
        // Print new brightness value
        sensorAmbientLight = tempAmbientLight;
        Serial.print("Light: ");
        Serial.print(tempAmbientLight);
        Serial.println("Lux");

        // Publish new brightness value through MQTT
        publishSensorData(MQTT_BH1750, "light", "light", sensorAmbientLight);
    }
}

void detectGesture()
{
    mqtt_online(MQTT_APDS9960);

    // read a gesture from the device
    const uint8_t gestureCode = apds.readGesture();
    // Skip if gesture has not been detected
    if (0 == gestureCode)
    {
        return;
    }
    String gesture = "";
    switch (gestureCode)
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
    publishSensorData(MQTT_APDS9960, "gesture", "gesture", gesture);
}

void handleBMP()
{
    sensors_event_t event;
    bmp.getEvent(&event);
    if (!event.pressure)
    {
        // BMP180 sensor error
        mqtt_status(MQTT_BMP180)->offline();
        return;
    }
    mqtt_online(MQTT_BMP180);
    Serial.print("BMP180 Pressure: ");
    Serial.print(event.pressure);
    Serial.println(" hPa");
    float temperature;
    bmp.getTemperature(&temperature);
    Serial.print("BMP180 Temperature: ");
    Serial.println(formatTemperature(temperature));

    sensorBaPressure = event.pressure;

    // Publish new pressure values through MQTT
    publishSensorData(MQTT_BMP180, "BMPpressure", "BMPpressure", sensorBaPressure);
    publishSensorData(MQTT_BMP180, "BMPtemperature", "BMPtemperature",
                      convertTemperature(temperature));

    if (configured_sea_level_pressure > 0)
    {
        float altitude;
        altitude = bmp.pressureToAltitude(configured_sea_level_pressure, sensorBaPressure, temperature);
        Serial.print("BMP180 Altitude: ");
        Serial.print(altitude);
        Serial.println(" m");
        publishSensorData(MQTT_BMP180, "BMPaltitude", "altitude", altitude);
    }

    if (configured_altitude >= MIN_ALTITUDE)
    {
        float slp;
        slp = bmp.seaLevelForAltitude(configured_altitude, event.pressure, temperature);
        Serial.print("BMP180 sea-level pressure: ");
        Serial.print(slp);
        Serial.println(" hPa");
        publishSensorData(MQTT_BMP180, "BMPsea-level-pressure", "pressure", slp);
    }
}

void handleSensors()
{
    i2cSensorToShow = NONE;
    if (isSensorAvailable(sensorHTU21D))
    {
        handleHTU21D();
    }
    else
    {
        mqtt_status(MQTT_HTU21D)->offline();
    }

    if (isSensorAvailable(sensorBMP180))
    {
        i2cSensorToShow = BMP;
        handleBMP();
    }
    else
    {
        mqtt_status(MQTT_BMP180)->offline();
    }

    if (isSensorAvailable(sensorBH1750))
    {
        i2cSensorToShow = (BMP == i2cSensorToShow) ? BOTH : BH;
        handleBH1750();
    }
    else
    {
        mqtt_status(MQTT_BH1750)->offline();
    }
}

float convertCelsiusToFahrenheit(float temperature)
{
    return (temperature * 9 / 5 + 32);
}

float convertTemperature(float temperature)
{
    return (true == configTempCelsius) ? temperature : convertCelsiusToFahrenheit(temperature);
}

String formatTemperature(float temperature)
{
    String unit = (true == configTempCelsius) ? "°C" : "F";
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
    sprintf(topic, "%s/%s/uptime", workgroup, machineId);
    mqtt_client(MQTT_ESP8266)->publish(topic, payload);
}

void displaySensorsDataI2C()
{
    if (BMP == i2cSensorToShow)
    {
        sensor_line3 = "Baro " + String(round(sensorBaPressure), 0) + " hPa";
    }
    else if (BH == i2cSensorToShow)
    {
        sensor_line3 = "Light " + String(sensorAmbientLight) + " lx";
    }
    else if (BOTH == i2cSensorToShow)
    {
        static int sensorSelect = 0;
        switch (++sensorSelect % 2)
        {
        case 0:
            sensor_line3 = "Light " + String(sensorAmbientLight) + " lx";
            break;
        default:
            sensor_line3 = "Baro " + String(round(sensorBaPressure), 0) + " hPa";
            break;
        }
    }
}

#ifdef MULTI_DS18B20_SUPPORT
int ConnectedDS18B20::next_id = 1;

ConnectedDS18B20::ConnectedDS18B20(const uint8_t *addr)
    : next(nullptr)
#ifdef MQTT_MODE_MULTIPLE
    , espClient()
    , mqttClient(espClient)
#endif
{
#ifdef MQTT_MODE_MULTIPLE
#ifdef MQTT_SERVER
    mqttClient.setServer(MQTT_SERVER, atoi(mqtt_port));
#else
    mqttClient.setServer(mqtt_server, atoi(mqtt_port));
#endif
#endif

    memcpy(&raw_addr, addr, addr_size);

    for (int d = 0; d < addr_size; d++)
        sprintf(hex_addr + 2 * d, "%02x", addr[d]);

    String sensor_id = String("ds18b20_") + hex_addr;
    topic = String("ds18b20/") + String(hex_addr) + "/temperature";

#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)
    availability_topic = String(workgroup) + "/" + machineId
        + "/status/" + sensor_id;
#endif

    Serial.println("Found " + sensor_id);

#ifdef MQTT_MODE_MULTIPLE
    client_id = compute_client_id((String("ds18b20-")
                                   + String(next_id++)).c_str());

    // This will establish a connection and publish the online status
    // as a side effect.
    client();
#endif
#ifdef MQTT_MODE_MIXED
    publish_online();
#endif
    publish_discovery();
}

void ConnectedDS18B20::publish_discovery()
{
#ifdef HOME_ASSISTANT_DISCOVERY

#ifdef MQTT_MODE_SINGLE
    String availability_topic = String(workgroup) + "/" + machineId
        + "/status/esp8266";
#endif

    publishSensorDiscovery("sensor",
                           (String("ds18b20_") + hex_addr).c_str(),
                           "temperature",
                           (String("DS18B20 ") + hex_addr
                            + String(" Temperature")).c_str(),
                           topic.c_str(),
                           "°C",
                           "{{ value_json.temperature }}",
                           availability_topic,
                           client());

#endif
}


#ifdef MQTT_MODE_MULTIPLE
void ConnectedDS18B20::reconnect()
{
#if 0
    // This code block can be useful for debugging, but it would print
    // sensitive data (the password), so it is disabled by default.
    Serial.print("DS18B20: client_id: ");
    Serial.println(client_id);
    Serial.print("DS18B20: mqtt_username: ");
    Serial.println(mqtt_username());
    Serial.print("DS18B20: mqtt_password: ");
    Serial.println(mqtt_password());
    Serial.print("DS18B20: availability_topic: ");
    Serial.println(availability_topic);
#endif

    Serial.print("MQTT DS18B20 ");
    Serial.print(hex_addr);
    Serial.print(": ");

    if (mqttClient.connect(client_id.c_str(),
                           mqtt_username(), mqtt_password(),
                           availability_topic.c_str(),
                           0, 1, "offline"))
    {
        Serial.println("connection established");
        publish_online();
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.println(mqttClient.state());
    }
}

void ConnectedDS18B20::mqtt_loop()
{
    mqttClient.loop();
}
#endif

PubSubClient *ConnectedDS18B20::client()
{
#ifdef MQTT_MODE_MULTIPLE
    if (!mqttClient.connected())
        reconnect();

    return &mqttClient;
#else
    return mqtt_client(MQTT_ESP8266);
#endif
}

void ConnectedDS18B20::handle_temp(float wtemp)
{
    float temp = convertTemperature(wtemp);
    Serial.println("DS18B20 " + String(hex_addr) + ":" + temp);
    publishSensorData(client(), topic.c_str(), "temperature", temp);
}

bool ConnectedDS18B20::same_addr(const uint8_t (&addr)[addr_size])
{
    return memcmp(raw_addr, &addr, sizeof raw_addr) == 0;
}

#if defined(MQTT_MODE_MULTIPLE) || defined(MQTT_MODE_MIXED)
void ConnectedDS18B20::publish_online()
{
    client()->publish(availability_topic.c_str(), "online", true);
}
#endif

#ifdef MQTT_MODE_MIXED
void ConnectedDS18B20::publish_offline()
{
    client()->publish(availability_topic.c_str(), "offline", true);
}
#endif

DS18B20Registry::DS18B20Registry()
    : online(nullptr), unprobed(nullptr)
{
    last_online = &online;
}

bool DS18B20Registry::loop()
{
    unprobed = online;
    online = nullptr;
    last_online = &online;

    sensors.requestTemperatures();

    // Always probe for hot-plugged (or unplugged) sensors.
    sensors.begin();

    int count = sensors.getDeviceCount();

    for (int ix = 0; ix < count; ix++)
    {
        uint8_t addr[ConnectedDS18B20::addr_size];

        if (!sensors.getAddress(addr, ix))
        {
            Serial.println("Can't get address of DS18B20 " + String(ix));
            continue;
        }

        float wtemp = sensors.getTempCByIndex(ix);
        if (wtemp == DEVICE_DISCONNECTED_C)
        {
            Serial.println("Lost contact with DS18B20 " + String(ix));
            continue;
        }

        ConnectedDS18B20 *dev = find_device(addr);
        dev->handle_temp(wtemp);
    }

    while (unprobed != NULL)
    {
        ConnectedDS18B20 *next = unprobed->next;
#ifdef MQTT_MODE_MIXED
        // We only need to publish the offline status explicitly in
        // MQTT_MODE_MIXED.  In MQTT_MODE_SINGLE, we don't provide an
        // availability status.  In MQTT_MODE_MULTIPLE, the
        // last-will-and-testament will cause the MQTT broker to
        // publish the offline message when we close the TCP
        // connection.
        unprobed->publish_offline();
#endif
        delete unprobed;
        unprobed = next;
    }

    return false;
}

void DS18B20Registry::mqtt_loop()
{
#ifdef MQTT_MODE_MULTIPLE
    for (ConnectedDS18B20 *dev = online; dev != nullptr; dev = dev->next)
        dev->mqtt_loop();
#endif
}

ConnectedDS18B20 *DS18B20Registry::find_device(const uint8_t (&addr)[ConnectedDS18B20::addr_size])
{
    ConnectedDS18B20 **prev = &unprobed;

    while (*prev)
    {
        ConnectedDS18B20 *tmp = *prev;

        if (tmp->same_addr(addr))
        {
            // Unlink from "unprobed".
            *prev = tmp->next;

            // Append last in "online".
            return append_online(tmp);
        }

        prev = &tmp->next;
    }

    // Create a new device, and append it to "online".
    return append_online(new ConnectedDS18B20(addr));
}

#ifdef MQTT_MODE_MIXED
// We have re-established connection with the MQTT broker.  Re-publish
// all online statuses.
void DS18B20Registry::mqtt_reconnected()
{
    for (ConnectedDS18B20 *node = online; node; node = node->next)
        node->publish_online();
}
#endif

ConnectedDS18B20 *DS18B20Registry::append_online(ConnectedDS18B20 *d)
{
    *last_online = d;
    d->next = nullptr;
    last_online = &d->next;
    return d;
}

#endif

// Returns true if we have computed something to be shown on the
// display.
bool handle_ds18b20()
{
#ifdef MULTI_DS18B20_SUPPORT
    return ds18b20_registry.loop();
#else
    int count = sensors.getDeviceCount();
    if (count <= 0)
        return false;

    sensors.requestTemperatures();
    float wtemp = sensors.getTempCByIndex(0);
    if (wtemp == DEVICE_DISCONNECTED_C)
    {
        Serial.println("Lost contact with DS18B20");
        return false;
    }

    wtemp = wtemp * dsTemperatureCoef;
    dsTemperature = wtemp;
    mqtt_online(MQTT_DS18B20);
    publishSensorData(MQTT_DS18B20, "water/temperature",
                      "temperature",
                      convertTemperature(wtemp));
    sensor_line3 = "Water " + formatTemperature(dsTemperature);
    Serial.println(sensor_line3);
    return true;
#endif
}


void loop()
{
    uptime_loop();

    for (int i = 0; i < MQTT_LAST; i++)
        mqtt_connections[i].mqttClient.loop();

#if defined(MQTT_MODE_MULTIPLE) && defined(MULTI_DS18B20_SUPPORT)
    ds18b20_registry.mqtt_loop();
#endif

    // Reconnect if there is an issue with the MQTT connection
    const unsigned long mqttConnectionMillis = millis();
    if (mqttConnectionInterval <= (mqttConnectionMillis - mqttConnectionPreviousMillis))
    {
        mqttConnectionPreviousMillis = mqttConnectionMillis;
        mqttReconnect();
    }

    // Handle gestures at a shorter interval
    if (isSensorAvailable(APDS9960_ADDRESS))
    {
        detectGesture();
    }
    else
    {
        mqtt_status(MQTT_APDS9960)->offline();
    }

    const unsigned long currentMillis = millis();

    // Handle button presses at a shorter interval
    if (haveButton && BUTTON_INTERVAL <= (currentMillis - buttonPreviousMillis))
    {
        mqtt_online(MQTT_BUTTON);

        bool currentState = digitalRead(ONE_WIRE_BUS);

        if (buttonState != currentState)
        {
            buttonState = currentState;
            publishSensorData(MQTT_BUTTON, "button/1", "pressed",
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
            mqtt_online(MQTT_DHT22);

            // Adjust temperature depending on the calibration coefficient
            temp = temp * temperatureCoef;

            dhtTemperature = temp;
            dhtHumidity = humidity;
            publishSensorData(MQTT_DHT22, "air/temperature", "temperature",
                              convertTemperature(temp));
            publishSensorData(MQTT_DHT22, "air/humidity", "humidity", humidity);

            // Calculate heat index
            float dhtHeatIndex = dht.computeHeatIndex(dhtTemperature, dhtHumidity, false);
            publishSensorData(MQTT_DHT22, "air/heatindex", "heatindex",
                              convertTemperature(dhtHeatIndex));
            Serial.println("DHT Heat Index: " + formatTemperature(dhtHeatIndex));
        }
        else
        {
            mqtt_status(MQTT_DHT22)->offline();
        }

        setDefaultSensorLines();

        long rssiValue = WiFi.RSSI();
        String rssi = "WiFi " + String(rssiValue) + " dBm";
        Serial.println(rssi);

        if (haveButton)
        {
            displayButton();
        }
        else if (handle_ds18b20())
        {
        }
        else
        {
#ifndef MULTI_DS18B20_SUPPORT
            mqtt_status(MQTT_DS18B20)->offline();
            sensors.begin(); // Probe for hotplugged DS18B20 sensor.
#endif

            if (NONE != i2cSensorToShow)
            {
                displaySensorsDataI2C();
            }
            else
            {
                static int select = 0;
                switch (++select % 2)
                {
                case 0:
                    timeClient.update();
                    sensor_line3 = "UTC: " + timeClient.getFormattedTime();
                    break;
                default:
                    sensor_line3 = rssi;
                    break;
                }
            }
        }

        need_redraw = true;

        publishSensorData(MQTT_ESP8266, "wifi/ssid", "ssid", WiFi.SSID());
        publishSensorData(MQTT_ESP8266, "wifi/bssid", "bssid", WiFi.BSSIDstr());
        publishSensorData(MQTT_ESP8266, "wifi/rssi", "rssi", rssiValue);
        publishSensorData(MQTT_ESP8266, "wifi/ip", "ip",
                          WiFi.localIP().toString());
        publishSensorData(MQTT_ESP8266, "sketch", "sketch", ESP.getSketchMD5());

#ifdef PUBLISH_CHIP_ID
        char chipid[9];
        snprintf(chipid, sizeof(chipid), "%08x", ESP.getChipId());
        publishSensorData(MQTT_ESP8266, "chipid", "chipid", chipid);
#endif

#ifdef PUBLISH_FREE_HEAP
        publishSensorData(MQTT_ESP8266, "free-heap", "bytes",
                          ESP.getFreeHeap());
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

    // Short sleep to reduce power consumption
    delay(1);
}
