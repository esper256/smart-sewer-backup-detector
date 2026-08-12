// This #include statement was automatically added by the Particle IDE.
#include <HttpClient.h>

PRODUCT_VERSION(1);

// Pin connected to the reed switch
const pin_t reedPin = D2;
const pin_t BUILT_IN_LED_PIN = D7;

// Variables to track the switch state and timing
bool isOpen = false;
bool currentOpenSuccessfullyNotified = false;

int ledBlinkState = LOW;

unsigned long openStartTime = 0;
const unsigned long OPEN_DURATION = 2000; // 2 seconds in milliseconds

bool noopNotified = false;
unsigned long lastNoopNotify = 0;
const unsigned long  NOOP_NOTIFY_PERIOD = 2419200000; // 28 days in milliseconds

// Particle event names
const char* EVENT_WEBHOOK_SUCCESS = "HomeAssistantWebhookSuccess";
const char* EVENT_SWITCH_OPEN = "ReedSwitchOpen";
const char* EVENT_ERROR = "ReedSwitchWebhookError";

// ----------  HTTP STUFF -----------
HttpClient http;
http_header_t headers[] = {
    // Add Authorization header if Home Assistant requires it
    // { "Authorization", "Bearer <YOUR_LONG_LIVED_ACCESS_TOKEN>" },
    { "Content-Type", "application/json" },
    { NULL, NULL } // End of headers
};
http_request_t request;
http_response_t response;

http_request_t noop_request;

// Home Assistant webhook details
const char* HA_IP = "192.168.0.2"; // Replace with your Home Assistant IP
const int HA_PORT = 8123;             // Default Home Assistant port

const char* HA_WEBHOOK_PATH = "/api/webhook/-PTgQpTPqjI3WmngdcCFITYa8";
const char* HA_NOOP_WEBHOOK_PATH = "/api/webhook/-tiM7hTNzw0RnNX46qtm2dqcJ";

void setup() {
    pinMode(BUILT_IN_LED_PIN, OUTPUT);

    // Configure the reed switch pin with internal pull-up
    pinMode(reedPin, INPUT_PULLUP);

    // Optional: Initialize Serial for debugging
    Serial.begin(9600);
    
    // Configure HTTP request
    request.hostname = HA_IP;
    request.port = HA_PORT;
    request.path = HA_WEBHOOK_PATH;
    request.body = "{\"status\": \"open\", \"message\": \"Reed switch open for 2+ seconds\"}";
    
    // Configure HTTP no-op request
    noop_request.hostname = HA_IP;
    noop_request.port = HA_PORT;
    noop_request.path = HA_NOOP_WEBHOOK_PATH;
    noop_request.body = "{\"status\": \"open\", \"message\": \"No backlog detected recently\"}";
    
    Serial.println("Setup() Complete");
}

void loop() {
    // Read the state of the reed switch
    int switchState = digitalRead(reedPin);
    
    // If the switch is open (HIGH due to pull-up), track the duration
    if (switchState == HIGH) {
        // Flip the state
        ledBlinkState = (ledBlinkState == LOW) ? HIGH : LOW;
        digitalWrite(BUILT_IN_LED_PIN, ledBlinkState);
        
        if (!isOpen) {
            // Switch just opened, record the start time
            isOpen = true;
            openStartTime = millis();
            Serial.println("Reed switch opened");
        }
        // Check if the switch has been open for 2 seconds
        if (isOpen && !currentOpenSuccessfullyNotified && (millis() - openStartTime >= OPEN_DURATION)) {
            Serial.println("Event published: Switch open for 2+ seconds");

            // Send HTTP POST to Home Assistant webhook
            http.post(request, response, headers);
            Serial.print("HTTP Response: ");
            Serial.println(response.status);
            Serial.println(response.body);

            // Publish event to Particle Cloud
            Particle.publish(EVENT_SWITCH_OPEN, "Switch open for 2+ seconds", PRIVATE);

            // Check HTTP response status
            if (response.status == 200) {
                // Success: Publish success event to Particle Cloud
                Particle.publish(EVENT_WEBHOOK_SUCCESS, "HA Webhook notified", PRIVATE);
                Serial.println("Success event published to Home Assistant");
                
                currentOpenSuccessfullyNotified = true;
            } else {
                // Error: Publish error event with status code and response body
                String errorData = String::format("{\"status\": %d, \"body\": \"%s\"}", 
                                                 response.status, 
                                                 response.body.c_str());
                Particle.publish(EVENT_ERROR, errorData, PRIVATE);
                Serial.print("Error event published: ");
                Serial.println(errorData);
            }

            // Reset the state to avoid repeated events until the switch closes again
            isOpen = false;
        }
    } else {
        currentOpenSuccessfullyNotified = false;
        // Switch is closed (LOW), reset the open state
        if (isOpen) {
            isOpen = false;
            digitalWrite(BUILT_IN_LED_PIN, LOW);
            ledBlinkState = LOW;
            Serial.println("Reed switch closed");
        }
    }

    if (millis() - lastNoopNotify >= NOOP_NOTIFY_PERIOD || !noopNotified) {
        Serial.println("Doing no-op");

            // Send HTTP POST to Home Assistant webhook
            http.post(noop_request, response, headers);
            Serial.print("HTTP Response: ");
            Serial.println(response.status);
            Serial.println(response.body);
            
            lastNoopNotify = millis();
            
            // Publish event to Particle Cloud
            // Check HTTP response status
            if (response.status == 200) {
                noopNotified = true;
                // Success: Publish success event to Particle Cloud
                Particle.publish(EVENT_WEBHOOK_SUCCESS, "HA no-op webhook notified", PRIVATE);
                Serial.println("Success NO-OP event published to Home Assistant");
            } else {
                // Error: Publish error event with status code and response body
                String errorData = String::format("{\"status\": %d, \"body\": \"%s\"}", 
                                                 response.status, 
                                                 response.body.c_str());
                Particle.publish(EVENT_ERROR, errorData, PRIVATE);
                Serial.print("Error NO-OP event published: ");
                Serial.println(errorData);
            }
    }

    // Small delay to prevent excessive CPU usage
    delay(100);
}
