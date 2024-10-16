#include "server_file.h"
#include "../../include/defines.h"

#include <stdio.h>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <dirent.h>
#ifdef __cplusplus
}
#endif

#include <esp_partition.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <esp_http_server.h>
#include <cJSON.h>

#include "webserver.h"
#include "server_help.h"
#include "ClassLogFile.h"
#include "MainFlowControl.h"
#include "gpioControl.h"
#include "helper.h"
#include "system.h"
#include "psram.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#endif //ENABLE_MQTT


static const char *TAG = "SERVER_FILE";


esp_err_t getDataFileList(httpd_req_t *req)
{
    esp_err_t retVal = ESP_FAIL;
    const char verz_name[] = "/sdcard/log/data";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);
    if (!dir) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "getDataFileList: Failed to open directory: " + std::string(verz_name));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to open directory");
        return ESP_FAIL;
    }

    cJSON *cJSONObject = cJSON_CreateObject();
    if (cJSONObject == NULL) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    cJSON *files;
    if (!cJSON_AddItemToObject(cJSONObject, "files", files = cJSON_CreateArray())) {
        cJSON_Delete(cJSONObject);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') { // ignore all files with starting dot (hidden files)
            continue;
        }

        if (getFileIsFiletype(std::string(entry->d_name), "csv")) {
            if (!cJSON_AddItemToArray(files, cJSON_CreateString(entry->d_name))) {
                retVal = ESP_FAIL;
                break;
            }
        }
    }
    closedir(dir);

    char *jsonChar = cJSON_Print(cJSONObject);
    cJSON_Delete(cJSONObject);

    if (jsonChar != NULL) {
        retVal = httpd_resp_send(req, jsonChar, strlen(jsonChar));
        cJSON_free(jsonChar);
    }

    return retVal;
}


esp_err_t getTfliteFileList(httpd_req_t *req)
{
    esp_err_t retVal = ESP_FAIL;
    const char verz_name[] = "/sdcard/config/models";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);
    if (!dir) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "getTfliteFileList: Failed to open directory: " + std::string(verz_name));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to open directory");
        return ESP_FAIL;
    }

    cJSON *cJSONObject = cJSON_CreateObject();
    if (cJSONObject == NULL) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    cJSON *files;
    if (!cJSON_AddItemToObject(cJSONObject, "files", files = cJSON_CreateArray())) {
        cJSON_Delete(cJSONObject);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') { // ignore all files with starting dot (hidden files)
            continue;
        }

        if (getFileIsFiletype(std::string(entry->d_name), "tfl") || getFileIsFiletype(std::string(entry->d_name), "tflite")) {
            if (!cJSON_AddItemToArray(files, cJSON_CreateString(entry->d_name))) {
                retVal = ESP_FAIL;
                break;
            }
        }
    }
    closedir(dir);

    char *jsonChar = cJSON_Print(cJSONObject);
    cJSON_Delete(cJSONObject);

    if (jsonChar != NULL) {
        retVal = httpd_resp_send(req, jsonChar, strlen(jsonChar));
        cJSON_free(jsonChar);
    }

    return retVal;
}


esp_err_t getCertFileList(httpd_req_t *req)
{
    esp_err_t retVal = ESP_FAIL;
    const char verz_name[] = "/sdcard/config/certs";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);
    if (!dir) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "getCertFileList: Failed to open directory: " + std::string(verz_name));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to open directory");
        return ESP_FAIL;
    }

    cJSON *cJSONObject = cJSON_CreateObject();
    if (cJSONObject == NULL) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    cJSON *files;
    if (!cJSON_AddItemToObject(cJSONObject, "files", files = cJSON_CreateArray())) {
        cJSON_Delete(cJSONObject);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') { // ignore all files with starting dot (hidden files)
            continue;
        }

        if (!cJSON_AddItemToArray(files, cJSON_CreateString(entry->d_name))) {
            retVal = ESP_FAIL;
        }
    }
    closedir(dir);

    char *jsonChar = cJSON_Print(cJSONObject);
    cJSON_Delete(cJSONObject);

    if (jsonChar != NULL) {
        retVal = httpd_resp_send(req, jsonChar, strlen(jsonChar));
        cJSON_free(jsonChar);
    }

    return retVal;
}


