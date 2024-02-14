// Home Assistant Mitsubishi Electric Heat Pump Controller https://github.com/unixko/MitsuCon
// using native MQTT Climate (HVAC) component with MQTT discovery for automatic configuration
// Set PubSubClient.h MQTT_MAX_PACKET_SIZE to 2048
#include "password.h"
#define debug_print                                                // manages most of the print and println debug, not all but most

#if defined debug_print
   #define debug_begin(x)        Serial.begin(x)
   #define debug(x)                   Serial.print(x)
   #define debugln(x)                 Serial.println(x)
#else
   #define debug_begin(x)
   #define debug(x)
   #define debugln(x)
#endif

// enable extra MQTT topic for debug/timer info
bool _debugMode  = true;
#define TIMER_ATTR
#define SWING_MODE

// comment out to disable OTA
#define OTA
const char* ota_password  = _ota_password;

// wifi settings
const char* ssid          = _ssid;
const char* password      = _password;

// mqtt server settings
const char* mqtt_server   = _mqtt_server;
const int mqtt_port       = _mqtt_port;
const char* mqtt_username = _mqtt_username;
const char* mqtt_password = _mqtt_password;

// mqtt client settings
// Change "heatpump" to be same on all lines
const char* name                                = "Heat Pump"; // Device Name displayed in Home Assistant
const char* client_id                           = "heatpump"; // WiFi hostname, OTA hostname, MQTT hostname
const char* heatpump_topic                      = "heatpump/{id}"; // MQTT topic, must be unique between heat pump unit
const char* heatpump_availability_topic         = "heatpump/{id}/tele/avty";
const char* heatpump_state_topic                = "heatpump/{id}/tele/stat";
const char* heatpump_current_topic              = "heatpump/{id}/tele/curr";
const char* heatpump_attribute_topic            = "heatpump/{id}/tele/attr";
const char* heatpump_mode_command_topic         = "heatpump/{id}/cmnd/mode";
const char* heatpump_temperature_command_topic  = "heatpump/{id}/cmnd/temp";
const char* heatpump_fan_mode_command_topic     = "heatpump/{id}/cmnd/fan";
const char* heatpump_swing_mode_command_topic   = "heatpump/{id}/cmnd/vane";
const char* heatpump_debug_topic                = "heatpump/{id}/debug";
const char* heatpump_identify_command_topic     = "heatpump/identify";

// Customization
const char* min_temp                    = "16"; // Minimum temperature, check value from heatpump remote control
const char* max_temp                    = "31"; // Maximum temperature, check value from heatpump remote control
const char* temp_step                   = "1"; // Temperature setting step, check value from heatpump remote control
const char* mqtt_discov_prefix          = "homeassistant"; // Home Assistant MQTT Discovery Prefix

// sketch settings
const unsigned int SEND_ROOM_TEMP_INTERVAL_MS = 60000;
