#include "MainFlowControl.h"
#include "../../include/defines.h"

#include <string>
#include <vector>
#include <iomanip>
#include <sstream>

#include <esp_log.h>
#include <esp_timer.h>
#include "esp_camera.h"
#include <cJSON.h>

#include "configClass.h"
#include "helper.h"
#include "system.h"
#include "statusled.h"
#include "time_sntp.h"
#include "ClassControlCamera.h"
#include "ClassLogFile.h"
#include "gpioControl.h"
#include "webserver.h"
#include "server_file.h"
#include "connect_wlan.h"
#include "psram.h"

#ifdef ENABLE_MQTT
    #include "interface_mqtt.h"
    #include "server_mqtt.h"
#endif //ENABLE_MQTT


static const char *TAG = "MAINCTRL";

ClassFlowControl flowctrl;

static TaskHandle_t xHandletask_autodoFlow = NULL;
static bool bTaskAutoFlowCreated = false;
static int taskAutoFlowState = FLOW_TASK_STATE_SETUPMODE;
static bool reloadConfig = false;
static bool manualFlowStart = false;
static long automaticProcessInterval = 0;
static int cycleCounter = 0;
static int processingTime = 0;


bool doInit(void)
{
    bool bRetVal = true;

    // Deinit GPIO handler
    gpio_handler_deinit();

    // Deinit main flow components before init all ressources again
    // ********************************************
    flowctrl.deinitFlow();
    //heap_caps_dump(MALLOC_CAP_INTERNAL);
    //heap_caps_dump(MALLOC_CAP_SPIRAM);

    // Init cam if init not yet done.
    // Make sure this is called between deinit and init of flow components (avoid SPIRAM fragmentation)
    // ********************************************
    if (!cameraCtrl.getcameraInitSuccessful()) {
        cameraCtrl.powerResetCamera();
        esp_err_t camStatus = cameraCtrl.initCam();

        if (camStatus != ESP_OK) // Camera init failed
            return false;

        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Init camera successful");
        cameraCtrl.printCamInfo();
    }

    //  // Init main flow components
    // ********************************************
    if (!flowctrl.initFlow()) {
        flowctrl.deinitFlow();
        return false;
    }

    // Init GPIO handler
    // Note: It has to be initialized before MQTT (topic subscription)
    // and after flow init (MQTT main topic parameter)
    // ********************************************
    if (!gpio_handler_init())
        bRetVal = false;

    // Init MQTT service
    // ********************************************
    #ifdef ENABLE_MQTT
        if (!flowctrl.initMqttService())
            bRetVal = false;
    #endif //ENABLE_MQTT

    //heap_caps_dump(MALLOC_CAP_INTERNAL);
    //heap_caps_dump(MALLOC_CAP_SPIRAM);

    return bRetVal;
}


esp_err_t triggerReloadConfig(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    if (taskAutoFlowState == FLOW_TASK_STATE_INIT ||
        taskAutoFlowState == FLOW_TASK_STATE_SETUPMODE ||
        taskAutoFlowState == FLOW_TASK_STATE_IDLE_NO_AUTOSTART)
    {
        const std::string zw = "001: Reload config and redo flow initialization (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        reloadConfig = true;
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_INIT_DELAYED) {
        const std::string zw = "002: Abort waiting delay and continue with process initialization (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state.
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART) {
        const std::string zw = "003: Abort waiting delay, reload config and reinitialize process(" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        reloadConfig = true;
        xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state.
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING ||
             taskAutoFlowState == FLOW_TASK_STATE_PUBLISH_DATA ||
             taskAutoFlowState == FLOW_TASK_STATE_ADDITIONAL_TASKS)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Reload config and schedule process reinitialization");
        const std::string zw = "004: Reload config and reinitialization got scheduled (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        reloadConfig = true;
    }
    else {
        LogFile.writeToFile(ESP_LOG_WARN, TAG, "Reload configuration not possible. No main task. Request rejected");
        const std::string zw = "E90: Reload config not possible. No main task. Request rejected (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
    }
    return ESP_OK;
}


#ifdef ENABLE_MQTT
esp_err_t triggerFlowStartByMqtt(std::string _topic)
{
    if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_NO_AUTOSTART ||
        taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by MQTT topic " + _topic);
        manualFlowStart = true;

        if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART)
            xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING ||
             taskAutoFlowState == FLOW_TASK_STATE_PUBLISH_DATA ||
             taskAutoFlowState == FLOW_TASK_STATE_ADDITIONAL_TASKS)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by MQTT topic "+ _topic + " got scheduled");
        manualFlowStart = true;
    }
    else {
        LogFile.writeToFile(ESP_LOG_WARN, TAG, "Cycle start triggered by MQTT topic " + _topic + ". Main task not initialized. Request rejected");
    }

    return ESP_OK;
}
#endif //ENABLE_MQTT



void triggerFlowStartByGpio()
{
    if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_NO_AUTOSTART ||
        taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by GPIO");
        manualFlowStart = true;

        if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART)
            xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING ||
             taskAutoFlowState == FLOW_TASK_STATE_PUBLISH_DATA ||
             taskAutoFlowState == FLOW_TASK_STATE_ADDITIONAL_TASKS)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by GPIO got scheduled");
        manualFlowStart = true;
    }
    else {
        LogFile.writeToFile(ESP_LOG_WARN, TAG, "Cycle start triggered by GPIO. Main task not initialized. Request rejected");
    }
}


