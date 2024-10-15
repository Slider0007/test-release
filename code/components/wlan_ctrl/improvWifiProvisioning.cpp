#include "improvWifiProvisioning.h"
#include "../../include/defines.h"

#include <string.h>

#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "improvWifi.h"
#include "connect_wlan.h"
#include "configClass.h"
#include "ClassLogFile.h"
#include "system.h"
#include "server_ota.h"
#include "softAP.h"
#include "helper.h"


static const char *TAG = "IMPROV";


static TaskHandle_t improvTaskHandle = NULL;
static ImprovWiFi *improvWifi = NULL;

static const int readBufSize = (UART_HW_FIFO_LEN(DEFAULT_UART_NUM));
static const int uartBufferSize = 2 * readBufSize;
static QueueHandle_t uartQueueHandle;

extern std::string getFwVersion(void);


void uart_event_handler(void)
{
    uart_event_t event;
    uint8_t dtmp[readBufSize];
    size_t buffered_size;

    // Waiting for UART event
    if (xQueueReceive(uartQueueHandle, (void *)&event, (TickType_t)portMAX_DELAY)) {
        bzero(dtmp, readBufSize);
        // ESP_LOGI(TAG, "uart[%d] event:", DEFAULT_UART_NUM);
        switch (event.type) {
            // Event of UART receving data
            /*We'd better handler data event fast, there would be much more data
            events than other types of events. If we take too much time on data event,
            the queue might be full.*/
            case UART_DATA:
                // ESP_LOGI(TAG, "[UART DATA]: %d", event.size);

                uart_read_bytes(DEFAULT_UART_NUM, dtmp, event.size, portMAX_DELAY);
                //	ESP_LOGI(TAG, "[DATA EVT]:");

                improvWifi->handleSerial(dtmp, event.size);
                break;
            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                // ESP_LOGI(TAG, "hw fifo overflow");

                // If fifo overflow happened, you should consider adding flow control
                // for your application. The ISR has already reset the rx FIFO, As an
                // example, we directly flush the rx buffer here in order to read more
                // data.
                uart_flush_input(DEFAULT_UART_NUM);
                xQueueReset(uartQueueHandle);
                break;
            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
                // ESP_LOGI(TAG, "ring buffer full");
                // If buffer full happened, you should consider increasing your buffer
                // size As an example, we directly flush the rx buffer here in order to
                // read more data.
                uart_flush_input(DEFAULT_UART_NUM);
                xQueueReset(uartQueueHandle);
                break;
            // Others
            default:
                // ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
        }
    }
}


static void improvTask(void *pvParameters)
{
    while (1) {
        uart_event_handler();
    }
}


void improvUartWrite(const unsigned char *txData, int length)
{
    uart_write_bytes(DEFAULT_UART_NUM, txData, length);
}


