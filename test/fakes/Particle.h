#pragma once

// Host stand-ins for the Device OS APIs this firmware calls. Not for the board.

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std::chrono_literals;

using pin_t = uint16_t;

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT_PULLUP = 2;
constexpr pin_t D2 = 2;
constexpr pin_t D7 = 7;
constexpr int LOG_LEVEL_INFO = 0;

#define SYSTEM_THREAD(x)

namespace Host {
inline unsigned long millis_ms = 0;
inline uint8_t pin[16] = {};
inline bool wifi_ready = true;
inline int http_status = 200;

struct HttpPost {
    std::string host;
    int port;
    std::string path;
    std::string body;
};
inline std::vector<HttpPost> posts;

struct CloudEvent {
    std::string event;
    std::string data;
};
inline std::vector<CloudEvent> cloud;

inline void reset() {
    millis_ms = 0;
    std::fill(std::begin(pin), std::end(pin), LOW);
    wifi_ready = true;
    http_status = 200;
    posts.clear();
    cloud.clear();
}
}

class String {
    std::string s_;

public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}

    String& operator=(const char* s) {
        s_ = s ? s : "";
        return *this;
    }

    const char* c_str() const { return s_.c_str(); }

    String operator+(const String& other) const { return String(s_ + other.s_); }

    static String format(const char* fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return String(buf);
    }
};

class IPAddress {};

struct WiFiClass {
    bool ready() const { return Host::wifi_ready; }
};
[[maybe_unused]] inline WiFiClass WiFi;

struct Logger {
    void info(const char*, ...) const {}
    void warn(const char*, ...) const {}
};
[[maybe_unused]] inline Logger Log;

struct SystemClass {
    String deviceID() const { return String("testdevice"); }
};
[[maybe_unused]] inline SystemClass System;

struct WatchdogConfiguration {
    WatchdogConfiguration& timeout(std::chrono::milliseconds) { return *this; }
};

struct WatchdogClass {
    int init(const WatchdogConfiguration&) { return 0; }
    void start() {}
    void refresh() {}
};
[[maybe_unused]] inline WatchdogClass Watchdog;

struct SerialLogHandler {
    explicit SerialLogHandler(int) {}
};

constexpr int PRIVATE = 1;

struct ParticleClass {
    bool connected() const { return Host::wifi_ready; }
    bool publish(const char* event, const char* data, int = PRIVATE) {
        Host::cloud.push_back({event, data});
        return true;
    }
};
[[maybe_unused]] inline ParticleClass Particle;

inline unsigned long millis() { return Host::millis_ms; }
inline void delay(unsigned long ms) { Host::millis_ms += ms; }
inline void pinMode(pin_t, uint8_t) {}
inline int digitalRead(pin_t pin) { return Host::pin[pin]; }
inline void digitalWrite(pin_t pin, uint8_t value) { Host::pin[pin] = value; }

void setup();
void loop();