esp_err_t handler_cycle_start(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_NO_AUTOSTART ||
        taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART ||
        flowctrl.getActualProcessState() == FLOW_INIT_WAITING_TIME_SYNC ||
        flowctrl.getActualProcessState() == FLOW_INIT_FAILED) // Possibility to manual retrigger a cycle when init is already failed
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by REST API");
        const std::string zw = "001: Cycle start triggered by REST API (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        manualFlowStart = true;

        if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART)
            xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING ||
             taskAutoFlowState == FLOW_TASK_STATE_PUBLISH_DATA ||
             taskAutoFlowState == FLOW_TASK_STATE_ADDITIONAL_TASKS)
    {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by REST API got scheduled");
        const std::string zw = "002: Cycle start triggered by REST API got scheduled (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());

        manualFlowStart = true;
    }
    else if (taskAutoFlowState == FLOW_TASK_STATE_INIT_DELAYED) {
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "Cycle start triggered by REST API (abort state 'Initialization (delayed)'");
        const std::string zw = "003: Cycle start triggered by REST API abort initialization delay (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
        xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state
    }
    else {
        LogFile.writeToFile(ESP_LOG_WARN, TAG, "Cycle start triggered by REST API. Main task not initialized. Request rejected");
        const std::string zw = "E90: Cycle start triggered by REST API. Main task not initialized. Request rejected (" + getCurrentTimeString("%H:%M:%S") + ")";
        httpd_resp_send(req, zw.c_str(), zw.length());
    }

    return ESP_OK;
}


esp_err_t handler_fallbackvalue(httpd_req_t *req)
{
    if (taskAutoFlowState <= FLOW_TASK_STATE_INIT) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "E95: Request rejected, flow not initialized");
        return ESP_FAIL;
    }

    // Default usage message when handler gets called without any parameter
    const std::string RESTUsageInfo =
        "Handler usage:<br>"
        "- To retrieve actual Fallback Value, please provide a number sequence name only, e.g. /set_fallbackvalue?sequence=main<br>"
        "- To set Fallback Value to a new value, please provide a number sequence name and a value, e.g. /set_fallbackvalue?sequence=main&value=1234.5678<br>"
        "NOTE:<br>"
        "value >= 0.0: Set Fallback Value to provided value<br>"
        "value <  0.0: Set Fallback Value to actual RAW value (as long RAW value is a valid number, without N)";

    // Default return error message when no return is programmed
    std::string sReturnMessage = "E90: Uninitialized";

    char query[100];
    char numberSequence[50];
    char value[20] = ""; // Default: empty value

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "sequence", numberSequence, sizeof(numberSequence)) != ESP_OK) {
            // If request is incomplete
            sReturnMessage = "E91: Query parameter incomplete or invalid! "
                             "Call /set_fallbackvalue to show REST API usage info and/or check documentation";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            return ESP_FAIL;
        }

        if (httpd_query_key_value(query, "value", value, sizeof(value)) == ESP_OK) {
            //ESP_LOGD(TAG, "Value: %s", value);
        }
    }
    else {  // if no parameter is provided, print handler usage
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, RESTUsageInfo.c_str(), RESTUsageInfo.length());
        return ESP_OK;
    }

    if (strlen(value) == 0) { // If no value is povided --> return actual FallbackValue
        sReturnMessage = flowctrl.getFallbackValue(std::string(numberSequence));

        if (sReturnMessage.empty()) {
            sReturnMessage = "E92: Number sequence not found";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            return ESP_FAIL;
        }
    }
    else {
        // New value is positive: Set FallbackValue to provided value and return value
        // New value is negative and actual RAW value is a valid number: Set FallbackValue to RAW value and return value
        LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "REST API handler_fallbackvalue called: sequence: " + std::string(numberSequence) +
                                                ", value: " + std::string(value));
        if (!flowctrl.setFallbackValue(numberSequence, value)) {
            sReturnMessage = "E93: Update request rejected. Please check device logs for more details";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            return ESP_FAIL;
        }

        sReturnMessage = flowctrl.getFallbackValue(std::string(numberSequence));

        if (sReturnMessage.empty()) {
            sReturnMessage = "E92: Number sequence not found";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            return ESP_FAIL;
        }
    }

    httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());

    return ESP_OK;
}


esp_err_t handler_editflow(httpd_req_t *req)
{
    const char* APIName = "editflow:v3"; // API name and version
    char query[200];
    char valuechar[30];
    std::string task;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "task", valuechar, sizeof(valuechar)) == ESP_OK) {
            task = std::string(valuechar);
        }
    }

    if (task.compare("api_name") == 0) {
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }
    else if (task.compare("data") == 0) {
        return getDataFileList(req);
    }
    else if (task.compare("tflite") == 0) {
        return getTfliteFileList(req);
    }
    else if (task.compare("certs") == 0) {
        return getCertFileList(req);
    }
    else if (task.compare("copy") == 0) {
        httpd_query_key_value(query, "in", valuechar, sizeof(valuechar));
        std::string in = std::string(valuechar);
        httpd_query_key_value(query, "out", valuechar, sizeof(valuechar));
        std::string out = std::string(valuechar);

        in = "/sdcard" + in;
        out = "/sdcard" + out;

        copyFile(in, out);

        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Copy Done");
    }
    else if (task.compare("cutref") == 0) {
        if (taskAutoFlowState < FLOW_TASK_STATE_INIT_DELAYED) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "E90: Request rejected, flow not initialized");
            return ESP_FAIL;
        }
        // Interlock request for memory category 4MB due to memory limitation
        else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING && getSPIRAMCategory() == SPIRAMCategory_4MB) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED,
                                ("E91: Request rejected, flow in process | Actual State: " + flowctrl.getActualProcessState()).c_str());
            return ESP_FAIL;
        }

        httpd_query_key_value(query, "in", valuechar, sizeof(valuechar));
        std::string in = std::string(valuechar);

        httpd_query_key_value(query, "out", valuechar, sizeof(valuechar));
        std::string out = std::string(valuechar);

        httpd_query_key_value(query, "x", valuechar, sizeof(valuechar));
        int x = std::stoi(std::string(valuechar));

        httpd_query_key_value(query, "y", valuechar, sizeof(valuechar));
        int y = std::stoi(std::string(valuechar));

        httpd_query_key_value(query, "dx", valuechar, sizeof(valuechar));
        int dx = std::stoi(std::string(valuechar));

        httpd_query_key_value(query, "dy", valuechar, sizeof(valuechar));
        int dy = std::stoi(std::string(valuechar));

        in = "/sdcard" + in;    // --> img_tmp/reference.jpg
        out = "/sdcard" + out;  // --> img_tmp/markerX.jpg

        // 4MB RAM external SPIRAM are not sufficient to perform alignment marker update while processing cycle
        // Reuse allocated memory of CImageBasis element "rawImage" (ClassTakeImage.cpp) and interlock operation at UI level
        if (taskAutoFlowState >= FLOW_TASK_STATE_IDLE_NO_AUTOSTART && getSPIRAMCategory() == SPIRAMCategory_4MB) {
            STBIObjectPSRAM.name="rawImage";
            STBIObjectPSRAM.usePreallocated = true;
            STBIObjectPSRAM.PreallocatedMemory = flowctrl.getRawImage()->getRgbImage();
            STBIObjectPSRAM.PreallocatedMemorySize = flowctrl.getRawImage()->getMemsize();
        }
        else {
            STBIObjectPSRAM.usePreallocated = false;
        }
        // Create element, be aware: CImageBasis of reference.jpg will be created first (921kB RAM needed)
        CAlignAndCutImage* caic = new CAlignAndCutImage("cutref1", in, STBIObjectPSRAM.usePreallocated);
        caic->cutAndSaveImage(out, x, y, dx, dy);
        delete caic;

        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "CutImage Done");
    }
    else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "E92: Task not found");
        return ESP_FAIL;
    }

    return ESP_OK;
}


