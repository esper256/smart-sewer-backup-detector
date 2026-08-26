// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Edit config_and_secrets.h. Add the HttpClient library
// (or the MQTT library if you switch transport).
//
// Wiring: dry contact between D2 and GND (INPUT_PULLUP). Debug LED on D7.
// Closed contact (dry pipe) => D2 LOW  => OK.
// Open contact (backup, or a cut wire) => D2 HIGH => ALARM.

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

const char* STATE_ALARM = "ALARM";
const char* STATE_OK = "OK";

bool contactOpen = false;
bool alarm = false;
unsigned long contactOpenAt = 0;

bool reported = false;
bool reportedAlarm = false;
unsigned long lastReportAt = 0;
unsigned long lastAttemptAt = 0;
bool backingOff = false;
bool haFailed = false;
bool cloudAlarmKnown = false;
bool cloudAlarm = false;

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

const char* stateText() {
    return alarm ? STATE_ALARM : STATE_OK;
}

bool publishCloud(const char* event, const char* data) {
    if (!Particle.connected()) {
        return false;
    }
    return Particle.publish(event, data, PRIVATE);
}

void publishCloudAlarm() {
    if (cloudAlarmKnown && cloudAlarm == alarm) {
        return;
    }
    if (!publishCloud("sewer-alarm", stateText())) {
        return;
    }
    cloudAlarmKnown = true;
    cloudAlarm = alarm;
}

void publishCloudHa(bool delivered) {
    if (delivered) {
        if (haFailed) {
            publishCloud("sewer-ha", "ok");
            haFailed = false;
        }
        return;
    }
    if (haFailed) {
        return;
    }
#ifdef SEWER_TRANSPORT_HTTP
    if (!WiFi.ready()) {
        return;
    }
    String detail = String::format("HTTP %d", httpResponse.status);
    if (!publishCloud("sewer-ha", detail.c_str())) {
        return;
    }
#endif
#ifdef SEWER_TRANSPORT_MQTT
    if (!publishCloud("sewer-ha", "mqtt")) {
        return;
    }
#endif
    haFailed = true;
}

#ifdef SEWER_TRANSPORT_HTTP
bool deliver() {
    if (!WiFi.ready()) {
        Log.warn("Wi-Fi not ready");
        return false;
    }

    httpRequest.body = String::format("{\"state\":\"%s\"}", stateText());
    http.post(httpRequest, httpResponse, httpHeaders);
    if (httpResponse.status != 200) {
        Log.warn("HTTP %d", httpResponse.status);
        return false;
    }
    return true;
}
#endif

#ifdef SEWER_TRANSPORT_MQTT
void mqttConnect() {
    if (!WiFi.ready()) {
        return;
    }
    if (backingOff && (millis() - lastAttemptAt) < RETRY_MS) {
        return;
    }
    lastAttemptAt = millis();
    backingOff = true;

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
    reported = false;
    lastAttemptAt = 0;
    backingOff = false;
    Log.info("mqtt connected");
}

bool deliver() {
    if (!mqtt.isConnected()) {
        return false;
    }
    if (!mqtt.publish(TOPIC_STATE, stateText(), true)) {
        Log.warn("mqtt publish failed");
        return false;
    }
    return true;
}
#endif

void report(bool immediate) {
    const bool changed = !reported || reportedAlarm != alarm;
    const bool heartbeatDue = reported &&
        ((millis() - lastReportAt) >= HEARTBEAT_MS);

    if (changed) {
        publishCloudAlarm();
    }

    if (!immediate && !changed && !heartbeatDue) {
        return;
    }
    if (backingOff && (millis() - lastAttemptAt) < RETRY_MS) {
        return;
    }

    if (!deliver()) {
        lastAttemptAt = millis();
        backingOff = true;
        publishCloudHa(false);
        return;
    }

    publishCloudHa(true);
    reported = true;
    reportedAlarm = alarm;
    lastReportAt = millis();
    lastAttemptAt = 0;
    backingOff = false;
    if (changed) {
        Log.info("%s", stateText());
    }
}

void setup() {
    contactOpen = false;
    alarm = false;
    contactOpenAt = 0;
    reported = false;
    reportedAlarm = false;
    lastReportAt = 0;
    lastAttemptAt = 0;
    backingOff = false;
    haFailed = false;
    cloudAlarmKnown = false;
    cloudAlarm = false;

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
        mqttConnect();
    }
#endif

    contactOpen = (digitalRead(CONTACT_PIN) == HIGH);

    if (contactOpen) {
        digitalWrite(LED_PIN, ((millis() / 100) % 2) ? HIGH : LOW);
        if (!alarm) {
            if (contactOpenAt == 0) {
                contactOpenAt = millis();
                Log.info("contact open");
            } else if ((millis() - contactOpenAt) >= ALARM_DEBOUNCE_MS) {
                alarm = true;
                Log.info("ALARM");
                report(true);
            }
        }
    } else {
        digitalWrite(LED_PIN, LOW);
        contactOpenAt = 0;
        if (alarm) {
            alarm = false;
            Log.info("OK");
            report(true);
        }
    }

    report(false);
    delay(20);
}
