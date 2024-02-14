#define MQTT_MAX_PACKET_SIZE 2048
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ezLED.h>  // ezLED library
#include "HeatPump.h"

#include "MitsuCon.h"

#ifdef OTA
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#endif

WiFiClient espClient;
PubSubClient mqtt_client(espClient);
HeatPump hp;
unsigned long lastTempSend;
const char* controller_sw_version = "20240212";
ezLED blueLed(LED_BUILTIN, CTRL_CATHODE);
HardwareSerial serialBus();

const char* clientTopic(const char* topicTemplate) {
  String topic = String(topicTemplate);
  topic.replace("{id}", String(ESP.getChipId()));
  return topic.c_str();
}

int mqttConnect() {
  // wifi not connected
  if ((WiFi.status() != WL_CONNECTED)) return -1;
  if (mqtt_client.connected()) return 0;

  unsigned long lastTryConnect = 0;
  if (!mqtt_client.connected()) {
    blueLed.blink(100, 250);
    blueLed.loop();
  }

  const String clientName = String(client_id) + "-" + String(ESP.getChipId());

  while (!mqtt_client.connected()) {
    if (lastTryConnect == 0 || millis() > (lastTryConnect + 5000)) {
      lastTryConnect = millis();
      Serial.println("MQTT try connecting ...");
      if (mqtt_client.connect(clientName.c_str(), mqtt_username, mqtt_password)) {  //}, clientTopic(heatpump_availability_topic), 1, 1, "offline")) {
        mqtt_client.subscribe(heatpump_identify_command_topic);
        mqtt_client.subscribe(clientTopic(heatpump_mode_command_topic));
        mqtt_client.subscribe(clientTopic(heatpump_temperature_command_topic));
        mqtt_client.subscribe(clientTopic(heatpump_fan_mode_command_topic));
        mqtt_client.subscribe(clientTopic(heatpump_swing_mode_command_topic));
        mqtt_client.publish(clientTopic(heatpump_availability_topic), "online", true);
        Serial.println("MQTT connected");
      } else Serial.println("MQTT connection failed, retry in 5 sec");
    }
    blueLed.loop();
    delay(100);
    if ((WiFi.status() != WL_CONNECTED)) {
      Serial.println("Wi-Fi disconnected");
      blueLed.cancel();
      return -1;
    }
  }

  blueLed.cancel();
  return 0;
}

void mqttAutoDiscovery() {
  const String chip_id = String(ESP.getChipId());
  const String mqtt_discov_topic = String(mqtt_discov_prefix) + "/climate/" + chip_id + "/config";
  const char* root_topic = clientTopic(heatpump_topic);
  Serial.println(root_topic);
  JsonDocument rootDiscovery;

  rootDiscovery["name"] = name;
  rootDiscovery["uniq_id"] = chip_id;
  rootDiscovery["~"] = root_topic;
  rootDiscovery["min_temp"] = min_temp;
  rootDiscovery["max_temp"] = max_temp;
  rootDiscovery["temp_step"] = temp_step;
  rootDiscovery["temperature_unit"] = "C";
  JsonArray modes = rootDiscovery.createNestedArray("modes");
  modes.add("heat_cool");
  modes.add("cool");
  modes.add("dry");
  modes.add("heat");
  modes.add("fan_only");
  modes.add("off");
  JsonArray fan_modes = rootDiscovery.createNestedArray("fan_modes");
  fan_modes.add("auto");
  fan_modes.add("quiet");
  fan_modes.add("1");
  fan_modes.add("2");
  fan_modes.add("3");
  fan_modes.add("4");
#ifdef SWING_MODE
  JsonArray swing_modes = rootDiscovery.createNestedArray("swing_modes");
  swing_modes.add("auto");
  swing_modes.add("1");
  swing_modes.add("2");
  swing_modes.add("3");
  swing_modes.add("4");
  swing_modes.add("5");
  swing_modes.add("swing");
#endif
  rootDiscovery["avty_t"] = "~/tele/avty";
  rootDiscovery["curr_temp_t"] = "~/tele/curr";
  rootDiscovery["curr_temp_tpl"] = "{{ value_json.roomTemperature }}";
  rootDiscovery["mode_cmd_t"] = "~/cmnd/mode";
  rootDiscovery["mode_stat_t"] = "~/tele/stat";
  rootDiscovery["mode_stat_tpl"] = "{{ 'off' if value_json.power == 'OFF' else value_json.mode | lower | replace('auto', 'heat_cool') | replace('fan', 'fan_only') }}";
  rootDiscovery["temp_cmd_t"] = "~/cmnd/temp";
  rootDiscovery["temp_stat_t"] = "~/tele/stat";
  rootDiscovery["temp_stat_tpl"] = "{{ value_json.temperature }}";
  rootDiscovery["fan_mode_cmd_t"] = "~/cmnd/fan";
  rootDiscovery["fan_mode_stat_t"] = "~/tele/stat";
  rootDiscovery["fan_mode_stat_tpl"] = "{{ value_json.fan | lower }}";
  rootDiscovery["swing_mode_cmd_t"] = "~/cmnd/vane";
  rootDiscovery["swing_mode_stat_t"] = "~/tele/stat";
  rootDiscovery["swing_mode_stat_tpl"] = "{{ value_json.vane | lower }}";
  rootDiscovery["act_t"] = "~/tele/curr";
  rootDiscovery["act_tpl"] = "{%set hp='climate.'+value_json.name|lower|replace(' ','_')%}{%if states(hp)=='off'%}off{%elif states(hp)=='fan'%}fan{%elif value_json.operating==false%}idle{%elif states(hp)=='heat'%}heating{%elif states(hp)=='cool' %}cooling{%elif states(hp)=='dry' %}drying{%elif states(hp)=='heat_cool'and(state_attr(hp,'temperature')-state_attr(hp,'current_temperature')>0)%}heating{%elif states(hp)=='heat_cool'and(state_attr(hp,'temperature')-state_attr(hp,'current_temperature')<0)%}cooling{%endif%}";
#ifdef TIMER_ATTR
  rootDiscovery["json_attr_t"] = "~/tele/attr";
#endif
  JsonObject device = rootDiscovery.createNestedObject("device");
  device["name"] = name;
  JsonArray ids = device.createNestedArray("ids");
  ids.add(chip_id);
  device["mf"] = "MitsuCon";
  device["mdl"] = "Mitsubishi Electric Heat Pump";
  device["sw"] = controller_sw_version;

  char bufferDiscover[2048];
  serializeJson(rootDiscovery, bufferDiscover);

  mqtt_client.setBufferSize(2048);
  if (!mqtt_client.publish(mqtt_discov_topic.c_str(), bufferDiscover, true)) {
    mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish DISCOV topic");
  }
}

