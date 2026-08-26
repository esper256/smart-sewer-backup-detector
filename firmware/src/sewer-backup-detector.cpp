// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Edit config_and_secrets.h. Add the HttpClient library
// (or the MQTT library if you switch transport).
//
// Wiring: reed between D2 and GND (INPUT_PULLUP). Debug LED on D7.
// Dry pipe (float down, reed closed) => D2 LOW.
// Backup (float up, reed open)       => D2 HIGH.
// A cut wire reads the same as a backup.

#include "Particle.h"
#include "config_and_secrets.h"

#ifdef SEWER_TRANSPORT_HTTP
#include "HttpClient.h"
#endif
#ifdef SEWER_TRANSPORT_MQTT
#include "MQTT.h"
#endif

SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

const pin_t REED_PIN = D2;
const pin_t LED_PIN = D7;

const unsigned long OPEN_DEBOUNCE_MS = 2000;
const unsigned long PUBLISH_PERIOD_MS = 60000;
const unsigned long RETRY_MS = 5000;

const char* PAYLOAD_OPEN = "OPEN";
const char* PAYLOAD_CLOSED = "CLOSED";

bool rawOpen = false;
bool stableOpen = false;
unsigned long openSince = 0;

bool havePublished = false;
bool publishedOpen = false;
unsigned long lastPublishMs = 0;
unsigned long lastRetryMs = 0;
bool retryWait = false;
bool haCloudError = false;
bool cloudReedKnown = false;
bool cloudReedOpen = false;

#ifdef SEWER_TRANSPORT_HTTP
const uint16_t HTTP_TIMEOUT_MS = 4000;

HttpClient http;
http_header_t httpHeaders[] = {
    { "Content-Type", "application/json" },
    { NULL, NULL }
};
http_request_t httpRequest;
http_response_t httpResponse;
#endif

#ifdef SEWER_TRANSPORT_MQTT
const int MQTT_KEEPALIVE_S = 60;
const int MQTT_BUFFER_SIZE = 256;
const char* TOPIC_AVAILABILITY = "sewer/availability";
const char* TOPIC_REED = "sewer/reed";

void mqttCallback(char*, uint8_t*, unsigned int) {}

MQTT mqtt(MQTT_BROKER_IP, MQTT_BROKER_PORT, MQTT_BUFFER_SIZE, MQTT_KEEPALIVE_S, mqttCallback);
#endif

const char* reedPayload() {
    return stableOpen ? PAYLOAD_OPEN : PAYLOAD_CLOSED;
}

bool cloudNote(const char* event, const char* data) {
    if (!Particle.connected()) {
        return false;
    }
    return Particle.publish(event, data, PRIVATE);
}

void cloudReedIfChanged() {
    if (cloudReedKnown && cloudReedOpen == stableOpen) {
        return;
    }
    if (!cloudNote("sewer-reed", reedPayload())) {
        return;
    }
    cloudReedKnown = true;
    cloudReedOpen = stableOpen;
}

void noteHaResult(bool ok) {
    if (ok) {
        if (haCloudError) {
            cloudNote("sewer-ha", "ok");
            haCloudError = false;
        }
        return;
    }
    if (haCloudError) {
        return;
    }
#ifdef SEWER_TRANSPORT_HTTP
    if (!WiFi.ready()) {
        return;
    }
    String detail = String::format("HTTP %d", httpResponse.status);
    if (!cloudNote("sewer-ha", detail.c_str())) {
        return;
    }
#endif
#ifdef SEWER_TRANSPORT_MQTT
    if (!cloudNote("sewer-ha", "mqtt")) {
        return;
    }
#endif
    haCloudError = true;
}

#ifdef SEWER_TRANSPORT_HTTP
void ensureTransport() {
}

bool sendReed() {
    if (!WiFi.ready()) {
        Log.warn("Wi-Fi not ready");
        return false;
    }

    httpRequest.body = String::format("{\"reed\":\"%s\"}", reedPayload());
    http.post(httpRequest, httpResponse, httpHeaders);
    if (httpResponse.status != 200) {
        Log.warn("HTTP %d", httpResponse.status);
        return false;
    }
    return true;
}
#endif

#ifdef SEWER_TRANSPORT_MQTT
void ensureTransport() {
    if (mqtt.isConnected()) {
        mqtt.loop();
        return;
    }

    if (!WiFi.ready()) {
        return;
    }
    if (retryWait && (millis() - lastRetryMs) < RETRY_MS) {
        return;
    }
    lastRetryMs = millis();
    retryWait = true;

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
    lastRetryMs = 0;
    retryWait = false;
    Log.info("mqtt connected");
}

bool sendReed() {
    if (!mqtt.isConnected()) {
        return false;
    }
    if (!mqtt.publish(TOPIC_REED, reedPayload(), true)) {
        Log.warn("mqtt publish failed");
        return false;
    }
    return true;
}
#endif

void publishReed(bool force) {
    const bool stateChanged = !havePublished || publishedOpen != stableOpen;
    const bool heartbeatDue = havePublished &&
        ((millis() - lastPublishMs) >= PUBLISH_PERIOD_MS);

    if (stateChanged) {
        cloudReedIfChanged();
    }

    if (!force && !stateChanged && !heartbeatDue) {
        return;
    }
    if (retryWait && (millis() - lastRetryMs) < RETRY_MS) {
        return;
    }

    if (!sendReed()) {
        lastRetryMs = millis();
        retryWait = true;
        noteHaResult(false);
        return;
    }

    noteHaResult(true);
    havePublished = true;
    publishedOpen = stableOpen;
    lastPublishMs = millis();
    lastRetryMs = 0;
    retryWait = false;
    if (stateChanged) {
        Log.info("reed %s", reedPayload());
    }
}

void setup() {
    rawOpen = false;
    stableOpen = false;
    openSince = 0;
    havePublished = false;
    publishedOpen = false;
    lastPublishMs = 0;
    lastRetryMs = 0;
    retryWait = false;
    haCloudError = false;
    cloudReedKnown = false;
    cloudReedOpen = false;

    pinMode(LED_PIN, OUTPUT);
    pinMode(REED_PIN, INPUT_PULLUP);

#ifdef SEWER_TRANSPORT_HTTP
    httpRequest.hostname = HA_HOST;
    httpRequest.port = HA_PORT;
    httpRequest.path = HA_WEBHOOK_PATH;
    httpRequest.timeout = HTTP_TIMEOUT_MS;
#endif

    Watchdog.init(WatchdogConfiguration().timeout(90s));
    Watchdog.start();

    Log.info("setup complete");
}

void loop() {
    Watchdog.refresh();
    ensureTransport();

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
