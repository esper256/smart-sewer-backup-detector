// Particle Photon 2 — sewer backup detector
// Device OS 5.3+. Add the HttpClient library in Particle Web IDE / Workbench.
//
// Wiring: reed switch between D2 and GND. D2 is INPUT_PULLUP.
// Dry pipe (float down, reed closed) => D2 LOW.
// Backup (float up, reed open)  => D2 HIGH. A cut wire looks like an alarm.

#include "Particle.h"
#include <HttpClient.h>

SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

const pin_t REED_PIN = D2;
const pin_t LED_PIN = D7; // Photon 2 has no D7 user LED; harmless GPIO if you add one

const unsigned long OPEN_MS = 2000;
const unsigned long RETRY_MS = 5000;
const unsigned long HEARTBEAT_MS = 28UL * 24UL * 60UL * 60UL * 1000UL; // 28 days
const uint16_t HTTP_TIMEOUT_MS = 4000;

const char* EVENT_OPEN = "ReedSwitchOpen";
const char* EVENT_WEBHOOK_OK = "HomeAssistantWebhookSuccess";
const char* EVENT_ERROR = "ReedSwitchWebhookError";

// --- fill in. Do not commit live webhook IDs. ---
const char* HA_HOST = "192.168.0.2";
const int HA_PORT = 8123;
const char* HA_ALARM_PATH = "/api/webhook/YOUR_ALARM_WEBHOOK_ID";
const char* HA_HEARTBEAT_PATH = "/api/webhook/YOUR_HEARTBEAT_WEBHOOK_ID";

HttpClient http;
http_header_t headers[] = {
    { "Content-Type", "application/json" },
    { NULL, NULL }
};

http_request_t alarmRequest;
http_request_t heartbeatRequest;
http_response_t response;

bool switchOpen = false;
bool alarmDelivered = false;
unsigned long openSince = 0;
unsigned long lastAlarmAttempt = 0;

bool heartbeatOk = false;
unsigned long lastHeartbeat = 0;

int ledState = LOW;

void configureRequest(http_request_t &req, const char* path, const char* body) {
    // Stock HttpClient treats String hostname as always "set", so a dotted-quad
    // in hostname is the reliable path. request.ip is unused by that library.
    req.hostname = HA_HOST;
    req.port = HA_PORT;
    req.path = path;
    req.body = body;
    req.timeout = HTTP_TIMEOUT_MS;
}

void publishIfCloud(const char* event, const char* data) {
    if (Particle.connected()) {
        Particle.publish(event, data, PRIVATE);
    }
}

bool postToHa(http_request_t &req) {
    if (!WiFi.ready()) {
        Log.warn("Wi-Fi not ready");
        return false;
    }

    http.post(req, response, headers);
    Log.info("HTTP %d %s", response.status, response.body.c_str());
    return response.status == 200;
}

void publishHttpError() {
    // Keep the payload short; Particle events are size-limited and HA bodies can
    // contain quotes that would break a naive JSON wrap.
    String err = String::format("status=%d", response.status);
    publishIfCloud(EVENT_ERROR, err);
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(REED_PIN, INPUT_PULLUP);

    configureRequest(alarmRequest, HA_ALARM_PATH,
                     "{\"status\":\"open\",\"message\":\"Reed switch open for 2+ seconds\"}");
    configureRequest(heartbeatRequest, HA_HEARTBEAT_PATH,
                     "{\"status\":\"ok\",\"message\":\"No backup detected recently\"}");

    Watchdog.init(WatchdogConfiguration().timeout(60s));
    Watchdog.start();

    Log.info("setup complete");
}

void loop() {
    Watchdog.refresh();

    const bool openNow = (digitalRead(REED_PIN) == HIGH);

    if (openNow) {
        ledState = (ledState == LOW) ? HIGH : LOW;
        digitalWrite(LED_PIN, ledState);

        if (!switchOpen) {
            switchOpen = true;
            openSince = millis();
            lastAlarmAttempt = 0;
            Log.info("reed open");
        }

        const bool held = (millis() - openSince) >= OPEN_MS;
        const bool retryDue = (lastAlarmAttempt == 0) ||
                              ((millis() - lastAlarmAttempt) >= RETRY_MS);

        if (held && !alarmDelivered && retryDue) {
            lastAlarmAttempt = millis();
            Log.info("alarm threshold");

            // Cloud event first: still get a record if the LAN POST fails.
            publishIfCloud(EVENT_OPEN, "Switch open for 2+ seconds");

            if (postToHa(alarmRequest)) {
                alarmDelivered = true;
                publishIfCloud(EVENT_WEBHOOK_OK, "HA Webhook notified");
            } else {
                publishHttpError();
            }
        }
    } else {
        if (switchOpen) {
            Log.info("reed closed");
            digitalWrite(LED_PIN, LOW);
            ledState = LOW;
        }
        switchOpen = false;
        alarmDelivered = false;
    }

    const bool heartbeatDue = !heartbeatOk
        ? (lastHeartbeat == 0 || (millis() - lastHeartbeat) >= RETRY_MS)
        : ((millis() - lastHeartbeat) >= HEARTBEAT_MS);

    if (heartbeatDue) {
        lastHeartbeat = millis();
        Log.info("heartbeat");
        if (postToHa(heartbeatRequest)) {
            heartbeatOk = true;
            publishIfCloud(EVENT_WEBHOOK_OK, "HA no-op webhook notified");
        } else {
            heartbeatOk = false;
            publishHttpError();
        }
    }

    delay(100);
}