void hpSettingsChanged() {
  JsonDocument root;

  heatpumpSettings currentSettings = hp.getSettings();

  root["power"] = currentSettings.power;
  root["mode"] = currentSettings.mode;
  root["temperature"] = currentSettings.temperature;
  root["fan"] = currentSettings.fan;
  root["vane"] = currentSettings.vane;
  root["wideVane"] = currentSettings.wideVane;

  char buffer[512];
  serializeJson(root, buffer);

  if (!mqtt_client.publish(clientTopic(heatpump_state_topic), buffer, true)) {
    mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish STATE topic");
  }
}

void hpStatusChanged(heatpumpStatus currentStatus) {
  JsonDocument rootInfo;

  rootInfo["name"] = name;
  rootInfo["roomTemperature"] = currentStatus.roomTemperature;
  rootInfo["operating"] = currentStatus.operating;

  char bufferInfo[512];
  serializeJson(rootInfo, bufferInfo);

  if (!mqtt_client.publish(clientTopic(heatpump_current_topic), bufferInfo, true)) {
    mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish CURR topic");
  }

#ifdef TIMER_ATTR
  JsonDocument rootTimers;


  rootTimers["timer_set"] = currentStatus.timers.mode;
  rootTimers["timer_on_mins"] = currentStatus.timers.onMinutesSet;
  rootTimers["timer_on_remain"] = currentStatus.timers.onMinutesRemaining;
  rootTimers["timer_off_mins"] = currentStatus.timers.offMinutesSet;
  rootTimers["timer_off_remain"] = currentStatus.timers.offMinutesRemaining;

  char bufferTimers[512];
  serializeJson(rootTimers, bufferTimers);

  if (!mqtt_client.publish(clientTopic(heatpump_attribute_topic), bufferTimers, true)) {
    mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish ATTR topic");
  }
#endif

  mqtt_client.publish(clientTopic(heatpump_availability_topic), "online", true);
}

