#include "time_sntp.h"
#include "../../include/defines.h"

#include <string>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_sntp.h"

#include "ClassLogFile.h"
#include "configFile.h"
#include "Helper.h"


static const char *TAG = "SNTP";

static std::string timeZone = "";
static std::string timeServer = "";
static bool useNtp = true;
static bool timeWasNotSetAtBoot = false;
static bool timeWasNotSetAtBoot_PrintStartBlock = false;

std::string getNtpStatusText(sntp_sync_status_t status);
static void setTimeZone(std::string _tzstring);
static std::string getServerName(void);


std::string ConvertTimeToString(time_t _time, const char * frm)
{
    struct tm timeinfo;
    char strftime_buf[64];

    localtime_r(&_time, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), frm, &timeinfo);
    return std::string(strftime_buf);
}


std::string getCurrentTimeString(const char * frm)
{
    time_t now;

    time(&now);
    return ConvertTimeToString(now, frm);
}


void time_sync_notification_cb(struct timeval *tv)
{
    if (timeWasNotSetAtBoot_PrintStartBlock) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "=================================================");
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "==================== Start ======================");
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "== Logs before time sync -> log_1970-01-01.txt ==");
        timeWasNotSetAtBoot_PrintStartBlock = false;
    }
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Time is synced with NTP server " +
            getServerName() + ": " + getCurrentTimeString("%Y-%m-%d %H:%M:%S"));
}


bool time_manual_reset_sync(void)
{
    sntp_restart();
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Waiting for time sync - " + std::to_string(retry) + "/" + std::to_string(retry_count));
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if (retry >= retry_count)
        return false;

    return true;
}


void setTimeZone(std::string _tzstring)
{
    setenv("TZ", _tzstring.c_str(), 1);
    tzset();    
    _tzstring = "Time zone set to " + _tzstring;
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, _tzstring);
}


std::string getNtpStatusText(sntp_sync_status_t status)
{
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        return "Synchronized";
    }
    else if (status == SNTP_SYNC_STATUS_IN_PROGRESS) {
        return "In Progress";
    }
    else { // SNTP_SYNC_STATUS_RESET
        return "Reset";
    }
}


bool getTimeIsSet(void)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if ((timeinfo.tm_year < (2022 - 1900))) {
        return false;
    }
    else {
        return true;
    }
}


bool getUseNtp(void)
{
    return useNtp;
}

bool getTimeWasNotSetAtBoot(void)
{
    return timeWasNotSetAtBoot;
}


std::string getServerName(void)
{
    char buf[100];

    if (sntp_getservername(0)){
        snprintf(buf, sizeof(buf), "%s", sntp_getservername(0));
        return std::string(buf);
    }
    else { // we have either IPv4 or IPv6 address
        ip_addr_t const *ip = sntp_getserver(0);
        if (ipaddr_ntoa_r(ip, buf, sizeof(buf)) != NULL) {
            return std::string(buf);
        }
    }
    return "";
}


/**
 * Load the TimeZone and TimeServer from the config file and initialize the NTP client
 */
bool setupTime()
{
    ConfigFile configFile = ConfigFile(CONFIG_FILE); 

    if (!configFile.ConfigFileExists()){
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "No config file - exit setupTime()");
        return false;
    }

    std::vector<std::string> splitted;
    std::string line = "";
    bool disabledLine = false;
    bool eof = false;

    /* Load config from config file */
    while ((!configFile.GetNextParagraph(line, disabledLine, eof) || 
            (line.compare("[System]") != 0)) && !eof) {}
    if (eof) {
        timeServer = "pool.ntp.org";
        timeZone = "CET-1CEST,M3.5.0,M10.5.0/3";
        return false;
    }

    if (disabledLine) {
        timeServer = "pool.ntp.org";
        timeZone = "CET-1CEST,M3.5.0,M10.5.0/3";
        return false;
    }

    while (configFile.getNextLine(&line, disabledLine, eof) && 
            !configFile.isNewParagraph(line))
    {
        splitted = ZerlegeZeile(line, "=");

        if (toUpper(splitted[0]) == "TIMEZONE") {
            if (splitted.size() <= 1) { // parameter part is empty, use default time zone
                timeZone = "CET-1CEST,M3.5.0,M10.5.0/3";
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeZone not set, using default: " + timeZone);
            }
            else {
                timeZone = splitted[1];
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeZone: " + timeZone);
            }
        }

        if (toUpper(splitted[0]) == "TIMESERVER") {
            if (splitted.size() <= 1) { // parameter part is empty, use default time server
                timeServer = "pool.ntp.org";
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "TimeServer not set, use default: " + timeServer);
            }
            else {
                timeServer = splitted[1];
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeServer: " + timeServer);
                useNtp = true;
            }
        }
    }

    // Timeserver disabled
    if (timeServer == "") {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeServer deactivated, disabling NTP");
        useNtp = false;
    }

    // Set timezone in any case, even no time source is selected.
    setTimeZone(timeZone);
    
    if (useNtp) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Configure NTP client");        
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, timeServer.c_str());
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        sntp_init();
    }

    /* The RTC keeps the time after a restart (Except on Power On or Pin Reset) 
     * There should only be a minor correction through NTP */

    // Get current time from RTC
    time_t now;
    time(&now);
    std::string sTimeString = ConvertTimeToString(now, "%Y-%m-%d %H:%M:%S");

    if (getTimeIsSet()) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Time is already set: " + sTimeString);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "The local time is unknown, starting with " + sTimeString);
        timeWasNotSetAtBoot = true;
        timeWasNotSetAtBoot_PrintStartBlock = true;
        
        if (useNtp)
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Once the NTP server provides a time, we will switch to that one");
    }

    return true;
}


/**
 * Update TimeZone
 */
void setupTimeZone(std::string _timeZone)
{
    if (timeZone.compare(_timeZone) == 0)
        return;

    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "TimeZone gets adjusted");
    
    timeZone = _timeZone;

    if (_timeZone == "") {
        _timeZone = "CET-1CEST,M3.5.0,M10.5.0/3";
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeZone not set, using default: " + _timeZone);
    }

    setTimeZone(_timeZone);
}


/**
 * TimeServer and init or restart NTP client
 */
void setupTimeServer(std::string _timeServer)
{
    if (timeServer.compare(_timeServer) == 0)
        return;

    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "TimeServer gets adjusted");

    if (_timeServer == "") {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeServer deactivated, disabling NTP");
        useNtp = false;
        sntp_stop();
    }
    else {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "TimeServer: " + _timeServer);
        useNtp = true;
    }

    timeServer = _timeServer;

    if (useNtp) {    
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, timeServer.c_str());
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);

        if (!sntp_enabled()) {
            setTimeZone(timeZone);
            sntp_init();
        }
        else {
            sntp_restart();
        }

        if (!time_manual_reset_sync())
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Time sync failed. Continue anyway");
    }
}
