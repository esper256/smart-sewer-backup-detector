#pragma once

// Fill in the placeholders. This file is the one the firmware includes.
// Do not commit a filled-in copy with a live webhook id or MQTT password.

// HTTP is the default: the Photon POSTs to Home Assistant on the LAN.
// MQTT is optional if you already run a broker (for example the Mosquitto
// add-on on the same machine as Home Assistant).
#define SEWER_TRANSPORT_HTTP
// #define SEWER_TRANSPORT_MQTT

#if defined(SEWER_TRANSPORT_MQTT) && defined(SEWER_TRANSPORT_HTTP)
#error Define only one of SEWER_TRANSPORT_HTTP or SEWER_TRANSPORT_MQTT
#endif
#if !defined(SEWER_TRANSPORT_MQTT) && !defined(SEWER_TRANSPORT_HTTP)
#error Define SEWER_TRANSPORT_HTTP or SEWER_TRANSPORT_MQTT
#endif

#ifdef SEWER_TRANSPORT_HTTP
const char* HA_HOST = "192.168.0.2";
const int HA_PORT = 8123;
const char* HA_WEBHOOK_PATH = "/api/webhook/replacemewithyourwebhookid";
#endif

#ifdef SEWER_TRANSPORT_MQTT
const uint8_t MQTT_BROKER_IP[] = {192, 168, 0, 2};
const uint16_t MQTT_BROKER_PORT = 1883;
const char* MQTT_USERNAME = "replacemewithyourusername";
const char* MQTT_PASSWORD = "replacemewithyourpassword";
#endif
