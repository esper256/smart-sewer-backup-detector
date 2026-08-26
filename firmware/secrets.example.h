#pragma once

// Copy this file to secrets.h and replace the placeholders.
// secrets.h is gitignored.

// Pick one transport. MQTT needs a broker (often the Mosquitto add-on on the
// same machine as Home Assistant). HTTP talks to Home Assistant directly.
#define SEWER_TRANSPORT_MQTT
// #define SEWER_TRANSPORT_HTTP

#if defined(SEWER_TRANSPORT_MQTT) && defined(SEWER_TRANSPORT_HTTP)
#error Define only one of SEWER_TRANSPORT_MQTT or SEWER_TRANSPORT_HTTP
#endif
#if !defined(SEWER_TRANSPORT_MQTT) && !defined(SEWER_TRANSPORT_HTTP)
#error Define SEWER_TRANSPORT_MQTT or SEWER_TRANSPORT_HTTP
#endif

#ifdef SEWER_TRANSPORT_MQTT
const uint8_t MQTT_BROKER_IP[] = {192, 168, 0, 2};
const uint16_t MQTT_BROKER_PORT = 1883;
const char* MQTT_USERNAME = "replacemewithyourusername";
const char* MQTT_PASSWORD = "replacemewithyourpassword";
#endif

#ifdef SEWER_TRANSPORT_HTTP
// Home Assistant LAN address and webhook path. HTTP only (not HTTPS).
const char* HA_HOST = "192.168.0.2";
const int HA_PORT = 8123;
const char* HA_WEBHOOK_PATH = "/api/webhook/replacemewithyourwebhookid";
#endif
