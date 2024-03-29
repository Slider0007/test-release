#include "server_mqtt.h"

#ifdef ENABLE_MQTT
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#include "esp_log.h"

#include "ClassLogFile.h"
#include "connect_wlan.h"
#include "read_wlanini.h"
#include "interface_mqtt.h"
#include "time_sntp.h"
#include "Helper.h"
#include "system.h"


static const char *TAG = "MQTT_SERVER";

extern const char* libfive_git_version(void);
extern const char* libfive_git_revision(void);
extern const char* libfive_git_branch(void);

std::vector<NumberPost*>* NUMBERS;
bool HomeassistantDiscovery = false;
std::string meterType = "";
std::string valueUnit = "";
std::string timeUnit = "";
std::string rateUnit = "Unit/Minute";
float processingInterval; // Minutes
int keepAlive = 0; // Seconds
bool retainFlag;
static std::string maintopic;
static bool sendingOf_DiscoveryAndStaticTopics_scheduled;



void mqttServer_setParameter(std::vector<NumberPost*>* _NUMBERS, int _keepAlive, float _processingInterval)
{
    NUMBERS = _NUMBERS;
    keepAlive = _keepAlive;
    processingInterval = _processingInterval; 
}


void mqttServer_setMeterType(std::string _meterType, std::string _valueUnit, std::string _timeUnit, std::string _rateUnit)
{
    meterType = _meterType;
    valueUnit = _valueUnit;
    timeUnit = _timeUnit;
    rateUnit = _rateUnit;
}


bool sendHomeAssistantDiscoveryTopic(std::string group, std::string field, std::string name, std::string icon, std::string unit, 
                                     std::string deviceClass, std::string stateClass, std::string entityCategory, int qos)
{
    std::string version = std::string(libfive_git_version());

    if (version == "") {
        version = std::string(libfive_git_branch()) + " (" + std::string(libfive_git_revision()) + ")";
    }
    
    std::string topicFull;
    std::string configTopic;
    std::string payload;

    configTopic = field;

    if (group != "" && (*NUMBERS).size() > 1) { // There is more than one meter, prepend the group so we can differentiate them
        configTopic = group + "_" + field;
        name = group + " " + name;
    }    

    if (field == "problem") { // Special binary sensor which is based on error topic
        topicFull = "homeassistant/binary_sensor/" + maintopic + "/" + configTopic + "/config";
    }
    else {
        topicFull = "homeassistant/sensor/" + maintopic + "/" + configTopic + "/config";
    }

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    payload = std::string("{")  +
        "\"~\": \"" + maintopic + "\","  +
        "\"unique_id\": \"" + maintopic + "-" + configTopic + "\","  +
        "\"object_id\": \"" + maintopic + "_" + configTopic + "\","  + // This used to generate the Entity ID
        "\"name\": \"" + name + "\","  +
        "\"icon\": \"mdi:" + icon + "\",";        

    if (group != "") {
        if (field == "problem") { // Special binary sensor which is based on error topic
            payload += "\"state_topic\": \"~/" + group + "/error\",";
            payload += "\"value_template\": \"{{ 'OFF' if 'no error' in value else 'ON'}}\",";
        }
        else {
            payload += "\"state_topic\": \"~/" + group + "/" + field + "\",";
        }
    }
    else {
        if (field == "problem") { // Special binary sensor which is based on error topic
            payload += "\"state_topic\": \"~/error\",";
            payload += "\"value_template\": \"{{ 'OFF' if 'no error' in value else 'ON'}}\",";
        }
        else {
            payload += "\"state_topic\": \"~/" + field + "\",";
        }
    }

    if (unit != "") {
        payload += "\"unit_of_meas\": \"" + unit + "\",";
    }

    if (deviceClass != "") {
        payload += "\"device_class\": \"" + deviceClass + "\",";
    }

    if (stateClass != "") {
        payload += "\"state_class\": \"" + stateClass + "\",";
    } 

    if (entityCategory != "") {
        payload += "\"entity_category\": \"" + entityCategory + "\",";
    } 

    payload += 
        "\"availability_topic\": \"~/" + std::string(LWT_TOPIC) + "\","  +
        "\"payload_available\": \"" + LWT_CONNECTED + "\","  +
        "\"payload_not_available\": \"" + LWT_DISCONNECTED + "\",";

    payload += std::string("\"device\": {")  +
        "\"identifiers\": [\"" + maintopic + "\"],"  +
        "\"name\": \"" + maintopic + "\","  +
        "\"model\": \"Meter Digitizer\","  +
        "\"manufacturer\": \"AI on the Edge Device\","  +
        "\"sw_version\": \"" + version + "\","  +
        "\"configuration_url\": \"http://" + getIPAddress() + "\""  +
    "}"  +
    "}";

    return MQTTPublish(topicFull, payload, qos, true);
}


