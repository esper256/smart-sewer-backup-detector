#include "Particle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

static void spin(unsigned long ms) {
    const unsigned long start = millis();
    while (millis() - start < ms) {
        loop();
    }
}

static void boot_dry() {
    Host::reset();
    Host::pin[D2] = LOW;
    setup();
    loop();
}

static const Host::HttpPost& last_post() {
    REQUIRE(!Host::posts.empty());
    return Host::posts.back();
}

static int cloud_count(const char* event) {
    int n = 0;
    for (const auto& e : Host::cloud) {
        if (e.event == event) {
            n++;
        }
    }
    return n;
}

static bool last_cloud_is(const char* event, const char* data) {
    for (int i = static_cast<int>(Host::cloud.size()) - 1; i >= 0; i--) {
        if (Host::cloud[static_cast<size_t>(i)].event == event) {
            return Host::cloud[static_cast<size_t>(i)].data == data;
        }
    }
    return false;
}

static bool body_is(const Host::HttpPost& post, const char* state) {
    char expected[32];
    std::snprintf(expected, sizeof(expected), "{\"state\":\"%s\"}", state);
    return post.body == expected;
}

static void test_boot_posts_ok() {
    boot_dry();
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(last_post().port == 8123);
    REQUIRE(last_post().path == "/api/webhook/replacemewithyourwebhookid");
    REQUIRE(cloud_count("sewer-alarm") == 1);
    REQUIRE(last_cloud_is("sewer-alarm", "OK"));
    REQUIRE(cloud_count("sewer-ha") == 0);
    std::puts("ok  boot posts OK");
}

static void test_short_open_does_not_alarm() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(1500);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(cloud_count("sewer-alarm") == 1);
    std::puts("ok  open < 2s stays OK");
}

static void test_open_debounce_then_clear() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(2100);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "ALARM"));

    Host::pin[D2] = LOW;
    loop();
    REQUIRE(Host::posts.size() == 3);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(cloud_count("sewer-alarm") == 3);
    REQUIRE(last_cloud_is("sewer-alarm", "OK"));
    std::puts("ok  2s open posts ALARM, close posts OK");
}

static void test_heartbeat() {
    boot_dry();
    spin(60000);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(cloud_count("sewer-alarm") == 1);
    REQUIRE(cloud_count("sewer-ha") == 0);
    std::puts("ok  60s heartbeat");
}

static void test_http_retry() {
    Host::reset();
    Host::pin[D2] = LOW;
    Host::http_status = 500;
    setup();
    loop();
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(cloud_count("sewer-ha") == 1);
    REQUIRE(last_cloud_is("sewer-ha", "HTTP 500"));

    spin(4000);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(cloud_count("sewer-ha") == 1);

    Host::http_status = 200;
    spin(2000);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(cloud_count("sewer-alarm") == 1);
    REQUIRE(cloud_count("sewer-ha") == 2);
    REQUIRE(last_cloud_is("sewer-ha", "ok"));
    std::puts("ok  failed POST retries after 5s");
}

static void test_wifi_down() {
    Host::reset();
    Host::wifi_ready = false;
    setup();
    loop();
    REQUIRE(Host::posts.empty());
    REQUIRE(Host::cloud.empty());

    Host::wifi_ready = true;
    spin(5100);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "OK"));
    REQUIRE(cloud_count("sewer-alarm") == 1);
    REQUIRE(cloud_count("sewer-ha") == 0);
    std::puts("ok  no POST until Wi-Fi is ready");
}

int main() {
    test_boot_posts_ok();
    test_short_open_does_not_alarm();
    test_open_debounce_then_clear();
    test_heartbeat();
    test_http_retry();
    test_wifi_down();
    std::puts("all host firmware tests passed");
    return 0;
}