esp_err_t handler_process_data(httpd_req_t *req)
{
    if (!bTaskAutoFlowCreated) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "E90: Request rejected, flow not initialized");
        return ESP_FAIL;
    }

    const char* APIName = "process_data:v3"; // API name and version
    char query[200];
    char valuechar[30];
    std::string type, numberSequence;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "type", valuechar, sizeof(valuechar)) == ESP_OK) {
            type = std::string(valuechar);
        }
        if (httpd_query_key_value(query, "sequence", valuechar, sizeof(valuechar)) == ESP_OK) {
            numberSequence = std::string(valuechar);
        }
    }
    else { // default - no parameter set: send data as JSON
        esp_err_t retVal = ESP_OK;
        std::string sReturnMessage;
        cJSON *cJSONObject = cJSON_CreateObject();

        if (cJSONObject == NULL) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "E91: Error, JSON object cannot be created");
            return ESP_FAIL;
        }

        if (cJSON_AddStringToObject(cJSONObject, "api_name", APIName) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "number_sequences", flowctrl.getSequenceData().size()) == NULL)
            retVal = ESP_FAIL;

        cJSON *cJSONObjectTimestampProcessed;
        if (!cJSON_AddItemToObject(cJSONObject, "timestamp_processed", cJSONObjectTimestampProcessed = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectTimestampFallbackValue;
        if (!cJSON_AddItemToObject(cJSONObject, "timestamp_fallbackvalue", cJSONObjectTimestampFallbackValue = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectActualValue;
        if (!cJSON_AddItemToObject(cJSONObject, "actual_value", cJSONObjectActualValue = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectFallbackValue;
        if (!cJSON_AddItemToObject(cJSONObject, "fallback_value", cJSONObjectFallbackValue = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectRawValue;
        if (!cJSON_AddItemToObject(cJSONObject, "raw_value", cJSONObjectRawValue = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectValueStatus;
        if (!cJSON_AddItemToObject(cJSONObject, "value_status", cJSONObjectValueStatus = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectRatePerMin;
        if (!cJSON_AddItemToObject(cJSONObject, "rate_per_minute", cJSONObjectRatePerMin = cJSON_CreateObject()))
            retVal = ESP_FAIL;
        cJSON *cJSONObjectRatePerInterval;
        if (!cJSON_AddItemToObject(cJSONObject, "rate_per_interval", cJSONObjectRatePerInterval = cJSON_CreateObject()))
            retVal = ESP_FAIL;

        for(const auto &sequence : flowctrl.getSequenceData()) {
            if (cJSON_AddStringToObject(cJSONObjectTimestampProcessed, sequence->sequenceName.c_str(), sequence->sTimeProcessed.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectTimestampFallbackValue, sequence->sequenceName.c_str(), sequence->sTimeFallbackValue.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectActualValue, sequence->sequenceName.c_str(), sequence->sActualValue.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectFallbackValue, sequence->sequenceName.c_str(), sequence->sFallbackValue.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectRawValue, sequence->sequenceName.c_str(), sequence->sRawValue.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectValueStatus, sequence->sequenceName.c_str(), sequence->sValueStatus.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectRatePerMin, sequence->sequenceName.c_str(), sequence->sRatePerMin.c_str()) == NULL)
                retVal = ESP_FAIL;
            if (cJSON_AddStringToObject(cJSONObjectRatePerInterval, sequence->sequenceName.c_str(), sequence->sRatePerInterval.c_str()) == NULL)
                retVal = ESP_FAIL;
        }

        if (cJSON_AddStringToObject(cJSONObject, "process_status", getProcessStatus().c_str()) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "process_interval", (int)(flowctrl.getProcessInterval() * 10) / 10.0) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "process_time", getFlowProcessingTime()) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddStringToObject(cJSONObject, "process_state", flowctrl.getActualProcessStateWithTime().c_str()) == NULL)
            retVal = ESP_FAIL;

        // 0: No error, -1: Error occured, -2: Multiple errors in a row, 1: Deviation occured, 2: Multiple deviaton in a row
        if (cJSON_AddNumberToObject(cJSONObject, "process_error", flowctrl.getFlowStateErrorOrDeviation()) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "device_uptime", getUptime()) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "cycle_counter", getFlowCycleCounter()) == NULL)
            retVal = ESP_FAIL;
        if (cJSON_AddNumberToObject(cJSONObject, "wlan_rssi", getWifiRssi()) == NULL)
            retVal = ESP_FAIL;

        // Print to preallocted buffer
        char *jsonData = ((struct HttpServerData *)req->user_ctx)->scratch;

        if (!cJSON_PrintPreallocated(cJSONObject, jsonData, WEBSERVER_SCRATCH_BUFSIZE, 1)) {
            retVal = ESP_FAIL;
        }

        cJSON_Delete(cJSONObject);

        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "application/json");

        if (retVal == ESP_OK)
            httpd_resp_send(req, jsonData, strlen(jsonData));
        else
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "E92: Error while adding JSON elements");

        return retVal;
    }

    /* Legacy: Provide single data as text response */
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    if (type.compare("api_name") == 0) {
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }
    else if (type.compare("number_sequences") == 0) {
        httpd_resp_sendstr(req, std::to_string(flowctrl.getSequenceData().size()).c_str());
        return ESP_OK;
    }
    else if (type.compare("timestamp_processed") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_TIMESTAMP_PROCESSED).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_TIMESTAMP_PROCESSED, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("timestamp_fallbackvalue") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_TIMESTAMP_FALLBACKVALUE).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_TIMESTAMP_FALLBACKVALUE, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("actual_value") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_VALUE).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_VALUE, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("fallback_value") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_FALLBACKVALUE).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_FALLBACKVALUE, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("raw_value") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RAWVALUE).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RAWVALUE, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("value_status") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_VALUE_STATUS).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_VALUE_STATUS, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("rate_per_minute") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RATE_PER_MIN).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RATE_PER_MIN, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("rate_per_interval") == 0) {
        if (numberSequence.empty()) {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RATE_PER_INTERVAL).c_str());
            return ESP_OK;
        }
        else {
            httpd_resp_sendstr(req, flowctrl.getSequenceResultInline(READOUT_TYPE_RATE_PER_INTERVAL, numberSequence).c_str());
            return ESP_OK;
        }
    }
    else if (type.compare("process_status") == 0) {
        httpd_resp_sendstr(req, getProcessStatus().c_str());
        return ESP_OK;
    }
    else if (type.compare("process_interval") == 0) {
        httpd_resp_sendstr(req, to_stringWithPrecision(flowctrl.getProcessInterval(), 1).c_str());
        return ESP_OK;
    }
    else if (type.compare("process_time") == 0) {
        httpd_resp_sendstr(req, std::to_string(getFlowProcessingTime()).c_str());
        return ESP_OK;
    }
    else if (type.compare("process_state") == 0) {
        httpd_resp_sendstr(req, flowctrl.getActualProcessStateWithTime().c_str());
        return ESP_OK;
    }
    else if (type.compare("process_error") == 0) {
        // 000: No error, E01: Error occured, E02: Multiple errors in a row, 001: Deviation occured, 002: Multiple deviaton in a row
        if (flowctrl.getFlowStateErrorOrDeviation() == 0)
            httpd_resp_sendstr(req, "000: No process error/deviation");
        else if (flowctrl.getFlowStateErrorOrDeviation() == -2)
            httpd_resp_sendstr(req, "E02: Multiple process errors in row");
        else if (flowctrl.getFlowStateErrorOrDeviation() == 2)
            httpd_resp_sendstr(req, "002: Multiple process deviation in row");
        else if (flowctrl.getFlowStateErrorOrDeviation() == -1)
            httpd_resp_sendstr(req, "E01: Process error occured");
        else if (flowctrl.getFlowStateErrorOrDeviation() == 1)
            httpd_resp_sendstr(req, "001: Process deviation occured");
        return ESP_OK;
    }
    else if (type.compare("device_uptime") == 0) {
        httpd_resp_sendstr(req, std::to_string(getUptime()).c_str());
        return ESP_OK;
    }
    else if (type.compare("cycle_counter") == 0) {
        httpd_resp_sendstr(req, std::to_string(getFlowCycleCounter()).c_str());
        return ESP_OK;
    }
    else if (type.compare("wlan_rssi") == 0) {
        httpd_resp_sendstr(req, std::to_string(getWifiRssi()).c_str());
        return ESP_OK;
    }
    else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "E93: Parameter not found");
        return ESP_FAIL;
    }
}