bool MQTThomeassistantDiscovery(int qos)
{  
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to send HA Discovery Topics, device is not connected to MQTT broker");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publish HA Discovery topics (Meter Type: '" + meterType + "', Value Unit: '" + valueUnit + "' , Rate Unit: '" + rateUnit + "')");

	//int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    //                                                   Group | Field        | User Friendly Name   | Icon                      | Unit | Device Class     | State Class  | Entity Category | QOS
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "uptime",      "Uptime",              "clock-time-eight-outline", "s",   "",                "",            "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "MAC",         "MAC Address",         "network-outline",          "",    "",                "",            "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "hostname",    "Hostname",            "network-outline",          "",    "",                "",            "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "freeMem",     "Free Memory",         "memory",                   "B",   "",                "measurement", "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "wifiRSSI",    "Wi-Fi RSSI",          "wifi",                     "dBm", "signal_strength", "",            "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "CPUtemp",     "CPU Temperature",     "thermometer",              "°C",  "temperature",     "measurement", "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "interval",    "Processing Interval", "clock-time-eight-outline", "min",  ""           ,    "measurement", "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "IP",          "IP Address",          "network-outline",           "",    "",               "",            "diagnostic",     qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",    "status",      "Status",              "list-status",               "",    "",               "",            "diagnostic",     qos);


    for (int i = 0; i < (*NUMBERS).size(); ++i) {
        std::string group = (*NUMBERS)[i]->name;
        if (group == "default") {
            group = "";
        }

        //                                                   Group | Field                   | User Friendly Name            | Icon                      | Unit         | Device Class   | State Class       | Entity Category | QOS
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "actual_value",           "Actual Value",                 "gauge",                    valueUnit,      meterType,      "total_increasing", "",               qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "raw_value",              "Raw Value",                    "raw",                      "",             "",             "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "fallback_value",         "Fallback Value",              "raw",                      "",             "",             "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "value_status",           "Value Status",                 "alert-circle-outline",     "",             "",             "",                 "diagnostic",     qos);        
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "rate_per_time_unit",     "Rate (" + rateUnit + ")",      "swap-vertical",            rateUnit,       "",             "measurement",      "",               qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "rate_per_processing",    "Rate per processing interval", "arrow-expand-vertical",    valueUnit,      "",             "measurement",      "",               qos); // correctly the Unit is Unit/Interval!
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "timestamp_processed",    "Timestamp last processing",    "clock-time-eight-outline", "",             "timestamp",    "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "json",                   "JSON",                         "code-json",                "",             "",             "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group, "problem",                "Problem",                      "alert-outline",            "",             "problem",      "",                 "",               qos); // Special binary sensor which is based on error topic
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published all HA Discovery MQTT topics");

    /*int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before Publishing Homeassistand Discovery Topics: " + 
            std::to_string(aFreeInternalHeapSizeBefore) + ", after: " + std::to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            std::to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + std::to_string(aMinFreeInternalHeapSize));*/

    return allSendsSuccessed;
}


