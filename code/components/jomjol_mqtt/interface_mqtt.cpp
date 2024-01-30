#include "interface_mqtt.h"

#ifdef ENABLE_MQTT
#include "esp_log.h"
#include "cJSON.h"

#ifdef DEBUG_DETAIL_ON
#include "esp_timer.h"
#endif

#include "connect_wlan.h"
#include "mqtt_client.h"
#include "ClassLogFile.h"
#include "MainFlowControl.h"


//#define DEBUG_DETAIL_ON


static const char *TAG = "MQTT_IF";

std::map<std::string, std::function<void()>>* connectFunktionMap = NULL;  
std::map<std::string, std::function<bool(std::string, char*, int)>>* subscribeFunktionMap = NULL;

int failedOnCycle = -1;
int MQTTReconnectCnt = 0;
 
esp_mqtt_event_id_t esp_mqtt_ID = MQTT_EVENT_ANY;
// ESP_EVENT_ANY_ID

bool mqtt_enabled = false;
bool mqtt_configOK = false;
bool mqtt_initialized = false;
bool mqtt_connected = false;

esp_mqtt_client_handle_t client = NULL;
std::string uri, client_id, lwt_topic, lwt_connected, lwt_disconnected, user, password, maintopic;
bool TLSEncryption = false;
std::string TLSCACert, TLSClientCert, TLSClientKey;
int keepalive;
bool SetRetainFlag;
void (*callbackOnConnected)(std::string, bool) = NULL;


bool MQTTPublish(std::string _key, std::string _content, int qos, bool retained_flag) 
{
    if (!mqtt_enabled) {                            // MQTT sevice not started / configured (MQTT_Init not called before)      
        return false;
    }

    if (failedOnCycle == getFlowCycleCounter()) {    // we already failed in this cycle, do not retry until the next cycle
        return true; // Fail quietly
    }

    #ifdef DEBUG_DETAIL_ON  
        LogFile.WriteHeapInfo("MQTT Publish");
    #endif

    MQTT_Init(); // Re-Init client if not initialized yet/anymore

    if (mqtt_initialized && mqtt_connected) {
        #ifdef DEBUG_DETAIL_ON 
            long long int starttime = esp_timer_get_time();
        #endif
        int msg_id = esp_mqtt_client_publish(client, _key.c_str(), _content.c_str(), 0, qos, retained_flag);
        #ifdef DEBUG_DETAIL_ON 
            ESP_LOGD(TAG, "Publish msg_id %d in %lld ms", msg_id, (esp_timer_get_time() - starttime)/1000);
        #endif
        if (msg_id == -1) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Failed to publish topic '" + _key + "', retrying");   
            #ifdef DEBUG_DETAIL_ON 
                starttime = esp_timer_get_time();
            #endif
            msg_id = esp_mqtt_client_publish(client, _key.c_str(), _content.c_str(), 0, qos, retained_flag);
            #ifdef DEBUG_DETAIL_ON 
                ESP_LOGD(TAG, "Publish msg_id %d in %lld ms", msg_id, (esp_timer_get_time() - starttime)/1000);
            #endif
            if (msg_id == -1) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to publish topic '" + _key + "', skipping all MQTT publishings in this cycle");
                failedOnCycle = getFlowCycleCounter();
                return false;
            }
        }

        if (_content.length() > 80) { // Truncate message if too long
            _content.resize(80);
            _content.append("..");
        }

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Published topic: " + _key + ", content: " + _content + " (msg_id=" + std::to_string(msg_id) + ")");
        return true;
    }
    else {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publish skipped. Client not initalized or not connected. (topic: " + _key + ")");
        return false;
    }
}