esp_err_t handler_recognition_details(httpd_req_t *req)
{
    const char* APIName = "recognition_details:v1"; // API name and version
    char query[100];
    char valuechar[30];
    std::string type, zw;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "type", valuechar, sizeof(valuechar)) == ESP_OK) {
            type = std::string(valuechar);
        }
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/html");

    if (type.compare("api_name") == 0) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }

    /*++++++++++++++++++++++++++++++++++++++++*/
    /* Page details */
    std::string txt = "<!DOCTYPE html><html lang=\"en\" xml:lang=\"en\"><head><meta charset=\"UTF-8\"><title>Recognition Details</title></head>\n";
    txt += "<body style=\"width:660px;max-width:660px;font-family:arial;padding:0px 10px;font-size:100%;-webkit-text-size-adjust:100%; text-size-adjust:100%;\">";
    txt += "<h2 style=\"font-size:1.5em;margin-block-start:0.0em;margin-block-end:0.2em;\">Recognition Details</h2>\n";
    txt += "<details id=\"desc_details\" style=\"font-size:16px;text-align:justify;margin-right:10px;\">\n";
    txt += "<summary><strong>CLICK HERE</strong> for more information</summary>\n";
    txt += "<p>On this page recognition details including the underlaying ROI image are visualized. "
            "<br><strong>Be aware: The visualized infos are representing the last fully completed image evaluation of a digitalization cycle.</strong></p>";
    txt += "<p>\"Raw Value\" represents the value which gets extracted and combined from all the single image results but without "
            "correction of any of the post-processing checks / alogrithms. The result after post-processing validation is represented with "
            "\"Value\". In the sections \"Digit ROI\" and \"Analog ROI\" all single \"raw results\" of the respective ROI images (digit styled ROI and "
            "analog styled ROI) are visualized separated per number sequence. The taken image which was used for processing (including the overlays "
            "to highlight the relevant areas) is visualized at the bottom of this page.</p>";
    txt += "</details><hr>\n";

    // Display message if flow is not initialized or image processing active
    if (taskAutoFlowState < 3 || taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING) {
        txt += "<h4>"
                "Image recognition details are only accessable if initialization is completed and no image evaluation is ongoing. "
                "Wait a few moments and refresh this page.</h4> Current state: " + flowctrl.getActualProcessState();
        httpd_resp_sendstr_chunk(req, txt.c_str());
    }
    else {
        /*++++++++++++++++++++++++++++++++++++++++*/
        /* Result */
        txt += "<h4 style=\"font-size:16px;background-color:lightgray;padding:5px;margin-top:40px;\">Result</h4>\n";
        txt += "<table style=\"width:660px;border-collapse:collapse;table-layout:fixed;\">";
        txt += "<tr><td style=\"font-weight:bold;width:40%;padding:3px 5px;text-align:left;vertical-align:middle;border:1px solid lightgrey\">Number Sequence</td>"
                "<td style=\"font-weight:bold;padding:3px 5px;text-align:left;vertical-align:middle;border:1px solid lightgrey\">Raw Value</td>"
                "<td style=\"font-weight:bold;padding:3px 5px;text-align:left;vertical-align:middle;border:1px solid lightgrey\">Actual Value</td></tr>";

        for (const auto &sequence : flowctrl.getSequenceData()) {
            txt += "<tr><td style=\"padding:3px 5px; text-align:left;vertical-align:middle;border:1px solid lightgrey\">" +
                sequence->sequenceName + "</td><td style=\"padding:3px 5px;text-align:left;vertical-align:middle;border:1px solid lightgrey\">" +
                sequence->sRawValue + "</td><td style=\"padding:3px 5px;text-align:left;vertical-align:middle;border:1px solid lightgrey\">" +
                sequence->sActualValue + "</td></tr>";
        }

        txt += "</table>\n";
        httpd_resp_sendstr_chunk(req, txt.c_str());

        /*++++++++++++++++++++++++++++++++++++++++*/
        /* Digit ROI */
        txt = "<h4 style=\"font-size:16px;background-color:lightgray;padding:5px;\">Digit ROI</h4>\n";
        txt += "<table style=\"border-spacing:5px;\">\n";

        if (!ConfigClass::getInstance()->get()->sectionDigit.enabled) {
            txt += "<tr><td>Digit ROI processing deactivated</td>";
        }
        else {
            for (const auto &sequence : flowctrl.getSequenceData()) {
                txt += "<tr><td style=\"font-weight:bold;vertical-align:bottom;\" colspan=\"3\">Number Sequence: " +
                        sequence->sequenceName + "</td></tr>\n";
                txt += "<tr style=\"text-align:center;vertical-align:top;\">\n";

                if (!sequence->digitRoi.empty()) {
                    for (const auto &roi : sequence->digitRoi) {
                        if (roi->CNNResult > -1) // Only show image if result is set, otherwise text "No Image"
                            txt += "<td style=\"width:150px;\"><h4 style=\"margin-block-start:0.5em;margin-block-end:0.0em;\">" +
                                    roi->sCNNResult + "</h4><p style=\"margin-block-start:0.5em;margin-block-end:1.33em;\"><img "
                                    "style=\"max-width:" + to_stringWithPrecision(640/(sequence->digitRoi.size() + 1), 0) +
                                    "px\" src=\"/img_tmp/" + roi->param->roiName + "_org.jpg\"></p></td>\n";
                        else
                            txt += "<td style=\"width:150px;\"><h4 style=\"margin-block-start:0.5em;margin-block-end:0.0em;\">" +
                                    roi->sCNNResult + "</h4><p style=\"margin-block-start:0.5em;margin-block-end:1.33em;\">No Image</p></td>\n";

                    }
                }
                else {
                    txt += "<tr><td>No digit ROIs</td>";
                }
            }
        }

        txt += "</tr></table>\n";
        httpd_resp_sendstr_chunk(req, txt.c_str());

        /*++++++++++++++++++++++++++++++++++++++++*/
        /* Analog ROI */
        txt = "<h4 style=\"font-size:16px;background-color:lightgray;padding:5px;\">Analog ROI</h4>\n";
        txt += "<table style=\"border-spacing:5px;\">\n";

        if (!ConfigClass::getInstance()->get()->sectionAnalog.enabled) {
            txt += "<tr><td>Analog ROI processing deactivated</td>";
        }
        else {
            for (const auto &sequence : flowctrl.getSequenceData()) {
                txt += "<tr><td style=\"font-weight:bold;vertical-align:bottom;\" colspan=\"3\">Number Sequence: " +
                        sequence->sequenceName + "</td></tr>\n";
                txt += "<tr style=\"text-align:center;vertical-align:top;\">\n";

                if (!sequence->analogRoi.empty()) {
                    for (const auto &roi : sequence->analogRoi) {
                        if (roi->CNNResult > -1) // Only show image if result is set, otherwise text "No Image"
                            txt += "<td style=\"width:150px;\"><h4 style=\"margin-block-start:0.5em;margin-block-end:0.0em;\">" +
                                    roi->sCNNResult + "</h4><p style=\"margin-block-start:0.5em;margin-block-end:1.33em;\"><img "
                                    "style=\"max-width:" + to_stringWithPrecision(640/(sequence->analogRoi.size() + 1), 0) +
                                    "px\" src=\"/img_tmp/" + roi->param->roiName + "_org.jpg\"></p></td>\n";
                        else
                            txt += "<td style=\"width:150px;\"><h4 style=\"margin-block-start:0.5em;margin-block-end:0.0em;\">" +
                                    roi->sCNNResult + "</h4><p style=\"margin-block-start:0.5em;margin-block-end:1.33em;\">No Image</p></td>\n";
                    }
                }
                else {
                    txt += "<tr><td>No analog ROIs</td>";
                }
            }
        }

        txt += "</tr></table>\n";
        httpd_resp_sendstr_chunk(req, txt.c_str());

        /*++++++++++++++++++++++++++++++++++++++++*/
        /* Show ALG_ROI image */
        txt = "<h4 style=\"font-size:16px;background-color:lightgray;padding:5px;\">Processed Image (incl. Overlays)</h4>\n";
        txt += "<img src=\"/img_tmp/alg_roi.jpg\">\n";
        txt += "</body></html>\n";
        httpd_resp_sendstr_chunk(req, txt.c_str());
    }

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}