bool publishSystemData(int qos)
{
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to publish system topics, not connected to MQTT broker");
        return false;
    }

    char tmp_char[50];

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing system topics");

	//int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    allSendsSuccessed |= MQTTPublish(maintopic + "/" + std::string(LWT_TOPIC), LWT_CONNECTED, qos, retainFlag); // Publish "connected" to maintopic/connection

    sprintf(tmp_char, "%ld", (long)getUpTime());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "uptime", std::string(tmp_char), qos, retainFlag);
    
    sprintf(tmp_char, "%lu", (long) getESPHeapSizeTotal());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "freeMem", std::string(tmp_char), qos, retainFlag);

    sprintf(tmp_char, "%d", get_WIFI_RSSI());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "wifiRSSI", std::string(tmp_char), qos, retainFlag);

    sprintf(tmp_char, "%d", (int)temperatureRead());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "CPUtemp", std::string(tmp_char), qos, retainFlag);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published system topics");

	/*int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before publishing System Topics: " + 
            std::to_string(aFreeInternalHeapSizeBefore) + ", after: " + std::to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            std::to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + std::to_string(aMinFreeInternalHeapSize));*/

    return allSendsSuccessed;
}


bool publishStaticData(int qos)
{
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to publish static topics, not connected to MQTT broker");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing static topics");

	//int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "MAC", getMac(), qos, retainFlag);
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "IP", getIPAddress(), qos, retainFlag);
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "hostname", wlan_config.hostname, qos, retainFlag);

    std::stringstream stream;
    stream << std::fixed << std::setprecision(1) << processingInterval; // minutes
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "interval", stream.str(), qos, retainFlag);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published static topics");

	/*int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before Publishing Static Topics: " + 
            std::to_string(aFreeInternalHeapSizeBefore) + ", after: " + std::to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            std::to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + std::to_string(aMinFreeInternalHeapSize));*/

    return allSendsSuccessed;
}


esp_err_t handler_scheduleSendingDiscoveryAndStaticTopics(httpd_req_t *req)
{
    sendingOf_DiscoveryAndStaticTopics_scheduled = true;
    char msg[] = "Publishing of HA Discovery and static topics scheduled";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");
    
    httpd_resp_send(req, msg, strlen(msg));  
    return ESP_OK;
}


esp_err_t sendDiscovery_and_static_Topics(void)
{
    if (!sendingOf_DiscoveryAndStaticTopics_scheduled) {
        // Flag not set, nothing to do
        return ESP_OK;
    }
    
    bool success = false;

    if (HomeassistantDiscovery) {
        success = MQTThomeassistantDiscovery(1);
    }

    success |= publishStaticData(1);

    if (success) { // Success, clear the flag
        sendingOf_DiscoveryAndStaticTopics_scheduled = false;
        return ESP_OK;
    }
    else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Failed to publish one or more topics (static / HA discovery)");
        /* Keep sendingOf_DiscoveryAndStaticTopics_scheduled set so we can retry after the next processing cycle */
        return ESP_FAIL;
    }
}


void GotConnected(std::string maintopic, bool retainFlag)
{
    // Nothing to do
}


void register_server_mqtt_uri(httpd_handle_t server)
{
    httpd_uri_t uri = { };
    uri.method    = HTTP_GET;

    uri.uri       = "/mqtt_publish_discovery";
    uri.handler   = handler_scheduleSendingDiscoveryAndStaticTopics;
    uri.user_ctx  = NULL;    
    httpd_register_uri_handler(server, &uri); 
}


std::string getTimeUnit(void)
{
    return timeUnit;
}


void SetHomeassistantDiscoveryEnabled(bool enabled)
{
    HomeassistantDiscovery = enabled;
}


void setMqtt_Server_Retain(bool _retainFlag)
{
    retainFlag = _retainFlag;
}


void mqttServer_setMainTopic( std::string _maintopic)
{
    maintopic = _maintopic;
}


std::string mqttServer_getMainTopic()
{
    return maintopic;
}


void scheduleSendingStaticTopics()
{
    sendingOf_DiscoveryAndStaticTopics_scheduled = true;
}

#endif //ENABLE_MQTT