esp_err_t sendFile(httpd_req_t *req, std::string filename)
{
    FILE *fd = fopen(filename.c_str(), "r");
    if (fd == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fd, NULL, _IOFBF, 512);

    // ESP_LOGI(TAG, "Sending file: %s", filename.c_str());
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // For all files with the following file extention tell the webbrowser to cache them for a long period
    if (endsWith(filename, ".html") ||
        endsWith(filename, ".htm") ||
        endsWith(filename, ".css") ||
        endsWith(filename, ".js") ||
        endsWith(filename, ".map") ||
        endsWith(filename, ".jpg") ||
        endsWith(filename, ".jpeg") ||
        endsWith(filename, ".ico") ||
        endsWith(filename, ".gif") ||
        endsWith(filename, ".svg") ||
        endsWith(filename, ".png") ||
        endsWith(filename, ".md") ||
        endsWith(filename, ".webmanifest") ||
        endsWith(filename, ".txt"))
    {
        if (filename == "/sdcard/html/index.html") {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=0");
        }
        else if (filename == "/sdcard/html/setup.html") {
            httpd_resp_set_hdr(req, "Clear-Site-Data", "\"*\"");
        }
        else {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=31536000");
        }
    }

    setContentTypeFromFile(req, filename.c_str());

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = (char *) ((struct HttpServerData *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, WEBSERVER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            std::string msg_txt = "sendFile: Failed to send file: " + filename;
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, msg_txt);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg_txt.c_str());
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t sendLogfile(httpd_req_t *req, bool send_full_file)
{
    FILE *fd = NULL;
    std::string currentfilename = LogFile.getCurrentFileName();

    //ESP_LOGD(TAG, "uri: %s, filepath: %s", req->uri, currentfilename.c_str());

    // !!! Do not close actual logfile to avoid software exception !!!
    //LogFile.closeLogFileAppendHandle();

    fd = fopen(currentfilename.c_str(), "r");
    if (fd == NULL) {
        // LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendLogfile: Failed to read file: " + currentfilename); // It's not a fault if no file is available
        httpd_resp_send(req, "No recent log entries", HTTPD_RESP_USE_STRLEN); // Respond with a positive feedback, no logs available from today
        return ESP_OK;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fd, NULL, _IOFBF, 512);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/plain");

    if (!send_full_file) { // Send only last part of file
        ESP_LOGD(TAG, "Sending last %d bytes of the actual logfile", LOGFILE_LAST_PART_BYTES);
        long pos = 0;

        /* Adapted from https://www.geeksforgeeks.org/implement-your-own-tail-read-last-n-lines-of-a-huge-file/ */
        if (fseek(fd, 0, SEEK_END)) {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendLogfile: Failed to get to end of file");
            return ESP_FAIL;
        }
        else {
            pos = ftell(fd); // Number of bytes in the file
            ESP_LOGD(TAG, "File contains %ld bytes", pos);

            // Calc start position -> either beginning of LAST PART (EOF - LAST_PART_BYTES) or beginning of file (pos = 0)
            pos = pos - std::min((long)LOGFILE_LAST_PART_BYTES, pos);

            if (fseek(fd, pos, SEEK_SET)) { // Go to start position
                LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendLogfile: Failed to go back " +
                                    std::to_string(std::min((long)LOGFILE_LAST_PART_BYTES, pos)) + " bytes within the file");
                return ESP_FAIL;
            }
        }

        /* Find end of line */
        while (pos > 0) { // Only search end of line if pos is pointing to "beginning of LAST PART"
                          // (skip if start is from beginning of file to ensure first line is included)
            if (fgetc(fd) == '\n') {
                break;
            }
        }
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct HttpServerData *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, WEBSERVER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            std::string msg_txt = "sendLogfile: File sending failed: " + currentfilename;
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, msg_txt);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg_txt.c_str());
            return ESP_FAIL;
        }

    /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t handler_logfiles(httpd_req_t *req)
{
    const char* APIName = "log:v2"; // API name and version
    char query[100];
    char valuechar[30];
    std::string type;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "type", valuechar, sizeof(valuechar)) == ESP_OK) {
            type = std::string(valuechar);
        }
    }

    if (type.empty()) {
        return sendLogfile(req, false);
    }
    else if (type.compare("full") == 0) {
        return sendLogfile(req, true);
    }
    else if (type.compare("api_name") == 0) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }
    else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "E91: Parameter not found");
        return ESP_FAIL;
    }
}


