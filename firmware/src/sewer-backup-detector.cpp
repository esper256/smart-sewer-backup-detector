// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Edit config_and_secrets.h.
//
// Dry contact D2–GND, INPUT_PULLUP. Debug LED on D7.
// Closed (dry) => LOW => OK. Open (backup or cut wire) => HIGH => ALARM.

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
const unsigned long ALARM_DEBOUNCE_MS = 2000;
const unsigned long HEARTBEAT_MS = 60000;
const unsigned long RETRY_MS = 5000;

struct {
    bool alarm;
    unsigned long openedAt;
    bool haSent;
    bool haAlarm;
    unsigned long haSentAt;
    bool backingOff;
    unsigned long backoffAt;
    bool particleHaDown;
    bool particleHasAlarm;
    bool particleAlarm;
} g;

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

bool elapsed(unsigned long start, unsigned long ms) {
    return millis() - start >= ms;
}

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
    if (g.backingOff && !elapsed(g.backoffAt, RETRY_MS)) {
        return;
    }
    g.backingOff = true;
    g.backoffAt = millis();

    String clientId = String("sewer-") + System.deviceID();
    if (!mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                      TOPIC_AVAILABILITY, MQTT::QOS1, 1, "offline", true)) {
        Log.warn("mqtt connect failed");
        return;
    }
    mqtt.publish(TOPIC_AVAILABILITY, "online", true);
    g.haSent = false;
    g.backingOff = false;
    Log.info("mqtt connected");
}
#endif

void readContact() {
    const bool open = digitalRead(CONTACT_PIN) == HIGH;
    digitalWrite(LED_PIN, (open && ((millis() / 100) % 2)) ? HIGH : LOW);

    if (!open) {
        g.openedAt = 0;
        if (g.alarm) {
            g.alarm = false;
            Log.info("OK");
        }
        return;
    }
    if (g.alarm) {
        return;
    }
    if (g.openedAt == 0) {
        g.openedAt = millis();
        Log.info("contact open");
        return;
    }
    if (elapsed(g.openedAt, ALARM_DEBOUNCE_MS)) {
        g.alarm = true;
        Log.info("ALARM");
    }
}

void setup() {
    g = {};
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

    readContact();
    const char* state = g.alarm ? "ALARM" : "OK";

    if (!g.particleHasAlarm || g.particleAlarm != g.alarm) {
        if (sendToParticleCloud("sewer-alarm", state)) {
            g.particleHasAlarm = true;
            g.particleAlarm = g.alarm;
        }
    }

    const bool haDue = !g.haSent || g.haAlarm != g.alarm ||
        (g.haSent && elapsed(g.haSentAt, HEARTBEAT_MS));
    if (haDue && (!g.backingOff || elapsed(g.backoffAt, RETRY_MS))) {
        if (sendToHomeAssistant(state)) {
            if (g.particleHaDown) {
                sendToParticleCloud("sewer-ha", "ok");
                g.particleHaDown = false;
            }
            g.haSent = true;
            g.haAlarm = g.alarm;
            g.haSentAt = millis();
            g.backingOff = false;
        } else {
            g.backingOff = true;
            g.backoffAt = millis();
            if (!g.particleHaDown) {
#ifdef SEWER_TRANSPORT_HTTP
                if (WiFi.ready()) {
                    String err = String::format("HTTP %d", httpResponse.status);
                    g.particleHaDown = sendToParticleCloud("sewer-ha", err.c_str());
                }
#endif
#ifdef SEWER_TRANSPORT_MQTT
                g.particleHaDown = sendToParticleCloud("sewer-ha", "mqtt");
#endif
            }
        }
    }

    delay(20);
}