void setTaskAutoFlowState(int _value)
{
    taskAutoFlowState = _value;
}


int getTaskAutoFlowState()
{
    return taskAutoFlowState;
}


std::string getProcessStatus(void)
{
    std::string process_status;

    if (flowctrl.isAutoStart() && (taskAutoFlowState >= 4 && taskAutoFlowState <= 7))
        process_status = "Processing (Automatic)";
    else if (!flowctrl.isAutoStart() && (taskAutoFlowState >= 3 && taskAutoFlowState <= 7))
        process_status = "Processing (Triggered Only)";
    else if (taskAutoFlowState >= 0 && taskAutoFlowState < 3)
        process_status = "Not Processing / Not Ready";
    else
        process_status = "Status unknown: " + taskAutoFlowState;

    return process_status;
}


int getFlowCycleCounter()
{
    return cycleCounter;
}


int getFlowProcessingTime()
{
    return processingTime;
}


void task_autodoFlow(void *pvParameter)
{
    int64_t cylceStartActualTime = 0;
    time_t cycleStartTime = 0;
    bTaskAutoFlowCreated = true;

    while (true) {
        // SETUP MODE CHECK
        // ********************************************
        if (taskAutoFlowState == FLOW_TASK_STATE_SETUPMODE) {
            if (flowctrl.getStatusSetupModus()) {
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_SETUP_MODE));
                flowctrl.setActualProcessState(std::string(FLOW_SETUP_MODE));
                #ifdef ENABLE_MQTT
                    MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
                #endif //ENABLE_MQTT

                while (true) {                              // Waiting for a REQUEST
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    if (reloadConfig) {
                        reloadConfig = false;
                        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Reload configuration");

                        ConfigClass::getInstance()->reinitConfig();
                        if (!flowctrl.getStatusSetupModus()) {
                            taskAutoFlowState = FLOW_TASK_STATE_INIT;       // Setup Mode done --> Do FLOW INIT
                            break;
                        }
                    }
                }
            }
            else {
                taskAutoFlowState = FLOW_TASK_STATE_INIT;       // Setup Mode done --> Do FLOW INIT
            }
        }

        // FLOW INITIALIZATION - DELAYED
        // Delay flow initialization if reboot was triggered by software exception
        // Note: Init and logging of the event is handled already in "main.cpp"
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_INIT_DELAYED) {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Process state: " + std::string(FLOW_INIT_DELAYED));
            flowctrl.setActualProcessState(std::string(FLOW_INIT_DELAYED));
            flowctrl.setFlowStateError();
            // Right now, it's not possible to provide state via MQTT because mqtt service is not yet started

            vTaskDelay(60*5000 / portTICK_PERIOD_MS); // Wait 5 minutes to give time to do an OTA update or fetch the log

            taskAutoFlowState = FLOW_TASK_STATE_INIT; // Continue to FLOW INIT
        }

        // FLOW INITIALIZATION
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_INIT) {
            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_INIT));
            flowctrl.setActualProcessState(std::string(FLOW_INIT));
            // Right now, it's not possible to provide state via MQTT because mqtt service is not yet started
            flowctrl.clearFlowStateEventInRowCounter();

            if (!doInit()) {
                LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Process state: " + std::string(FLOW_INIT_FAILED));
                flowctrl.setActualProcessState(std::string(FLOW_INIT_FAILED));
                flowctrl.setFlowStateError();
                #ifdef ENABLE_MQTT
                if (getMQTTisConnected())
                    MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
                #endif //ENABLE_MQTT

                while (true) { // Waiting for a REQUEST
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    if (reloadConfig) { // Possibility to manual retrigger a cycle with parameter reload when init is already failed
                        reloadConfig = false;
                        manualFlowStart = false; // parameter reload has higher prio
                        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Reload configuration");
                        taskAutoFlowState = FLOW_TASK_STATE_INIT;   // Repeat FLOW INIT
                        break;
                    }
                    else if (manualFlowStart) { // Possibility to manual retrigger a cycle with manual start when init is already failed
                        manualFlowStart = false;
                        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Start process (manual trigger)");
                        taskAutoFlowState = FLOW_TASK_STATE_INIT;  // Repeat FLOW INIT
                        break;
                    }
                }
            }
            else {
                // Waiting for NTP time sync to ensure process start with valid time
                if (ConfigClass::getInstance()->get()->sectionNetwork.time.ntp.timeSyncEnabled &&
                    ConfigClass::getInstance()->get()->sectionNetwork.time.ntp.processStartInterlock)
                {
                    LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process start interlock: Waiting for time sync");
                    flowctrl.setActualProcessState(std::string(FLOW_INIT_WAITING_TIME_SYNC));
                    #ifdef ENABLE_MQTT
                    if (getMQTTisConnected())
                        MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
                    #endif //ENABLE_MQTT

                    while (true) { // Waiting for time sync
                        vTaskDelay(1000 / portTICK_PERIOD_MS);
                        if (reloadConfig) {
                            reloadConfig = false;
                            manualFlowStart = false; // Reload config has higher prio
                            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Reload configuration");
                            taskAutoFlowState = FLOW_TASK_STATE_INIT; // Return to state "FLOW INIT"
                            break;
                        }
                        else if (manualFlowStart) {
                            manualFlowStart = false;
                            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Start process (manual trigger) without synced time");
                            taskAutoFlowState = FLOW_TASK_STATE_IDLE_NO_AUTOSTART; // Continue to test if AUTOSTART is TRUE
                            break;
                        }
                        else if (getTimeIsSynced()) {
                            taskAutoFlowState = FLOW_TASK_STATE_IDLE_NO_AUTOSTART; // Continue to test if AUTOSTART is TRUE
                            break;
                        }
                    }
                }
                else {
                    taskAutoFlowState = FLOW_TASK_STATE_IDLE_NO_AUTOSTART; // Continue to test if AUTOSTART is TRUE
                }

                flowctrl.clearFlowStateEventInRowCounter();
                taskYIELD();
            }
        }

        // AUTOSTART CHECK --> AUTOMATIC OR MANUAL MODE?
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_NO_AUTOSTART) {
            if (!flowctrl.isAutoStart(automaticProcessInterval)) {
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_IDLE_NO_AUTOSTART));
                flowctrl.setActualProcessState(std::string(FLOW_IDLE_NO_AUTOSTART));
                #ifdef ENABLE_MQTT
                    MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
                #endif //ENABLE_MQTT

                while (true) { // Waiting for a REQUEST
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    if (reloadConfig) {
                        reloadConfig = false;
                        manualFlowStart = false; // Reload config has higher prio
                        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Reload configuration");
                        taskAutoFlowState = FLOW_TASK_STATE_INIT; // Return to state "FLOW INIT"
                        break;
                    }
                    else if (manualFlowStart) {
                        LogFile.writeToFile(ESP_LOG_INFO, TAG, "Start process (manual trigger)");
                        taskAutoFlowState = FLOW_TASK_STATE_IMG_PROCESSING; // Start manual triggered single cycle of "FLOW PROCESSING"
                        break;
                    }
                }
            }
            else {
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Start process (automatic trigger)");
                taskAutoFlowState = FLOW_TASK_STATE_IMG_PROCESSING; // Continue to state "FLOW PROCESSING"
            }
        }

        // IMAGE PROCESSING / EVALUATION
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_IMG_PROCESSING) {
            // Clear separation between runs
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "----------------------------------------------------------------");
            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Cycle #" + std::to_string(++cycleCounter) + " started");
            cycleStartTime = getUptime();
            cylceStartActualTime = esp_timer_get_time();

            if (flowctrl.doFlowImageEvaluation(getCurrentTimeString(DEFAULT_TIME_FORMAT))) {
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Image evaluation completed (" +
                                    std::to_string(getUptime() - cycleStartTime) + "s)");
            }
            else {
                LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Image evaluation: Process error occured");
            }

            taskAutoFlowState = FLOW_TASK_STATE_PUBLISH_DATA;               // Continue with TASKS after FLOW FINISHED
        }

        // PUBLISH DATA / RESULTS
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_PUBLISH_DATA) {

            if (!flowctrl.doFlowPublishData(getCurrentTimeString(DEFAULT_TIME_FORMAT))) {
                LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Publish data: Process error occured");
            }
            taskAutoFlowState = FLOW_TASK_STATE_ADDITIONAL_TASKS;           // Continue with TASKS after FLOW FINISHED
        }

        // ADDITIONAL TASKS
        // Process further tasks after image is fully processed and results are published
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_ADDITIONAL_TASKS) {
            // Post process handling (if neccessary)
            // ********************************************
            if (flowctrl.flowStateEventOccured()) {

                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_POST_EVENT_HANDLING));
                flowctrl.setActualProcessState(std::string(FLOW_POST_EVENT_HANDLING));
                #ifdef ENABLE_MQTT
                    MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
                #endif

                flowctrl.postProcessEventHandler();
                LogFile.removeOldDebugFiles();
            }
            else {
                flowctrl.clearFlowStateEventInRowCounter();
                #ifdef ENABLE_MQTT
                    MQTTPublish(std::string(mqttServer_getMainTopic() + "/process/status/process_error"), "0", 1, false);
                #endif
            }

            // Additional tasks
            // ********************************************
            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_ADDITIONAL_TASKS));
            flowctrl.setActualProcessState(std::string(FLOW_ADDITIONAL_TASKS));
            #ifdef ENABLE_MQTT
                MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
            #endif //ENABLE_MQTT

            // Cleanup outdated log and data files (retention policy)
            LogFile.removeOldLogFile();
            LogFile.removeOldDataLog();

            // CPU Temp -> Logfile
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "CPU Temperature: " + std::to_string((int)getSOCTemperature()) + "°C");

            // WIFI Signal Strength (RSSI) -> Logfile
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "WIFI Signal (RSSI): " + std::to_string(getWifiRssi()) + "dBm");

            processingTime = (int)(getUptime() - cycleStartTime);
            // Cycle finished -> Logfile
            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Cycle #" + std::to_string(cycleCounter) +
                    " completed (" + std::to_string(processingTime) + "s)");

            // Check if time is synchronized (if NTP is configured)
            if (getUseNtp() && !getTimeIsSynced()) {
                LogFile.writeToFile(ESP_LOG_WARN, TAG, "Time server is configured, but time is not yet set");
                setStatusLed(TIME_CHECK, 1, false);
            }

            // WIFI roaming handling (if activated)
            // ********************************************
            // Trigger client triggered roaming query
            #if (defined WLAN_USE_MESH_ROAMING && defined WLAN_USE_MESH_ROAMING_ACTIVATE_CLIENT_TRIGGERED_QUERIES)
                wifiRoamingQuery();
            #endif

            // Scan channels and check if an AP with better RSSI is available, then disconnect and try to reconnect to AP with better RSSI
            // NOTE: Keep this at the end of this state, because scan is done in blocking mode and this takes ca. 1,5 - 2s.
            #ifdef WLAN_USE_ROAMING_BY_SCANNING
                wifiRoamByScanning();
            #endif

            // Check if triggerd reload config or manually triggered single cycle
            // ********************************************
            if (taskAutoFlowState == FLOW_TASK_STATE_INIT) {
                reloadConfig = false; // reload by post process event handler has higher prio
                manualFlowStart = false; // Reload config has higher prio
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "postProcessEventHandler trigger: Reload configuration");
            }
            else if (reloadConfig) {
                reloadConfig = false;
                manualFlowStart = false; // Reload config has higher prio
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Manual trigger: Reload configuration");
                taskAutoFlowState = FLOW_TASK_STATE_INIT;                   // Return to state "FLOW INIT"
            }
            else if (manualFlowStart) {
                manualFlowStart = false;
                if (flowctrl.isAutoStart()) {
                    LogFile.writeToFile(ESP_LOG_INFO, TAG, "Start process (manual trigger)");
                    taskAutoFlowState = FLOW_TASK_STATE_IMG_PROCESSING;         // Continue with next "FLOW PROCESSING" cycle"
                }
                else {
                    taskAutoFlowState = FLOW_TASK_STATE_IDLE_NO_AUTOSTART;      // Return to state "Idle (NO AUTOSTART)"
                }
            }
            else {
                taskAutoFlowState = FLOW_TASK_STATE_IDLE_AUTOSTART;         // Continue to state "Idle (AUTOSTART / WAITING STATE)"
            }
        }

        // IDLE / WAIT STATE
        // "Wait state" until autotimer is elapsed to restart next cycle
        // ********************************************
        else if (taskAutoFlowState == FLOW_TASK_STATE_IDLE_AUTOSTART) {
            LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_IDLE_AUTOSTART));
            flowctrl.setActualProcessState(std::string(FLOW_IDLE_AUTOSTART));
            #ifdef ENABLE_MQTT
                MQTTPublish(mqttServer_getMainTopic() + "/process/status/process_state", flowctrl.getActualProcessState(), 1, false);
            #endif //ENABLE_MQTT

            int64_t processIntervalDeltaTime = (esp_timer_get_time() - cylceStartActualTime) / 1000;
            if (automaticProcessInterval > processIntervalDeltaTime) {
                vTaskDelay((automaticProcessInterval - processIntervalDeltaTime) / portTICK_PERIOD_MS);
            }

            // Check if reload config is triggered by REST API
            // ********************************************
            if (reloadConfig) {
                reloadConfig = false;
                manualFlowStart = false; // Reload config has higher prio
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Trigger: Reload configuration");
                taskAutoFlowState = FLOW_TASK_STATE_INIT;               // Return to state "FLOW INIT"
            }
            else if (manualFlowStart) {
                manualFlowStart = false;
                LogFile.writeToFile(ESP_LOG_INFO, TAG, "Start process (manual trigger)");
                taskAutoFlowState = FLOW_TASK_STATE_IMG_PROCESSING;     // Continue with next "FLOW PROCESSING" cycle"
            }
            else {
                taskAutoFlowState = FLOW_TASK_STATE_IMG_PROCESSING;     // Continue with next "FLOW PROCESSING" cycle
            }
        }

        // INVALID STATE
        // ********************************************
        else {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "taskAutoFlowState: Invalid state called. Programming error");
            flowctrl.setActualProcessState(std::string(FLOW_INVALID_STATE));
        }
    }

    // Delete task if it exits from the loop above
    // ********************************************
    vTaskDelete(NULL);
    xHandletask_autodoFlow = NULL;
}