static esp_err_t sendDatafile(httpd_req_t *req, bool send_full_file)
{
    FILE *fd = NULL;
    std::string currentfilename = LogFile.getCurrentFileNameData();

    //ESP_LOGD(TAG, "uri: %s, filepath: %s", req->uri, currentfilename.c_str());

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/plain");

    fd = fopen(currentfilename.c_str(), "r");
    if (fd == NULL) {
        //LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendDatafile: Failed to read file: " + currentfilename); // It's not a fault if no file is available
        httpd_resp_send(req, "No recent data entries", HTTPD_RESP_USE_STRLEN); // Respond with a positive feedback, no data available from today
        return ESP_OK;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fd, NULL, _IOFBF, 512);

    if (!send_full_file) { // Send only last part of file
        ESP_LOGD(TAG, "Sending last %d bytes of the actual datafile", LOGFILE_LAST_PART_BYTES);
        long pos = 0;

        /* Adapted from https://www.geeksforgeeks.org/implement-your-own-tail-read-last-n-lines-of-a-huge-file/ */
        if (fseek(fd, 0, SEEK_END)) {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendDatafile: Failed to get to end of file");
            return ESP_FAIL;
        }
        else {
            pos = ftell(fd); // Number of bytes in the file
            ESP_LOGD(TAG, "File contains %ld bytes", pos);

            // Calc start position -> either beginning of LAST PART (EOF - LAST_PART_BYTES) or beginning of file (pos = 0)
            pos = pos - std::min((long)LOGFILE_LAST_PART_BYTES, pos);

            if (fseek(fd, pos, SEEK_SET)) { // Go to start position
                LogFile.writeToFile(ESP_LOG_ERROR, TAG, "sendDatafile: Failed to go back " +
                                    std::to_string(std::min((long)LOGFILE_LAST_PART_BYTES, pos)) + " bytes within the file");
                return ESP_FAIL;
            }
        }

        /* Find end of line */
        while (pos > 0) { // Only search end of line if pos is pointing to "beginning of LAST PART"
                          // (skip if start is from beginning of file to ensure first line is included)
            if (fgetc(fd) == '\n') {
                break;
            }
        }
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct HttpServerData *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, WEBSERVER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            std::string msg_txt = "sendDatafile: File sending failed: " + currentfilename;
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, msg_txt);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg_txt.c_str());
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t handler_datafiles(httpd_req_t *req)
{
    const char* APIName = "data:v2"; // API name and version
    char query[100];
    char valuechar[30];
    std::string type;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "type", valuechar, sizeof(valuechar)) == ESP_OK) {
            type = std::string(valuechar);
        }
    }

    if (type.empty()) {
        return sendDatafile(req, false);
    }
    else if (type.compare("full") == 0) {
        return sendDatafile(req, true);
    }
    else if (type.compare("api_name") == 0) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }
    else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "E91: Parameter not found");
        return ESP_FAIL;
    }
}


