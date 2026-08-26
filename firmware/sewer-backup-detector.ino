// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Add the MQTT library (hirotakaster). Copy secrets.example.h to secrets.h.
//
// Wiring: reed between D2 and GND (INPUT_PULLUP). Debug LED on D7.
// Dry pipe (float down, reed closed) => D2 LOW.
// Backup (float up, reed open)       => D2 HIGH.
// A cut wire reads the same as a backup.
//
// The Photon reports reed state over LAN MQTT. Home Assistant sends the alerts.

#include "Particle.h"
#include "MQTT.h"
#include "secrets.h"

SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

const pin_t REED_PIN = D2;
const pin_t LED_PIN = D7;

const unsigned long OPEN_DEBOUNCE_MS = 2000;
const unsigned long PUBLISH_PERIOD_MS = 60000;
const unsigned long MQTT_RECONNECT_MS = 5000;
const int MQTT_KEEPALIVE_S = 60;
const int MQTT_BUFFER_SIZE = 256;

const char* TOPIC_AVAILABILITY = "sewer/availability";
const char* TOPIC_REED = "sewer/reed";
const char* PAYLOAD_OPEN = "OPEN";
const char* PAYLOAD_CLOSED = "CLOSED";

void mqttCallback(char*, uint8_t*, unsigned int) {}

MQTT mqtt(MQTT_BROKER_IP, MQTT_BROKER_PORT, MQTT_BUFFER_SIZE, MQTT_KEEPALIVE_S, mqttCallback);

bool rawOpen = false;
bool stableOpen = false;
unsigned long openSince = 0;

bool havePublished = false;
bool publishedOpen = false;
unsigned long lastPublishMs = 0;
unsigned long lastMqttAttemptMs = 0;

void publishReed(bool force) {
    if (!mqtt.isConnected()) {
        return;
    }
    if (!force && havePublished && publishedOpen == stableOpen &&
        (millis() - lastPublishMs) < PUBLISH_PERIOD_MS) {
        return;
    }

    const char* payload = stableOpen ? PAYLOAD_OPEN : PAYLOAD_CLOSED;
    if (!mqtt.publish(TOPIC_REED, payload, true)) {
        Log.warn("mqtt publish failed");
        return;
    }

    havePublished = true;
    publishedOpen = stableOpen;
    lastPublishMs = millis();
    Log.info("reed %s", payload);
}

void ensureMqtt() {
    if (mqtt.isConnected()) {
        mqtt.loop();
        return;
    }

    if (!WiFi.ready()) {
        return;
    }
    if (lastMqttAttemptMs != 0 && (millis() - lastMqttAttemptMs) < MQTT_RECONNECT_MS) {
        return;
    }
    lastMqttAttemptMs = millis();

    String clientId = String("sewer-") + System.deviceID();
    const bool ok = mqtt.connect(
        clientId.c_str(),
        MQTT_USERNAME,
        MQTT_PASSWORD,
        TOPIC_AVAILABILITY,
        MQTT::QOS1,
        1,
        "offline",
        true
    );
    if (!ok) {
        Log.warn("mqtt connect failed");
        return;
    }

    mqtt.publish(TOPIC_AVAILABILITY, "online", true);
    havePublished = false;
    Log.info("mqtt connected");
    publishReed(true);
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(REED_PIN, INPUT_PULLUP);

    Watchdog.init(WatchdogConfiguration().timeout(90s));
    Watchdog.start();

    Log.info("setup complete");
}

void loop() {
    Watchdog.refresh();
    ensureMqtt();

    rawOpen = (digitalRead(REED_PIN) == HIGH);

    if (rawOpen) {
        digitalWrite(LED_PIN, ((millis() / 100) % 2) ? HIGH : LOW);
        if (!stableOpen) {
            if (openSince == 0) {
                openSince = millis();
                Log.info("reed opening");
            } else if ((millis() - openSince) >= OPEN_DEBOUNCE_MS) {
                stableOpen = true;
                Log.info("reed open");
                publishReed(true);
            }
        }
    } else {
        digitalWrite(LED_PIN, LOW);
        openSince = 0;
        if (stableOpen) {
            stableOpen = false;
            Log.info("reed closed");
            publishReed(true);
        }
    }

    publishReed(false);
    delay(20);
}
