#pragma once

// Host stand-in for nmattisson/HttpClient. Same method names the firmware calls.

#include "Particle.h"

struct http_header_t {
    const char* header;
    const char* value;
};

struct http_request_t {
    String hostname;
    IPAddress ip;
    String path;
    int port = 0;
    String body;
    uint16_t timeout = 0;
};

struct http_response_t {
    int status = -1;
    String body;
};

class HttpClient {
public:
    void post(http_request_t& request, http_response_t& response, http_header_t*) {
        Host::posts.push_back({
            request.hostname.c_str(),
            request.port,
            request.path.c_str(),
            request.body.c_str(),
        });
        response.status = Host::http_status;
    }
};