void improvWifiScan(unsigned char *scanResponse, int bufLen, uint16_t *networkNum)
{
    esp_err_t retVal;

    // If already in AP mode, switch to station mode
    wifi_mode_t wifiMode;
    esp_wifi_get_mode(&wifiMode);

    if(wifiMode == WIFI_MODE_AP) {
        wifiDeinitAP();
        vTaskDelay(pdMS_TO_TICKS(100));

        initWifiStation();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    wifi_scan_config_t wifiScanConfig;
    memset(&wifiScanConfig, 0, sizeof(wifiScanConfig));

    wifiScanConfig.show_hidden = true; // Scan also hidden SSIDs
	wifiScanConfig.channel = 0; // Scan all channels

    // Start scan if in wrong state disconnect first and restart scan again
    if (esp_wifi_scan_start(&wifiScanConfig, true) == ESP_ERR_WIFI_STATE) {
        wifi_ap_record_t apInfoTmp;

        do {
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
        } while (esp_wifi_sta_get_ap_info(&apInfoTmp) != ESP_ERR_WIFI_NOT_CONNECT);

        esp_wifi_scan_start(&wifiScanConfig, true); // Scan in blocking mode
    }

    *networkNum = 10; // Max. number of APs, value will be updated by function "esp_wifi_scan_get_ap_num"
	retVal = esp_wifi_scan_get_ap_num(networkNum); // Get actual found APs
    if (retVal != ESP_OK) {
      	LogFile.writeToFile(ESP_LOG_ERROR, TAG, "improvWifiScan: esp_wifi_scan_get_ap_num: Error: " + intToHexString(retVal));
		return;
    }
    wifi_ap_record_t* apInfo = new wifi_ap_record_t[*networkNum]; // Allocate necessary record datasets
	if (apInfo == NULL) {
		esp_wifi_scan_get_ap_records(0, NULL); // Free internal heap
		LogFile.writeToFile(ESP_LOG_ERROR, TAG, "improvWifiScan: Failed to allocate heap for apInfo");
		return;
	}
	else {
        retVal = esp_wifi_scan_get_ap_records(networkNum, apInfo);
    	if (retVal != ESP_OK) { // Retrieve results (and free internal heap)
			LogFile.writeToFile(ESP_LOG_ERROR, TAG, "improvWifiScan: esp_wifi_scan_get_ap_records: Error: " + intToHexString(retVal));
			delete[] apInfo;
			return;
		}
	}

    // Build response string
    scanResponse[0] = 0;
    for (int i = 0; i < *networkNum; i++) {
        char rssiStr[8] = {0};
        char cipherStr[8] = {0};
        int neededLen;

        itoa(apInfo[i].rssi, rssiStr, 10);
        if (apInfo[i].authmode != WIFI_AUTH_OPEN) {
            strcat(cipherStr, "YES");
        }
        else {
            strcat(cipherStr, "NO");
        }
        neededLen = strlen((const char *)apInfo[i].ssid) + strlen(rssiStr) + strlen(cipherStr) + 3;

        if ((bufLen - neededLen) > 0) {
            strcat((char *)scanResponse, (char *)apInfo[i].ssid);
            strcat((char *)scanResponse, (char *)",");
            strcat((char *)scanResponse, (char *)rssiStr);
            strcat((char *)scanResponse, (char *)",");
            strcat((char *)scanResponse, (char *)cipherStr);
            strcat((char *)scanResponse, (char *)"\n");

            bufLen -= neededLen;
        }
    }
}


bool improvWifiConnect(const char *ssid, const char *password)
{
    // Set new configuration
    ConfigClass::getInstance()->cfgTmp()->sectionNetwork.wlan.ssid = std::string(ssid);
    ConfigClass::getInstance()->cfgTmp()->sectionNetwork.wlan.password = std::string(password);
    ConfigClass::getInstance()->saveMigDataToNVS("wlan_pw", ConfigClass::getInstance()->cfgTmp()->sectionNetwork.wlan.password);
    ConfigClass::getInstance()->persistConfig();
    ConfigClass::getInstance()->reinitConfig();

    // Get existing wifi config
    wifi_config_t wifiConfig;
    esp_wifi_get_config(WIFI_IF_STA, &wifiConfig);

    // Set new credentials
    strcpy((char*)wifiConfig.sta.ssid, (const char*)ConfigClass::getInstance()->get()->sectionNetwork.wlan.ssid.c_str());
    strcpy((char*)wifiConfig.sta.password, (const char*)ConfigClass::getInstance()->get()->sectionNetwork.wlan.password.c_str());
    esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);

    // Disconnect and reconnect
    wifi_ap_record_t apInfoTmp;
    do {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    } while (esp_wifi_sta_get_ap_info(&apInfoTmp) != ESP_ERR_WIFI_NOT_CONNECT);
    esp_wifi_connect();

    // Check connection state
    int count = 0;
    while (!getWIFIisConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (count > 30) { // Timeout 30s
            return false;
        }
        count++;
    }

    return true;
}


void improvInit(void)
{
    improvWifi = new ImprovWiFi();
    improvWifi->serialWrite(improvUartWrite);

    ImprovTypes::ChipFamily chipFamily;
    if (getChipModel() == "ESP32") {
        chipFamily = ImprovTypes::CF_ESP32;
    }
    else if (getChipModel() == "ESP32S3") {
        chipFamily = ImprovTypes::CF_ESP32_S3;
    }
    else {
        chipFamily = ImprovTypes::CF_ESP32;
    }
    improvWifi->setDeviceInfo(chipFamily, "Firmware:", getFwVersion().c_str(), "AI-on-the-Edge Device");

    improvWifi->setCustomConnectWiFi(improvWifiConnect);
    improvWifi->setCustomScanWiFi(improvWifiScan);
    improvWifi->setCustomisConnected(getWIFIisConnected);
    improvWifi->setCustomGetLocalIpCallback(getIPAddress);

    // Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
    // uart_set_pin(DEFAULT_UART_NUM, 1, 3, -1, -1);

    // Install UART driver using an event queue here
    uart_driver_install(DEFAULT_UART_NUM, uartBufferSize, uartBufferSize, 10, &uartQueueHandle, 0);

    xTaskCreatePinnedToCore(&improvTask, "improv", 4 * 1024, NULL, 4, &improvTaskHandle, tskNO_AFFINITY);
}


void improvDeinit(void)
{
    if (improvTaskHandle) {
        uart_driver_delete(DEFAULT_UART_NUM);
        vTaskDelete(improvTaskHandle);
        improvTaskHandle = NULL;
    }

    if (improvWifi != NULL) {
        delete improvWifi;
        improvWifi = NULL;
    }
}