static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    std::string topic = "";
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            mqtt_initialized = true;
            break;
        
        case MQTT_EVENT_CONNECTED:
            MQTTReconnectCnt = 0;
            mqtt_initialized = true;
            mqtt_connected = true;
            MQTTconnected();
            break;
        
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            MQTTReconnectCnt++;
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Disconnected, trying to reconnect");

            if (MQTTReconnectCnt >= 5) {
                MQTTReconnectCnt = 0;
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Disconnected, multiple reconnect attempts failed, still retrying");
            }
            break;
        
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA");
            ESP_LOGD(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGD(TAG, "DATA=%.*s", event->data_len, event->data);
            topic.assign(event->topic, event->topic_len);
            if (subscribeFunktionMap != NULL) {
                if (subscribeFunktionMap->find(topic) != subscribeFunktionMap->end()) {
                    ESP_LOGD(TAG, "call subcribe function for topic %s", topic.c_str());
                    (*subscribeFunktionMap)[topic](topic, event->data, event->data_len);
                }
            } else {
                ESP_LOGW(TAG, "no handler available\r\n");
            }
            break;
        
        case MQTT_EVENT_ERROR:
            // http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718033 --> chapter 3.2.2.3 

            // The server does not support the level of the MQTT protocol requested by the client
            // NOTE: Only protocol 3.1.1 is supported (refer to setting in sdkconfig)
            if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_PROTOCOL) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, unacceptable protocol version (0x01)");  
            }
            // The client identifier is correct UTF-8 but not allowed by the server
            // e.g. clientID empty (cannot be the case -> default set in firmware)
            else if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_ID_REJECTED) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, identifier rejected (0x02)");
            }
            // The network connection has been made but the MQTT service is unavailable
            else if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, Server unavailable (0x03)");
            }
            // The data in the user name or password is malformed
            else if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, malformed data in username or password (0x04)");
            }
            // The client is not authorized to connect
            else if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, not authorized. Check username/password (0x05)");
            }
            
            // Log any ESP-TLS error: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/error-codes.html
            if (TLSEncryption && (event->error_handle->esp_tls_last_esp_err != ESP_OK)) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection refused, TLS error code: " + 
                        intToHexString(event->error_handle->esp_tls_last_esp_err) + 
                        " (More infos: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/error-codes.html");
            }

            #ifdef DEBUG_DETAIL_ON 
                ESP_LOGI(TAG, "MQTT_EVENT_ERROR - esp_mqtt_error_codes:");
                ESP_LOGI(TAG, "error_type:%d", event->error_handle->error_type);
                ESP_LOGI(TAG, "connect_return_code:%d", event->error_handle->connect_return_code);
                ESP_LOGI(TAG, "esp_transport_sock_errno:%d", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "esp_tls_last_esp_err:%d", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "esp_tls_stack_err:%d", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "esp_tls_cert_verify_flags:%d", event->error_handle->esp_tls_cert_verify_flags);
            #endif

            break;
        
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, (int)event_id);
    mqtt_event_handler_cb((esp_mqtt_event_handle_t) event_data);
}


