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

static bool body_is(const Host::HttpPost& post, const char* reed) {
    char expected[32];
    std::snprintf(expected, sizeof(expected), "{\"reed\":\"%s\"}", reed);
    return post.body == expected;
}

static void test_boot_posts_closed() {
    boot_dry();
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "CLOSED"));
    REQUIRE(last_post().port == 8123);
    REQUIRE(last_post().path == "/api/webhook/replacemewithyourwebhookid");
    std::puts("ok  boot posts CLOSED");
}

static void test_short_open_does_not_post() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(1500);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "CLOSED"));
    std::puts("ok  open < 2s stays CLOSED");
}

static void test_open_debounce_then_close() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(2100);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "OPEN"));

    Host::pin[D2] = LOW;
    loop();
    REQUIRE(Host::posts.size() == 3);
    REQUIRE(body_is(last_post(), "CLOSED"));
    std::puts("ok  2s open posts OPEN, close posts CLOSED");
}

static void test_heartbeat() {
    boot_dry();
    spin(60000);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "CLOSED"));
    std::puts("ok  60s heartbeat");
}

static void test_http_retry() {
    Host::reset();
    Host::pin[D2] = LOW;
    Host::http_status = 500;
    setup();
    loop();
    REQUIRE(Host::posts.size() == 1);

    spin(4000);
    REQUIRE(Host::posts.size() == 1);

    Host::http_status = 200;
    spin(2000);
    REQUIRE(Host::posts.size() == 2);
    REQUIRE(body_is(last_post(), "CLOSED"));
    std::puts("ok  failed POST retries after 5s");
}

static void test_wifi_down() {
    Host::reset();
    Host::wifi_ready = false;
    setup();
    loop();
    REQUIRE(Host::posts.empty());

    Host::wifi_ready = true;
    spin(5100);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(body_is(last_post(), "CLOSED"));
    std::puts("ok  no POST until Wi-Fi is ready");
}

int main() {
    test_boot_posts_closed();
    test_short_open_does_not_post();
    test_open_debounce_then_close();
    test_heartbeat();
    test_http_retry();
    test_wifi_down();
    std::puts("all host firmware tests passed");
    return 0;
}