// Response with a run-time generated html consisting all files and folders of requested path
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath, const char* uripath, bool readonly)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    char dirpath_corrected[FILE_PATH_MAX];
    strcpy(dirpath_corrected, dirpath);

    HttpServerData * server_data = (HttpServerData *) req->user_ctx;
    if ((strlen(dirpath_corrected)-1) > strlen(server_data->basePathFileserver))      // if dirpath is not mountpoint, the last "\" needs to be removed
        dirpath_corrected[strlen(dirpath_corrected)-1] = '\0';

    DIR *dir = opendir(dirpath_corrected);

    const size_t dirpath_len = strlen(dirpath);
    ESP_LOGD(TAG, "Dirpath: <%s>, Pathlength: %d", dirpath, dirpath_len);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    ESP_LOGD(TAG, "entrypath: <%s>", entrypath);

    if (!dir) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "http_resp_dir_html: Failed to open directory: " + std::string(dirpath));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to open directory");
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    /* Send HTML file header */
    //httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>"); --> This is already part of 'sys_fileserver.html' file

    /////////////////////////////////////////////////
    if (!readonly) {
        FILE *fd = fopen("/sdcard/html/sys_fileserver.html", "r");

        /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
        // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
        setvbuf(fd, NULL, _IOFBF, 512);

        char *chunk = ((struct HttpServerData *)req->user_ctx)->scratch;
        size_t chunksize;
        do {
            chunksize = fread(chunk, 1, WEBSERVER_SCRATCH_BUFSIZE, fd);
            //ESP_LOGD(TAG, "Chunksize %d", chunksize);
            if (chunksize > 0){
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                    fclose(fd);
                    std::string msg_txt = "http_resp_dir_html: File sending failed: /sdcard/html/sys_fileserver.html";
                    LogFile.writeToFile(ESP_LOG_DEBUG, TAG, msg_txt);
                    httpd_resp_sendstr_chunk(req, NULL);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg_txt.c_str());
                    return ESP_FAIL;
                }
            }
        } while (chunksize != 0);
        fclose(fd);
        //ESP_LOGD(TAG, "File sending complete");
    }
    ///////////////////////////////

    std::string _zw = std::string(dirpath);
    _zw = _zw.substr(8, _zw.length() - 8);
    _zw = "/delete/" + _zw + "?task=deldircontent";


    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
        "<table id=\"files_table\">"
        "<col style=\"width:800px\"><col style=\"width:300px\"><col style=\"width:300px\"><col style=\"width:100px\">"
        "<thead><tr><th>Name</th><th>Type</th><th>Size</th>");
    if (!readonly) {
        httpd_resp_sendstr_chunk(req, "<th>"
            "<form method=\"post\" action=\"");
        httpd_resp_sendstr_chunk(req, _zw.c_str());
        httpd_resp_sendstr_chunk(req,
            "\"><button type=\"submit\">DELETE ALL!</button></form>"
            "</th></tr>");
    }
    httpd_resp_sendstr_chunk(req, "</thead><tbody>\n");

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        // Don't show these files, because passwords included
        if (strcmp("wlan.ini", entry->d_name) == 0 || strcmp("wlan_ini.bak", entry->d_name) == 0 ||
            strcmp("config.ini", entry->d_name) == 0 || strcmp("config_ini.bak", entry->d_name) == 0) {
            continue;
        }

        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        ESP_LOGD(TAG, "Entrypath: %s", entrypath);
        if (stat(entrypath, &entry_stat) == -1) {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "http_resp_dir_html: Failed to read " +
                                std::string(entrytype) + ": " + std::string(entry->d_name));
            continue;
        }

        if (entry->d_type == DT_DIR) {
            strcpy(entrysize, "-\0");
        }
        else {
            if (entry_stat.st_size >= 1024) {
                sprintf(entrysize, "%ld KB", entry_stat.st_size / 1024); // kBytes
            }
            else {
                sprintf(entrysize, "%ld B", entry_stat.st_size); // Bytes
            }
        }

        ESP_LOGD(TAG, "Found %s: %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
        httpd_resp_sendstr_chunk(req, "/fileserver");
        httpd_resp_sendstr_chunk(req, uripath);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td><td>");
        httpd_resp_sendstr_chunk(req, entrytype);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        if (!readonly) {
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
            httpd_resp_sendstr_chunk(req, uripath);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
        }
        httpd_resp_sendstr_chunk(req, "</td></tr>\n");
    }
    closedir(dir);

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}