bool MQTT_Configure(std::string _mqttURI, std::string _clientid, std::string _user, std::string _password,
                    std::string _maintopic, std::string _lwt, std::string _lwt_connected, std::string _lwt_disconnected,
                    bool _TLSEncryption, std::string _TLSCACertFilename, std::string _TLSClientCertFilename,
                    std::string _TLSClientKeyFilename, int _keepalive, bool _SetRetainFlag, void *_callbackOnConnected)
{
    if ((_mqttURI.length() == 0) || (_maintopic.length() == 0) || (_clientid.length() == 0)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Init aborted! Config error (URI, MainTopic or ClientID missing)");
        return false;
    }

    uri = _mqttURI;
    client_id = _clientid;
    lwt_topic = _maintopic + "/" + _lwt;
    lwt_connected = _lwt_connected;
    lwt_disconnected = _lwt_disconnected;
    keepalive = _keepalive;
    SetRetainFlag = _SetRetainFlag;
    maintopic = _maintopic;
    callbackOnConnected = ( void (*)(std::string, bool) )(_callbackOnConnected);
    user = _user;
    password = _password;
    TLSEncryption = _TLSEncryption;

    #ifdef __HIDE_PASSWORD
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "URI: " + uri + ", clientname: " + client_id + ", user: " + user + 
                        ", password: *****, maintopic: " + maintopic + ", last-will-topic: " + lwt_topic + 
                        ", keepAlive: " + std::to_string(keepalive) + ", RetainFlag: " + std::to_string(SetRetainFlag) + 
                        ", TLSEncryption: " + std::to_string(TLSEncryption)); 
    #else
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "URI: " + uri + ", clientname: " + client_id + ", user: " + user + 
                        ", password: " + password  + ", maintopic: " + maintopic + ", last-will-topic: " + lwt_topic + 
                        ", keepAlive: " + std::to_string(keepalive)  + ", RetainFlag: " + std::to_string(SetRetainFlag) + 
                        ", TLSEncryption: " + std::to_string(TLSEncryption));
     #endif

    if (TLSEncryption) {
        if (uri.substr(0,8) != "mqtts://") {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "TLS: URI parameter needs to be configured with \'mqtts://\'");
            return false;
        }

        if (uri.substr(uri.find_last_of(":")+1, 4) != "8883") {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "TLS: URI parameter not using default MQTT TLS port \'8883\'");
        }

        if (!_TLSCACertFilename.empty()) { // TLS parameter activated and not empty
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "TLS: CA certificate file: " + _TLSCACertFilename);
            std::ifstream ifs(_TLSCACertFilename);
            std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
            TLSCACert = content;

            if (TLSCACert.empty()) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "TLS: Failed to load CA certificate");
            }
        }

        if (!_TLSClientCertFilename.empty()) {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "TLS: Client certificate file: " + _TLSClientCertFilename);
            std::ifstream cert_ifs(_TLSClientCertFilename);
            std::string cert_content((std::istreambuf_iterator<char>(cert_ifs)), (std::istreambuf_iterator<char>()));
            TLSClientCert = cert_content;

            if (TLSClientCert.empty()) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "TLS: Failed to load client certificate");
            }
        }

        if (!_TLSClientKeyFilename.empty()) {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "TLS: Client key file: " + _TLSClientKeyFilename);
            std::ifstream key_ifs(_TLSClientKeyFilename);
            std::string key_content((std::istreambuf_iterator<char>(key_ifs)), (std::istreambuf_iterator<char>()));
            TLSClientKey = key_content;

            if (TLSClientKey.empty()) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "TLS: Failed to load client key");
            }
        }
    }
    else {
        if (uri.substr(0,7) != "mqtt://") {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "URI parameter needs to be configured with \'mqtt://\'");
            return false;
        }
        
        if (uri.substr(uri.find_last_of(":")+1, 4) != "1883") {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "URI parameter not using default MQTT port \'1883\'");
        }
    }

    mqtt_configOK = true;
    return true;
}