void createMainFlowTask()
{
    #ifdef DEBUG_DETAIL_ON
            LogFile.writeHeapInfo("CreateFlowTask: start");
    #endif

    LogFile.writeToFile(ESP_LOG_INFO, TAG, "Process state: " + std::string(FLOW_CREATE_FLOW_TASK));
    flowctrl.setActualProcessState(std::string(FLOW_CREATE_FLOW_TASK));

    BaseType_t xReturned = xTaskCreatePinnedToCore(&task_autodoFlow, "task_autodoFlow", 12 * 1024, NULL, tskIDLE_PRIORITY+2, &xHandletask_autodoFlow, 0);
    if( xReturned != pdPASS ) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Failed to create task_autodoFlow");
        LogFile.writeHeapInfo("CreateFlowTask: Failed to create task");
        flowctrl.setActualProcessState(std::string(FLOW_FLOW_TASK_FAILED));
        flowctrl.setFlowStateError();
    }

    #ifdef DEBUG_DETAIL_ON
            LogFile.writeHeapInfo("CreateFlowTask: end");
    #endif
}


void deleteMainFlowTask()
{
    #ifdef DEBUG_DETAIL_ON
        ESP_LOGD(TAG, "deleteMainFlowTask: xHandletask_autodoFlow: %ld", (long) xHandletask_autodoFlow);
    #endif
    if( xHandletask_autodoFlow != NULL )
    {
        vTaskDelete(xHandletask_autodoFlow);
        xHandletask_autodoFlow = NULL;
    }
    #ifdef DEBUG_DETAIL_ON
    	ESP_LOGD(TAG, "Killed: xHandletask_autodoFlow");
    #endif
}


void registerMainFlowTaskUri(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering URI handlers");

    httpd_uri_t camuri = { };
    camuri.method    = HTTP_GET;

    camuri.uri       = "/cycle_start";
    camuri.handler   = handler_cycle_start;
    camuri.user_ctx  = NULL;
    httpd_register_uri_handler(server, &camuri);

    camuri.uri       = "/set_fallbackvalue";
    camuri.handler   = handler_fallbackvalue;
    camuri.user_ctx  = NULL;
    httpd_register_uri_handler(server, &camuri);

    camuri.uri       = "/editflow";
    camuri.handler   = handler_editflow;
    camuri.user_ctx  = NULL;
    httpd_register_uri_handler(server, &camuri);

    camuri.uri       = "/process_data";
    camuri.handler   = handler_process_data;
    camuri.user_ctx  = httpServerData; // Pass server data as context
    httpd_register_uri_handler(server, &camuri);

    camuri.uri       = "/recognition_details";
    camuri.handler   = handler_recognition_details;
    camuri.user_ctx  = NULL;
    httpd_register_uri_handler(server, &camuri);
}