// Handler to download a file from server (sd card)
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = getPathFromUri(filepath, ((struct HttpServerData *)req->user_ctx)->basePathFileserver,
                                             req->uri  + sizeof("/fileserver") - 1, sizeof(filepath));

    if (!filename) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "download_get_handler: Filename is too long");
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "download_get_handler: Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        bool readonly = false;
        size_t buf_len = httpd_req_get_url_query_len(req) + 1;
        if (buf_len > 1) {
            char buf[buf_len];
            if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
                ESP_LOGD(TAG, "Found URL query => %s", buf);
                char param[32];
                /* Get value of expected key from query string */
                if (httpd_query_key_value(buf, "readonly", param, sizeof(param)) == ESP_OK) {
                    ESP_LOGD(TAG, "Found URL query parameter => readonly=%s", param);
                    readonly = (strcmp(param, "true") == 0);
                }
            }
        }

        ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);
        return http_resp_dir_html(req, filepath, filename, readonly);
    }

    ESP_LOGD(TAG, "download_get_handler: Filename: %s | Filepath: %s", filename, filepath);

    // Reject unavailable files
    if (stat(filepath, &file_stat) == -1) {
        // Special case: Do not log special config files in log to avoid confusion because they are missing before finalizing initial setup
        if (strcmp("/sdcard/config/reference.jpg", filepath) != 0 &&
            strcmp("/sdcard/config/marker1.jpg", filepath) != 0 &&
            strcmp("/sdcard/config/marker2.jpg", filepath) != 0)
        {
            LogFile.writeToFile(ESP_LOG_WARN, TAG, "download_get_handler: File not found: " + std::string(filepath));
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    //  Reject special files with sensitive data
    if (strcmp("/sdcard/wlan.ini", filepath) == 0 || strcmp("/sdcard/config/backup/wlan_ini.bak", filepath) == 0 ||
        strcmp("/sdcard/config/config.ini", filepath) == 0 || strcmp("/sdcard/config/backup/config_ini.bak", filepath) == 0)
    {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "File request forbidden (sensitive data)");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "download_get_handler: Failed to read file: " + std::string(filepath));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to read file");
        return ESP_FAIL;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fd, NULL, _IOFBF, 512);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    ESP_LOGD(TAG, "Sending file: %s (%ld bytes)", filename, file_stat.st_size);
    setContentTypeFromFile(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct HttpServerData *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, WEBSERVER_SCRATCH_BUFSIZE, fd);

        /* Send buffer contents as HTTP chunk. If empty this functions as a
         * last-chunk message, signaling end-of-response, to the HTTP client.
         * See RFC 2616, section 3.6.1 for details on Chunked Transfer Encoding. */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            std::string msg_txt = "download_get_handler: File sending failed: " + std::string(filepath);
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, msg_txt);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg_txt.c_str());
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File successfully sent");

    return ESP_OK;
}