int MQTT_Init()
{ 
    if (mqtt_initialized) {
        return 0;
    }

    if (mqtt_configOK) {                           
        mqtt_enabled = true;
    } else {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Init called, but client is not yet configured.");
        return 0;
    }

    if (!getWIFIisConnected()) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Init called, but WIFI is not yet connected.");
        return 0;
    }

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Init");
    MQTTdestroy_client(false);

    esp_mqtt_client_config_t mqtt_cfg = { };

    mqtt_cfg.broker.address.uri = uri.c_str();
    mqtt_cfg.credentials.client_id = client_id.c_str();
    mqtt_cfg.network.disable_auto_reconnect = false;     // Reconnection routine active (Default: false)
    mqtt_cfg.network.reconnect_timeout_ms = 15000;       // Try to reconnect to broker (Default: 10000ms)
    mqtt_cfg.network.timeout_ms = 20000;                 // Network Timeout (Default: 10000ms)
    mqtt_cfg.session.message_retransmit_timeout = 3000;  // Time after message resent when broker not acknowledged (QoS1, QoS2)
    mqtt_cfg.session.last_will.topic = lwt_topic.c_str();
    mqtt_cfg.session.last_will.retain = 1;
    mqtt_cfg.session.last_will.msg = lwt_disconnected.c_str();
    mqtt_cfg.session.last_will.msg_len = (int)(lwt_disconnected.length());
    mqtt_cfg.session.keepalive = keepalive;
    mqtt_cfg.buffer.size = 2048;                         // size of MQTT send/receive buffer (Default: 1024)

    if (!user.empty()) {
        mqtt_cfg.credentials.username = user.c_str();
    }

    if (!password.empty()) {
        mqtt_cfg.credentials.authentication.password = password.c_str();
    }

    if (TLSEncryption) {
        if (!TLSCACert.empty()) {
            mqtt_cfg.broker.verification.certificate = TLSCACert.c_str();
            mqtt_cfg.broker.verification.certificate_len = TLSCACert.length() + 1;  
            mqtt_cfg.broker.verification.skip_cert_common_name_check = true;    // Skip any validation of server certificate CN field
        }

        if (!TLSClientCert.empty()) {
            mqtt_cfg.credentials.authentication.certificate = TLSClientCert.c_str();
            mqtt_cfg.credentials.authentication.certificate_len = TLSClientCert.length() + 1;
        }

        if (!TLSClientKey.empty()) {       
            mqtt_cfg.credentials.authentication.key = TLSClientKey.c_str();
            mqtt_cfg.credentials.authentication.key_len = TLSClientKey.length() + 1;
        }
    }

    #ifdef DEBUG_DETAIL_ON  
        LogFile.WriteHeapInfo("MQTT Client Init");
    #endif

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client) {
        esp_err_t ret = esp_mqtt_client_register_event(client, esp_mqtt_ID, mqtt_event_handler, client);
        if (ret != ESP_OK)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Could not register event (ret=" + std::to_string(ret) + ")");
            mqtt_initialized = false;
            return -1;
        }

        #ifdef DEBUG_DETAIL_ON  
            LogFile.WriteHeapInfo("MQTT Client Start");
        #endif
        ret = esp_mqtt_client_start(client);
        if (ret != ESP_OK) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Client start failed (retval=" + std::to_string(ret) + ")");
            mqtt_initialized = false;
            return -1;
        }
        else {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Client started, waiting for established connection");
            mqtt_initialized = true;
            return 1;
        }
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Init failed, no handle created");
        mqtt_initialized = false;
        return -1;
    }
}


void MQTTdestroy_client(bool _disable = false)
{
    if (client) {
        if (mqtt_connected) {
            MQTTdestroySubscribeFunction();      
            esp_mqtt_client_disconnect(client);
            mqtt_connected = false;
        }
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
        mqtt_initialized = false;
    }

    if (_disable) // Disable MQTT service, avoid restart with MQTTPublish
        mqtt_configOK = false;
}


bool getMQTTisEnabled()
{
    return mqtt_enabled;
}


bool getMQTTisConnected()
{
    return mqtt_connected;
}


bool getMQTTisEncrypted()
{
    return TLSEncryption;
}


bool mqtt_handler_flow_start(std::string _topic, char* _data, int _data_len) 
{
    ESP_LOGD(TAG, "Handler called: topic %s, data %.*s", _topic.c_str(), _data_len, _data);

    if (_data_len > 0) {
        MQTTCtrlFlowStart(_topic);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "handler_flow_start: handler called, but no data");
    }

    return ESP_OK;
}


bool mqtt_handler_set_fallbackvalue(std::string _topic, char* _data, int _data_len) 
{
    //ESP_LOGD(TAG, "Handler called: topic %s, data %.*s", _topic.c_str(), _data_len, _data);
    //example: {"numbersname": "main", "value": 12345.1234567}

    if (_data_len > 0) {    // Check if data length > 0
        cJSON *jsonData = cJSON_Parse(_data);
        cJSON *numbersname = cJSON_GetObjectItemCaseSensitive(jsonData, "numbersname");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(jsonData, "value");

        if (cJSON_IsString(numbersname) && (numbersname->valuestring != NULL)) {    // Check if numbersname is valid
            if (cJSON_IsNumber(value)) {   // Check if value is a number
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handler_set_fallbackvalue called: numbersname: " + std::string(numbersname->valuestring) + 
                                                                                         ", value: " + std::to_string(value->valuedouble));
                if (flowctrl.UpdateFallbackValue(std::to_string(value->valuedouble), std::string(numbersname->valuestring))) {
                    cJSON_Delete(jsonData);
                    return ESP_OK;
                }
            }
            else {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "handler_set_fallbackvalue: value not a valid number (\"value\": 12345.12345)");
            }
        }
        else {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "handler_set_fallbackvalue: numbersname not a valid string (\"numbersname\": \"main\")");
        }
        cJSON_Delete(jsonData);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "handler_set_fallbackvalue: handler called, but no data received");
    }

    return ESP_FAIL;
}


