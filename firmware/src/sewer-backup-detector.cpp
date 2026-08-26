// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Edit config_and_secrets.h.
//
// Dry contact D2–GND, INPUT_PULLUP. Debug LED on D7.
// Closed (dry) => LOW => OFF. Open (backup or cut wire) => HIGH => ON.
// ON/OFF are Home Assistant binary_sensor payloads (device_class moisture:
// on = wet, off = dry). Automations decide whether that is an alarm.

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

const pin_t CONTACT_PIN = D2;
const pin_t LED_PIN = D7;
const unsigned long WET_DEBOUNCE_MS = 2000;
const unsigned long HEARTBEAT_MS = 60000;
const unsigned long RETRY_MS = 5000;
const int kUnsent = -1;

// contactOpenedAt is when the pin went open (0 = closed). wet is the latched
// moisture after WET_DEBOUNCE_MS. They disagree only during debounce.
static bool wet;
static unsigned long contactOpenedAt;

// 0 = dry, 1 = wet, kUnsent = nothing delivered yet.
static int lastWetSentToHomeAssistant;
static unsigned long lastHomeAssistantPublishAt;
static bool backingOff;
static unsigned long backoffStartedAt;

static bool haDownPublishedToParticleCloud;
static int lastWetSentToParticleCloud;

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
const char* TOPIC_STATE = "sewer/state";
void mqttCallback(char*, uint8_t*, unsigned int) {}
MQTT mqtt(MQTT_BROKER_IP, MQTT_BROKER_PORT, MQTT_BUFFER_SIZE, MQTT_KEEPALIVE_S, mqttCallback);
#endif

bool sendToParticleCloud(const char* event, const char* data) {
    return Particle.connected() && Particle.publish(event, data, PRIVATE);
}

#ifdef SEWER_TRANSPORT_HTTP
bool sendToHomeAssistant(const char* state) {
    if (!WiFi.ready()) {
        Log.warn("Wi-Fi not ready");
        return false;
    }
    httpRequest.body = String::format("{\"state\":\"%s\"}", state);
    http.post(httpRequest, httpResponse, httpHeaders);
    if (httpResponse.status != 200) {
        Log.warn("HTTP %d", httpResponse.status);
        return false;
    }
    return true;
}
#endif

#ifdef SEWER_TRANSPORT_MQTT
bool sendToHomeAssistant(const char* state) {
    if (!mqtt.isConnected()) {
        return false;
    }
    if (!mqtt.publish(TOPIC_STATE, state, true)) {
        Log.warn("mqtt publish failed");
        return false;
    }
    return true;
}

void connectToMqtt() {
    if (!WiFi.ready()) {
        return;
    }
    if (backingOff && millis() - backoffStartedAt < RETRY_MS) {
        return;
    }
    backingOff = true;
    backoffStartedAt = millis();

    String clientId = String("sewer-") + System.deviceID();
    if (!mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                      TOPIC_AVAILABILITY, MQTT::QOS1, 1, "offline", true)) {
        Log.warn("mqtt connect failed");
        return;
    }
    mqtt.publish(TOPIC_AVAILABILITY, "online", true);
    lastWetSentToHomeAssistant = kUnsent;
    backingOff = false;
    Log.info("mqtt connected");
}
#endif

void updateWet(bool contactOpen) {
    if (!contactOpen) {
        contactOpenedAt = 0;
        if (wet) {
            wet = false;
            Log.info("dry");
        }
        return;
    }
    if (wet) {
        return;
    }
    if (contactOpenedAt == 0) {
        contactOpenedAt = millis();
        Log.info("contact open");
        return;
    }
    if (millis() - contactOpenedAt >= WET_DEBOUNCE_MS) {
        wet = true;
        Log.info("wet");
    }
}

void setup() {
    wet = false;
    contactOpenedAt = 0;
    lastWetSentToHomeAssistant = kUnsent;
    lastHomeAssistantPublishAt = 0;
    backingOff = false;
    backoffStartedAt = 0;
    haDownPublishedToParticleCloud = false;
    lastWetSentToParticleCloud = kUnsent;

    pinMode(LED_PIN, OUTPUT);
    pinMode(CONTACT_PIN, INPUT_PULLUP);

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
#ifdef SEWER_TRANSPORT_MQTT
    if (mqtt.isConnected()) {
        mqtt.loop();
    } else {
        connectToMqtt();
    }
#endif

    const bool contactOpen = digitalRead(CONTACT_PIN) == HIGH;
    digitalWrite(LED_PIN, (contactOpen && ((millis() / 100) % 2)) ? HIGH : LOW);
    updateWet(contactOpen);

    const int wetInt = wet ? 1 : 0;
    const char* state = wet ? "ON" : "OFF";

    const bool haDue = lastWetSentToHomeAssistant != wetInt ||
        (lastWetSentToHomeAssistant != kUnsent &&
         millis() - lastHomeAssistantPublishAt >= HEARTBEAT_MS);
    if (haDue && (!backingOff || millis() - backoffStartedAt >= RETRY_MS)) {
        if (sendToHomeAssistant(state)) {
            if (haDownPublishedToParticleCloud) {
                sendToParticleCloud("sewer-ha", "ok");
                haDownPublishedToParticleCloud = false;
            }
            lastWetSentToHomeAssistant = wetInt;
            lastHomeAssistantPublishAt = millis();
            backingOff = false;
        } else {
            backingOff = true;
            backoffStartedAt = millis();
            if (!haDownPublishedToParticleCloud) {
#ifdef SEWER_TRANSPORT_HTTP
                if (WiFi.ready()) {
                    String err = String::format("HTTP %d", httpResponse.status);
                    haDownPublishedToParticleCloud =
                        sendToParticleCloud("sewer-ha", err.c_str());
                }
#endif
#ifdef SEWER_TRANSPORT_MQTT
                haDownPublishedToParticleCloud =
                    sendToParticleCloud("sewer-ha", "mqtt");
#endif
            }
        }
    }

    if (lastWetSentToParticleCloud != wetInt) {
        if (sendToParticleCloud("sewer-state", state)) {
            lastWetSentToParticleCloud = wetInt;
        }
    }

    delay(20);
}