// Handler to upload a file to server (sd card)
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = getPathFromUri(filepath, ((struct HttpServerData *)req->user_ctx)->basePathFileserver,
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "upload_post_handler: Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: Invalid filename: " + std::string(filename));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload_post_handler: Invalid filename");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: File too large: " + std::to_string(req->content_len) + " bytes");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload_post_handler: File size must be less than " MAX_FILE_SIZE_STR);
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: File already exists: " + std::string(filepath));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload_post_handler: File already exists");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "w");
    if (!fd) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: Failed to create file: " + std::string(filepath));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload_post_handler: Failed to create file");
        return ESP_FAIL;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fd, NULL, _IOFBF, 512);

    ESP_LOGI(TAG, "Receiving file: %s", filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *buf = ((struct HttpServerData *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size: %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, WEBSERVER_SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // Retry if timeout occurred
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: File reception failed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload_post_handler: Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "upload_post_handler: File write failed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload_post_handler: Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "upload_post_handler: File saved: " + std::string(filename));
    ESP_LOGI(TAG, "File reception completed");


    std::string directory = std::string(filepath);
	size_t zw = directory.find("/");
	size_t found = zw;
	while (zw != std::string::npos) {
		zw = directory.find("/", found+1);
		if (zw != std::string::npos)
			found = zw;
	}

    int start_fn = strlen(((struct HttpServerData *)req->user_ctx)->basePathFileserver);
    ESP_LOGD(TAG, "Directory: %s, start_fn: %d, found: %d", directory.c_str(), start_fn, found);
	directory = directory.substr(start_fn, found - start_fn + 1);
    directory = "/fileserver" + directory;
    //    ESP_LOGD(TAG, "Directory danach 2: %s", directory.c_str());

    /* Redirect onto root to see the updated file list */
    if (strcmp(filename, "/config/marker1.jpg") == 0 ||
        strcmp(filename, "/config/marker2.jpg") == 0 ||
        strcmp(filename, "/config/reference.jpg") == 0 ||
        strcmp(filename, "/img_tmp/marker1.jpg") == 0 ||
        strcmp(filename, "/img_tmp/marker2.jpg") == 0 ||
        strcmp(filename, "/img_tmp/reference.jpg") == 0 )
    {
        httpd_resp_set_status(req, HTTPD_200); // Response without redirection request -> Avoid reloading of folder content
    }
    else {
        httpd_resp_set_status(req, "303 See Other"); // Respond with redirection request
    }

    httpd_resp_set_hdr(req, "Location", directory.c_str()); // If 303 -> Redirect onto root to see the updated file list (only for fileserver action)
    httpd_resp_sendstr(req, "File uploaded successfully");

    return ESP_OK;
}


// Handler to delete a file or folder content from server (sd card)
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    char query[200];
    char valuechar[30];
    std::string task;
    std::string directory;
    std::string zw;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "task", valuechar, sizeof(valuechar)) == ESP_OK) {
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "delete_post_handler: Task: " + std::string(valuechar));
            task = std::string(valuechar);
        }
    }

    if (task.compare("deldircontent") == 0) {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = getPathFromUri(filepath, ((struct HttpServerData *)req->user_ctx)->basePathFileserver,
                                                req->uri  + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename) {
            httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "delete_post_handler: Filename too long");
            return ESP_FAIL;
        }
        zw = std::string(filename);
        zw = zw.substr(0, zw.length()-1);
        directory = "/fileserver" + zw + "/";
        zw = "/sdcard" + zw;
        ESP_LOGD(TAG, "Directory to delete: %s", zw.c_str());

        deleteAllFilesInDirectory(zw);
        ESP_LOGD(TAG, "Location after delete directory content: %s", directory.c_str());
    }
    else {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = getPathFromUri(filepath, ((struct HttpServerData *)req->user_ctx)->basePathFileserver,
                                                req->uri  + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "delete_post_handler: Filename too long");
            return ESP_FAIL;
        }

        /* Filename cannot have a trailing '/' */
        if (filename[strlen(filename) - 1] == '/') {
            LogFile.writeToFile(ESP_LOG_ERROR, TAG, "delete_post_handler: Invalid filename: " + std::string(filename));
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "delete_post_handler: Invalid filename");
            return ESP_FAIL;
        }

        if (stat(filepath, &file_stat) == -1) { // File does not exist
            /* This is ok, we would delete it anyway */
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "delete_post_handler: Deletion triggered, but file not existing: " + std::string(filename));
        }
        else {
            LogFile.writeToFile(ESP_LOG_DEBUG, TAG, "delete_post_handler: Deleting file: " + std::string(filename));
            /* Delete file */
            unlink(filepath);
        }

        /* Avoid redirect to root folder after deletion for system processed files */
        if (strcmp(filename, "/config/marker1.jpg") == 0 ||
            strcmp(filename, "/config/marker2.jpg") == 0 ||
            strcmp(filename, "/config/reference.jpg") == 0 ||
            strcmp(filename, "/img_tmp/marker1.jpg") == 0 ||
            strcmp(filename, "/img_tmp/marker2.jpg") == 0 ||
            strcmp(filename, "/img_tmp/reference.jpg") == 0 )
        {
            httpd_resp_set_status(req, HTTPD_200); // Response without redirection request -> Avoid reloading of folder content
        }
        else {
            httpd_resp_set_status(req, "303 See Other"); // Respond with redirection request
        }

        char *pos = strrchr(filename, '/');
        *pos = '\0'; // Cut off filename -> From here filename is not valid anymore
        directory = std::string(filename);
        directory = "/fileserver" + directory + "/";
        //ESP_LOGI(TAG, "DELETE HANDLER2: Directory: %s", directory.c_str());
    }

    httpd_resp_set_hdr(req, "Location", directory.c_str()); // If 303 -> Redirect onto root to see the updated file list (only for fileserver action)
    httpd_resp_sendstr(req, "File successfully deleted");
    return ESP_OK;
}