void MQTTconnected()
{
    if (mqtt_connected) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Connected to broker");
        
        if (connectFunktionMap != NULL) {
            for(std::map<std::string, std::function<void()>>::iterator it = connectFunktionMap->begin(); it != connectFunktionMap->end(); ++it) {
                it->second();
                ESP_LOGD(TAG, "call connect function %s", it->first.c_str());
            }
        }

        // Subcribe to topics
        // Note: Further subsriptions are handled in GPIO class
        //*****************************************
        std::function<bool(std::string topic, char* data, int data_len)> subHandler1 = mqtt_handler_flow_start;     
        MQTTregisterSubscribeFunction(maintopic + "/ctrl/flow_start", subHandler1);        // subcribe to maintopic/ctrl/flow_start

        std::function<bool(std::string topic, char* data, int data_len)> subHandler2 = mqtt_handler_set_fallbackvalue;     
        MQTTregisterSubscribeFunction(maintopic + "/ctrl/set_fallbackvalue", subHandler2);      // subcribe to maintopic/ctrl/set_fallbackvalue

       if (subscribeFunktionMap != NULL) {
            for(std::map<std::string, std::function<bool(std::string, char*, int)>>::iterator it = subscribeFunktionMap->begin(); it != subscribeFunktionMap->end(); ++it) {
                int msg_id = esp_mqtt_client_subscribe(client, it->first.c_str(), 0);
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "topic " + it->first + " subscribe successful, msg_id=" + std::to_string(msg_id));
            }
        }

        /* Send Static Topics and Homeassistant Discovery */
        if (callbackOnConnected) {                              // Call onConnected callback routine --> mqtt_server
            callbackOnConnected(maintopic, SetRetainFlag);
        }
    }
}


void MQTTregisterConnectFunction(std::string name, std::function<void()> func)
{
    ESP_LOGD(TAG, "MQTTregisteronnectFunction %s\r\n", name.c_str());
    if (connectFunktionMap == NULL) {
        connectFunktionMap = new std::map<std::string, std::function<void()>>();
    }

    if ((*connectFunktionMap)[name] != NULL) {
        ESP_LOGW(TAG, "connect function %s already registred", name.c_str());
        return;
    }

    (*connectFunktionMap)[name] = func;

    if (mqtt_connected) {
        func();
    }
}


void MQTTunregisterConnectFunction(std::string name)
{
    ESP_LOGD(TAG, "unregisterConnnectFunction %s\r\n", name.c_str());
    if ((connectFunktionMap != NULL) && (connectFunktionMap->find(name) != connectFunktionMap->end())) {
        connectFunktionMap->erase(name);
    }
}


void MQTTregisterSubscribeFunction(std::string topic, std::function<bool(std::string, char*, int)> func)
{
    ESP_LOGD(TAG, "registerSubscribeFunction %s", topic.c_str());
    if (subscribeFunktionMap == NULL) {
        subscribeFunktionMap = new std::map<std::string, std::function<bool(std::string, char*, int)>>();
    }

    if ((*subscribeFunktionMap)[topic] != NULL) {
        ESP_LOGW(TAG, "topic %s already registered for subscription", topic.c_str());
        return;
    }

    (*subscribeFunktionMap)[topic] = func;
}


void MQTTdestroySubscribeFunction()
{
    if (subscribeFunktionMap != NULL) {
        if (mqtt_connected) {
            for(std::map<std::string, std::function<bool(std::string, char*, int)>>::iterator it = subscribeFunktionMap->begin(); it != subscribeFunktionMap->end(); ++it) {
                int msg_id = esp_mqtt_client_unsubscribe(client, it->first.c_str());
                ESP_LOGD(TAG, "topic %s unsubscribe successful, msg_id=%d", it->first.c_str(), msg_id);
            }
        }

        subscribeFunktionMap->clear();
        delete subscribeFunktionMap;
        subscribeFunktionMap = NULL;
    }
}
#endif //ENABLE_MQTT
