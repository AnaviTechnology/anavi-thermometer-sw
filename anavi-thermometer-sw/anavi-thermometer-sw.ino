// -*- mode: c++; indent-tabs-mode: nil; c-file-style: "stroustrup" -*-
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

// If HOME_ASSISTANT_DISCOVERY is defined, the Anavi Thermometer will
// publish MQTT messages that makes Home Assistant auto-discover the
// device.  See https://www.home-assistant.io/docs/mqtt/discovery/.
//
// This requires PubSubClient 2.7.

#define HOME_ASSISTANT_DISCOVERY 1

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESP8266httpUpdate.h>

//needed for library
#include <DNSServer.h>
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

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

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

// Configure pins
const int pinAlarm = 16;
const int pinButton = 0;

bool power = false;

unsigned long sensorPreviousMillis = 0;
const long sensorInterval = 10000;

unsigned long mqttConnectionPreviousMillis = millis();
const long mqttConnectionInterval = 60000;

// Set temperature coefficient for calibration depending on an empirical research with
// comparison to DS18B20 and other temperature sensors. You may need to adjust it for the
// specfic DHT22 unit on your board
float temperatureCoef = 0.9;

// Similar, for the DS18B20 sensor.
float dsTemperatureCoef = 1.0;

float dhtTemperature = 0;
float dhtHumidity = 0;
float dsTemperature = 0;
float sensorTemperature = 0;
float sensorHumidity = 0;
uint16_t sensorAmbientLight = 0;

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "iot.eclipse.org";
char mqtt_port[6] = "1883";
char workgroup[32] = "workgroup";
// MQTT username and password
char username[20] = "";
char password[20] = "";
#ifdef HOME_ASSISTANT_DISCOVERY
char ha_name[32+1] = "";        // Make sure the machineId fits.
#endif

// MD5 of chip ID.  If you only have a handful of thermometers and use
// your own MQTT broker (instead of iot.eclips.org) you may want to
// truncate the MD5 by changing the 32 to a smaller value.
char machineId[32+1] = "";

//flag for saving data
bool shouldSaveConfig = false;

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

char cmnd_update_topic[12 + sizeof(machineId)];
char line1_topic[11 + sizeof(machineId)];
char line2_topic[11 + sizeof(machineId)];
char line3_topic[11 + sizeof(machineId)];
char cmnd_temp_coefficient_topic[14 + sizeof(machineId)];
char cmnd_ds_temp_coefficient_topic[20 + sizeof(machineId)];

// The display can fit 26 "i":s on a single line.  It will fit even
// less of other characters.
char global_line1[26+1];
char global_line2[26+1];
char global_line3[26+1];

char stat_temp_coefficient_topic[14 + sizeof(machineId)];
char stat_ds_temp_coefficient_topic[20 + sizeof(machineId)];

//callback notifying us of the need to save config
void saveConfigCallback ()
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void drawDisplay(const char *line1, const char *line2 = "", const char *line3 = "")
{
    // Write on OLED display
    // Clear the internal memory
    u8g2.clearBuffer();
    // Set appropriate font
    u8g2.setFont(u8g2_font_ncenR14_tr);
    u8g2.drawStr(0,14, line1);
    u8g2.drawStr(0,39, line2);
    u8g2.drawStr(0,64, line3);
    // Transfer internal memory to the display
    u8g2.sendBuffer();
}