static std::string printCoreDumpBacktraceInfo(const esp_core_dump_summary_t *summary)
{
    if (summary == NULL) {
        return "No core dump available";
    }

    char results[256]; // Assuming a maximum of 256 characters for the backtrace string
    int offset = 0;

    for (int i = 0; i < summary->exc_bt_info.depth; i++) {
        uintptr_t pc = summary->exc_bt_info.bt[i]; // Program Counter (PC)
        int len = snprintf(results + offset, sizeof(results) - offset, " 0x%08X", pc);
        if (len >= 0 && offset + len < sizeof(results)) {
            offset += len;
        }
        else {
            break; // Reached the limit of the results buffer
        }
    }

    return std::string("Backtrace: " + std::string(results) +
            "\nDepth: " + std::to_string((int)summary->exc_bt_info.depth) +
            "\nCorrupted: " + std::to_string(summary->exc_bt_info.corrupted) +
            "\nPC: " + std::to_string((int)summary->exc_pc) +
            "\nFirmware version: " + getFwVersion());
}


static esp_err_t coredump_handler(httpd_req_t *req)
{
    const char* APIName = "coredump:v1"; // API name and version
    char query[200];
    char valuechar[30];
    std::string task;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "task", valuechar, sizeof(valuechar)) == ESP_OK) {
            task = std::string(valuechar);
        }
    }

    // Check if coredump partition is available
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");
    if (partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Partition 'coredump' not found");
        return ESP_FAIL;
    }

    // Get core dump summary to check if core dump is available
    esp_core_dump_summary_t summary;
    esp_err_t coreDumpGetSummaryRetVal = esp_core_dump_get_summary(&summary);

    // Save core dump file
    // Debug with esp-coredump (https://github.com/espressif/esp-coredump) --> e.g. install with "pip install esp-coredump"
    // Generic: esp-coredump info_corefile --gdb <path_to_gdb_bin> --rom-elf <soc_specific_rom_elf_file> --core-format raw
    //  --core <downloaded coredump file (FIRMWARE_BOARDTYPE-coredump-elf.bin)> firmware.elf (firmware debug zip --> firmware.elf)
    // Example: esp-coredump info_corefile --gdb <tool-xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb.exe>
    //  --rom-elf esp32_rev0_rom.elf --core-format raw --core firmware_ESP32CAM_coredump-elf.bin firmware.elf
    if (task.compare("save") == 0) {
        if (coreDumpGetSummaryRetVal != ESP_OK) { // Skip save request if no core dump is available (empty partition)
            httpd_resp_sendstr(req, "Skip request, no core dump available");
            return ESP_OK;
        }

        // Get firmware and cleanup name to have proper filename
        std::string firmware = getFwVersion();
        replaceAll(firmware, ":", "_");
        replaceAll(firmware, " ", "_");
        replaceAll(firmware, "(", "_");
        replaceAll(firmware, ")", "_");

        std::string attachmentFile = "attachment;filename=" + firmware + "_" + getBoardType() + "_coredump-elf.bin";
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", attachmentFile.c_str());

        /* Retrieve the pointer to scratch buffer for temporary storage */
        char *buf = ((struct HttpServerData *)req->user_ctx)->scratch;
        if (buf == NULL) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No scratch buffer available");
            return ESP_FAIL;
        }

        int i = 0;
        for (i = 0; i < (partition->size / WEBSERVER_SCRATCH_BUFSIZE); i++) {
            esp_partition_read(partition, i * WEBSERVER_SCRATCH_BUFSIZE, buf, WEBSERVER_SCRATCH_BUFSIZE);
            httpd_resp_send_chunk(req, buf, WEBSERVER_SCRATCH_BUFSIZE);
        }

        int pendingSize = partition->size - (i * WEBSERVER_SCRATCH_BUFSIZE);
        if (pendingSize > 0) {
            ESP_ERROR_CHECK(esp_partition_read(partition, i * WEBSERVER_SCRATCH_BUFSIZE, buf, pendingSize));
            httpd_resp_send_chunk(req, buf, pendingSize);
        }
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    else if (task.compare("clear") == 0) { // Format partition 'coredump'
        esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
        if (err == ESP_OK) {
            httpd_resp_sendstr(req, "Partition 'coredump' cleared");
            return ESP_OK;
        }
        else {
            std::string errMsg = "Failed to format partition 'coredump'. Error: " + intToHexString(err);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errMsg.c_str());
            return ESP_FAIL;
        }
    }
    else if (task.compare("force_exception") == 0) { // Only for testing purpose, and ESP exception crash can be forced
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "coredump_handler: Software exception triggered manually");
        httpd_resp_send_chunk(req, NULL, 0);
        assert(0);
        return ESP_OK;
    }
    else if (task.compare("api_name") == 0) {
        httpd_resp_sendstr(req, APIName);
        return ESP_OK;
    }

    // Default action: Print backtrace summary
    if (coreDumpGetSummaryRetVal == ESP_OK) {
        httpd_resp_sendstr(req, printCoreDumpBacktraceInfo(&summary).c_str());
    }
    else {
        httpd_resp_sendstr(req, "No core dump available");
    }

    return ESP_OK;
}


