#ifndef JOMJOL_CONTROLCAMERA_H
#define JOMJOL_CONTROLCAMERA_H

#include <esp_log.h>
#include <esp_http_server.h>


void registerCameraUri(httpd_handle_t server);

#endif