void hpPacketDebug(byte* packet, unsigned int length, char* packetDirection) {
  if (_debugMode) {
    String message;
    for (int idx = 0; idx < length; idx++) {
      if (packet[idx] < 16) {
        message += "0";
      }
      message += String(packet[idx], HEX) + " ";
    }

    JsonDocument root;

    root[packetDirection] = message;

    char buffer[512];
    serializeJson(root, buffer);

    if (!mqtt_client.publish(clientTopic(heatpump_debug_topic), buffer)) {
      mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish DEBUG topic");
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  blueLed.blinkNumberOfTimes(200, 200, 5);
  Serial.println("MQTT topic received:");
  Serial.println(topic);
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println("MQTT message received:");
  Serial.println(message); 

  // IDENTIFY
  if (strcmp(topic, heatpump_identify_command_topic) == 0) {
    Serial.println("IDENTIFY command");
    if (strcmp(message, String(ESP.getChipId()).c_str()) == 0)
      blueLed.blinkNumberOfTimes(500, 200, 25);
  } 
  else {
    JsonDocument root;
    heatpumpSettings currentSettings = hp.getSettings();
    root["power"] = currentSettings.power;
    root["mode"] = currentSettings.mode;
    root["temperature"] = currentSettings.temperature;
    root["fan"] = currentSettings.fan;
    root["vane"] = currentSettings.vane;
    if (strcmp(topic, clientTopic(heatpump_mode_command_topic)) == 0) {
      if (strcmp(message, "off") == 0) {
        const char* power = "OFF";
        hp.setPowerSetting(power);
        root["power"] = power;
      } else if (strcmp(message, "heat_cool") == 0) {
        const char* power = "ON";
        hp.setPowerSetting(power);
        root["power"] = power;
        const char* mode = "AUTO";
        hp.setModeSetting(mode);
        root["mode"] = mode;
      } else if (strcmp(message, "fan_only") == 0) {
        const char* power = "ON";
        hp.setPowerSetting(power);
        root["power"] = power;
        const char* mode = "FAN";
        hp.setModeSetting(mode);
        root["mode"] = mode;
      } else {
        const char* power = "ON";
        hp.setPowerSetting(power);
        root["power"] = power;
        const char* mode = strupr(message);
        hp.setModeSetting(mode);
        root["mode"] = mode;
      }
    } 
    else if (strcmp(topic, clientTopic(heatpump_temperature_command_topic)) == 0) {
      float temperature = atof(message);
      hp.setTemperature(temperature);
      root["temperature"] = temperature;
    } 
    else if (strcmp(topic, clientTopic(heatpump_fan_mode_command_topic)) == 0) {
      const char* fan = strupr(message);
      hp.setFanSpeed(fan);
      root["fan"] = fan;
    } 
    else if (strcmp(topic, clientTopic(heatpump_swing_mode_command_topic)) == 0) {
      const char* vane = strupr(message);
      hp.setVaneSetting(vane);
      root["vane"] = vane;
    }

    hp.update();

    char buffer[512];
    serializeJson(root, buffer);

    if (!mqtt_client.publish(clientTopic(heatpump_state_topic), buffer, true)) {
      mqtt_client.publish(clientTopic(heatpump_debug_topic), "failed to publish STATE topic");
    }
  }
}


void setup() {
  Serial.begin(115200);

  // set WiFi and connect
  Serial.println("Wi-Fi setup");
  const String hostname = String(client_id) + "-" + String(ESP.getChipId());
  WiFi.hostname(hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("Wi-Fi connecting...");
  blueLed.blink(250, 250);
  while (WiFi.status() != WL_CONNECTED) {
    blueLed.loop();
    delay(100);
  }
  Serial.println("Wi-Fi connected");
  blueLed.cancel();
  // set MQTT
  Serial.println("MQTT setup");
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);

#ifdef OTA
  // set OTA
  Serial.println("OTA setup");
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.begin();
#endif

  // connect heat pump
  Serial.println("Heat pump setup");
  hp.setSettingsChangedCallback(hpSettingsChanged);
  hp.setStatusChangedCallback(hpStatusChanged);
  hp.setPacketCallback(hpPacketDebug);
  
  Serial.println("Heat pump connect");
  hp.connect(&Serial1); // TODO: use sw serial port for ESP8266 to use Serial for debug

  lastTempSend = millis();
}

void wifiReconnect() {
  unsigned long lastTryConnect = 0;
  blueLed.blink(250, 250);
  while (WiFi.status() != WL_CONNECTED) {
    if (lastTryConnect == 0 || millis() > (lastTryConnect + 5000)) {
      WiFi.disconnect();
      WiFi.reconnect();
      lastTryConnect = millis();
    } else {
      blueLed.loop();
    }
  }
  blueLed.cancel();
}

void handleConnection(){
  while (!mqtt_client.connected()) {
    if (mqttConnect() == -1)  // WiFi disconnected
      wifiReconnect();
    else
      mqttAutoDiscovery();  // on first connect or reconnect send autodiscovery to HA
  }
}

void handleHeatPumpStatus(){
  hp.sync();
  
  if (millis() > (lastTempSend + SEND_ROOM_TEMP_INTERVAL_MS)) {
    blueLed.fade(255, 0, 1000);
    hpStatusChanged(hp.getStatus());
    lastTempSend = millis();
  }
}

void loop() {
  handleConnection();
  handleHeatPumpStatus();
  mqtt_client.loop();
  blueLed.loop();
#ifdef OTA
  ArduinoOTA.handle();
#endif
}