void registerFileserverUri(httpd_handle_t server, const char *basePath)
{
    ESP_LOGI(TAG, "Registering URI handlers");

    /* Validate file storage base path */
    if (!basePath) {
        //if (!basePath || strcmp(basePath, "/spiffs") != 0) {
        LogFile.writeToFile(ESP_LOG_ERROR, TAG, "basePath for fileserver root folder not set");
        //return ESP_ERR_INVALID_ARG;
    }

    strlcpy(httpServerData->basePathFileserver, basePath, sizeof(httpServerData->basePathFileserver));

    httpd_uri_t file_download = {
        .uri       = "/fileserver*", // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*", // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*", // Match all URIs of type /delete/path/to/file
        .method    = HTTP_POST,
        .handler   = delete_post_handler,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    /* URI handler for deleting files from server */
    httpd_uri_t coredump = {
        .uri       = "/coredump",
        .method    = HTTP_GET,
        .handler   = coredump_handler,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &coredump);

    httpd_uri_t handler_logfile = {
        .uri       = "/log",
        .method    = HTTP_GET,
        .handler   = handler_logfiles,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &handler_logfile);

    httpd_uri_t handler_datafile = {
        .uri       = "/data",
        .method    = HTTP_GET,
        .handler   = handler_datafiles,
        .user_ctx  = httpServerData // Pass server data as context
    };
    httpd_register_uri_handler(server, &handler_datafile);
}