#pragma once

// Copy this file to secrets.h and replace the placeholders.
// secrets.h is gitignored. Never commit real credentials.

// LAN MQTT broker (Home Assistant Mosquitto add-on, or equivalent). MQTT is
// plaintext. Do not expose port 1883 past your LAN.

const uint8_t MQTT_BROKER_IP[] = {192, 168, 0, 2};
const uint16_t MQTT_BROKER_PORT = 1883;
const char* MQTT_USERNAME = "replacemewithyourusername";
const char* MQTT_PASSWORD = "replacemewithyourpassword";