void setup()
{
    // put your setup code here, to run once:
    strcpy(global_line1, "");
    strcpy(global_line2, "");
    strcpy(global_line3, "");
    Serial.begin(115200);
    Serial.println();
    u8g2.begin();
    dht.begin();
    sensors.begin();

    delay(10);

    //LED
    pinMode(pinAlarm, OUTPUT);
    //Button
    pinMode(pinButton, INPUT);

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
                DynamicJsonBuffer jsonBuffer;
                JsonObject& json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                if (json.success())
                {
                    Serial.println("\nparsed json");

                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(workgroup, json["workgroup"]);
                    strcpy(username, json["username"]);
                    strcpy(password, json["password"]);
#ifdef HOME_ASSISTANT_DISCOVERY
                    {
                        const char *s = json.get<const char*>("ha_name");
                        if (!s)
                            s = machineId;
                        snprintf(ha_name, sizeof(ha_name), "%s", s);
                    }
#endif
                }
                else
                {
                    Serial.println("failed to load json config");
                }
            }
        }
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
    sprintf(cmnd_update_topic, "cmnd/%s/update", machineId);

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
    WiFiManagerParameter custom_workgroup("workgroup", "workgroup", workgroup, 32);
    WiFiManagerParameter custom_mqtt_user("user", "MQTT username", username, 20);
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", password, 20);
#ifdef HOME_ASSISTANT_DISCOVERY
    WiFiManagerParameter custom_mqtt_ha_name("ha_name", "Sensor name for Home Assistant", ha_name, sizeof(ha_name));
#endif

    char htmlMachineId[200];
    sprintf(htmlMachineId,"<p style=\"color: red;\">Machine ID:</p><p><b>%s</b></p><p>Copy and save the machine ID because you will need it to control the device.</p>", machineId);
    WiFiManagerParameter custom_text_machine_id(htmlMachineId);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_workgroup);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);
#ifdef HOME_ASSISTANT_DISCOVERY
    wifiManager.addParameter(&custom_mqtt_ha_name);
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
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.autoConnect("ANAVI Thermometer", ""))
    {
        digitalWrite(pinAlarm, LOW);
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    digitalWrite(pinAlarm, LOW);

    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(workgroup, custom_workgroup.getValue());
    strcpy(username, custom_mqtt_user.getValue());
    strcpy(password, custom_mqtt_pass.getValue());
#ifdef HOME_ASSISTANT_DISCOVERY
    strcpy(ha_name, custom_mqtt_ha_name.getValue());
#endif

    //save the custom parameters to FS
    if (shouldSaveConfig)
    {
        Serial.println("saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        json["mqtt_server"] = mqtt_server;
        json["mqtt_port"] = mqtt_port;
        json["workgroup"] = workgroup;
        json["username"] = username;
        json["password"] = password;
#ifdef HOME_ASSISTANT_DISCOVERY
        json["ha_name"] = ha_name;
#endif

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile)
        {
            Serial.println("failed to open config file for writing");
        }

        json.printTo(Serial);
        json.printTo(configFile);
        configFile.close();
        //end save
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    drawDisplay("Connected!", "Local IP:", WiFi.localIP().toString().c_str());
    delay(2000);

    // Sensors
    htu.begin();

    // MQTT
    Serial.print("MQTT Server: ");
    Serial.println(mqtt_server);
    Serial.print("MQTT Port: ");
    Serial.println(mqtt_port);
    // Print MQTT Username
    Serial.print("MQTT Username: ");
    Serial.println(username);
    // Hide password from the log and show * instead
    char hiddenpass[20] = "";
    for (size_t charP=0; charP < strlen(password); charP++)
    {
        hiddenpass[charP] = '*';
    }
    hiddenpass[strlen(password)] = '\0';
    Serial.print("MQTT Password: ");
    Serial.println(hiddenpass);
#ifdef HOME_ASSISTANT_DISCOVERY
    Serial.print("Home Assistant sensor name: ");
    Serial.println(ha_name);
#endif

    const int mqttPort = atoi(mqtt_port);
    mqttClient.setServer(mqtt_server, mqttPort);
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
            ESP.restart();
        }
        else
        {
            // Cancel reset to factory defaults
            Serial.println("Reset to factory defaults cancelled.");
            digitalWrite(pinAlarm, LOW);
        }
    }
}

