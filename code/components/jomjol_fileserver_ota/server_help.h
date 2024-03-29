#ifndef SERVERHELP_H
#define SERVERHELP_H

#include <string>

#include "esp_http_server.h"


esp_err_t send_file(httpd_req_t *req, std::string filename);
const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize);
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename);

#endif //SERVERHELP_H