void do_ota_upgrade(char *text)
{
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(text);
    if (!json.success())
    {
        Serial.println("No success decoding JSON.\n");
    }
    else if (!json.get<const char*>("server"))
    {
        Serial.println("JSON is missing server\n");
    }
    else if (!json.get<const char*>("file"))
    {
        Serial.println("JSON is missing file\n");
    }
    else
    {
        String server = json.get<const char*>("server");
        String file = json.get<const char*>("file");
        Serial.print("Attempting to upgrade from ");
        Serial.print(server);
        Serial.print(":");
        Serial.println(file);
        ESPhttpUpdate.setLedPin(pinAlarm, HIGH);
        WiFiClient update_client;
        t_httpUpdate_return ret = ESPhttpUpdate.update(update_client,
                                                       server, 80, file);
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
        snprintf(global_line1, sizeof(global_line1), "%s", text);
    }

    if (strcmp(topic, line2_topic) == 0)
    {
        snprintf(global_line2, sizeof(global_line2), "%s", text);
    }

    if (strcmp(topic, line3_topic) == 0)
    {
        snprintf(global_line3, sizeof(global_line3), "%s", text);
    }

    if (strcmp(topic, cmnd_temp_coefficient_topic) == 0)
    {
        temperatureCoef = atof(text);
    }

    if (strcmp(topic, cmnd_ds_temp_coefficient_topic) == 0)
    {
        dsTemperatureCoef = atof(text);
    }

    if (strcmp(topic, cmnd_update_topic) == 0)
    {
        Serial.println("OTA request seen.\n");
        do_ota_upgrade(text);
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

void mqttReconnect()
{
    char clientId[18 + sizeof(machineId)];
    snprintf(clientId, sizeof(clientId), "anavi-thermometer-%s", machineId);

    // Loop until we're reconnected
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (true == mqttClient.connect(clientId, username, password))
        {
            Serial.println("connected");

            // Subscribe to MQTT topics
            mqttClient.subscribe(line1_topic);
            mqttClient.subscribe(line2_topic);
            mqttClient.subscribe(line3_topic);
            mqttClient.subscribe(cmnd_temp_coefficient_topic);
            mqttClient.subscribe(cmnd_ds_temp_coefficient_topic);
            mqttClient.subscribe(cmnd_update_topic);
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
const char *temp_template = (
    "{\"device_class\": \"temperature\", "
    "\"name\": \"%s Temp\", "
    "\"state_topic\": \"%s/%s/air/temperature\", "
    "\"unit_of_measurement\": \"°C\", "
    "\"value_template\": \"{{ value_json.temperature}}\" }");

const char *humid_template = (
    "{\"device_class\": \"humidity\", "
    "\"name\": \"%s Humidity\", "
    "\"state_topic\": \"%s/%s/air/humidity\", "
    "\"unit_of_measurement\": \"%%\", "
    "\"value_template\": \"{{ value_json.humidity}}\" }");

const char *water_temp_template = (
    "{\"device_class\": \"temperature\", "
    "\"name\": \"%s Water Temp\", "
    "\"state_topic\": \"%s/%s/water/temperature\", "
    "\"unit_of_measurement\": \"°C\", "
    "\"value_template\": \"{{ value_json.temperature}}\" }");

bool publishLargePayload(const char *topic, const char *payload, bool retained)
{
    size_t payload_len = strlen(payload);

    if (!mqttClient.beginPublish(topic, payload_len, true))
    {
        Serial.println("beginPublish failed!\n");
        return false;
    }

    if (mqttClient.write((uint8_t*)payload, payload_len) != payload_len)
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
    static char payload[300];
    static char topic[80];
    snprintf(payload, sizeof(payload), "%f", temperatureCoef);
    mqttClient.publish(stat_temp_coefficient_topic, payload, true);
    snprintf(payload, sizeof(payload), "%f", dsTemperatureCoef);
    mqttClient.publish(stat_ds_temp_coefficient_topic, payload, true);

#ifdef HOME_ASSISTANT_DISCOVERY
    snprintf(payload, sizeof(payload),
             temp_template, ha_name, workgroup, machineId);
    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/%s/temp/config", machineId);
    publishLargePayload(topic, payload, true);

    snprintf(payload, sizeof(payload),
             humid_template, ha_name, workgroup, machineId);
    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/%s/humidity/config", machineId);
    publishLargePayload(topic, payload, true);

    if (0 < sensors.getDeviceCount())
    {
        snprintf(payload, sizeof(payload),
                 water_temp_template, ha_name, workgroup, machineId);
        snprintf(topic, sizeof(topic),
                 "homeassistant/sensor/%s/watertemp/config", machineId);
        publishLargePayload(topic, payload, true);
    }
#endif
}

void publishSensorData(const char* subTopic, const char* key, const float value)
{
    StaticJsonBuffer<100> jsonBuffer;
    char payload[100];
    JsonObject& json = jsonBuffer.createObject();
    json[key] = value;
    json.printTo((char*)payload, json.measureLength() + 1);
    char topic[200];
    sprintf(topic,"%s/%s/%s", workgroup, machineId, subTopic);
    mqttClient.publish(topic, payload, true);
}

void publishSensorData(const char* subTopic, const char* key, const String& value)
{
    StaticJsonBuffer<100> jsonBuffer;
    char payload[100];
    JsonObject& json = jsonBuffer.createObject();
    json[key] = value;
    json.printTo((char*)payload, json.measureLength() + 1);
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
        Serial.print(sensorTemperature);
        Serial.println("C");

        // Publish new temperature value through MQTT
        publishSensorData("temperature", "temperature", sensorTemperature);
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
        // Print new humidity value
        sensorAmbientLight = tempAmbientLight;
        Serial.print("Light: ");
        Serial.print(tempAmbientLight);
        Serial.println("Lux");

        // Publish new humidity value through MQTT
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
}

void loop()
{
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
    if (sensorInterval <= (currentMillis - sensorPreviousMillis))
    {
        sensorPreviousMillis = currentMillis;
        handleSensors();

        float temp = dht.readTemperature();
        // Adjust temperature depending on the calibration coefficient
        temp = temp*temperatureCoef;
        float humidity = dht.readHumidity();

        if (!isnan(humidity) && !isnan(temp))
        {
            dhtTemperature = temp;
            dhtHumidity = humidity;
            publishSensorData("air/temperature", "temperature", temp);
            publishSensorData("air/humidity", "humidity", humidity);
        }
        String air="Air "+String(dhtTemperature, 1)+"C ";
        Serial.println(air);
        String hum="Humidity "+String(dhtHumidity, 0)+"%";
        Serial.println(hum);

        String rssi = String(WiFi.RSSI()) + " dBm";
        Serial.println(rssi);
        String water;
        if (0 < sensors.getDeviceCount())
        {
            sensors.requestTemperatures();
            float wtemp = sensors.getTempCByIndex(0);
            wtemp = wtemp * dsTemperatureCoef;
            dsTemperature = wtemp;
            publishSensorData("water/temperature", "temperature", wtemp);
            water="Water "+String(dsTemperature,1)+"C";
            Serial.println(water);
        }
        else
        {
            water = rssi;
        }

        publishSensorData("wifi/ssid", "ssid", WiFi.SSID());
        publishSensorData("wifi/bssid", "bssid", WiFi.BSSIDstr());
        publishSensorData("wifi/rssi", "rssi", rssi);
        publishSensorData("wifi/ip", "ip", WiFi.localIP().toString());
        publishSensorData("sketch", "sketch", ESP.getSketchMD5());

        char chipid[9];
        snprintf(chipid, sizeof(chipid), "%08x", ESP.getChipId());
        publishSensorData("chipid", "chipid", chipid);

        drawDisplay(global_line1[0] ? global_line1 : air.c_str(),
                    global_line2[0] ? global_line2 : hum.c_str(),
                    global_line3[0] ? global_line3 : water.c_str());
    }

    // Press and hold the button to reset to factory defaults
    factoryReset();
}
