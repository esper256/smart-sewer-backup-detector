#include "Particle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

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

static void go_sewerBackup() {
    Host::pin[D2] = HIGH;
    spin(2100);
}

static void go_clear() {
    Host::pin[D2] = LOW;
    loop();
}

static const Host::HttpPost& last_post() {
    REQUIRE(!Host::posts.empty());
    return Host::posts.back();
}

static bool body_is(const Host::HttpPost& post, const char* state) {
    char expected[32];
    std::snprintf(expected, sizeof(expected), "{\"state\":\"%s\"}", state);
    return post.body == expected;
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

static std::vector<std::string> particle_states() {
    std::vector<std::string> out;
    for (const auto& e : Host::cloud) {
        if (e.event == "sewer-state") {
            out.push_back(e.data);
        }
    }
    return out;
}

static void require_ha(std::initializer_list<const char*> states) {
    REQUIRE(Host::posts.size() == states.size());
    size_t i = 0;
    for (const char* state : states) {
        REQUIRE(body_is(Host::posts[i], state));
        i++;
    }
}

static void require_particle(std::initializer_list<const char*> states) {
    const auto got = particle_states();
    REQUIRE(got.size() == states.size());
    size_t i = 0;
    for (const char* state : states) {
        REQUIRE(got[i] == state);
        i++;
    }
}

static void test_boot_posts_off() {
    boot_dry();
    require_ha({"OFF"});
    REQUIRE(last_post().port == 8123);
    REQUIRE(last_post().path == "/api/webhook/replacemewithyourwebhookid");
    require_particle({"OFF"});
    REQUIRE(cloud_count("sewer-ha") == 0);
    std::puts("ok  boot posts OFF");
}

static void test_short_open_stays_dry() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(1500);
    require_ha({"OFF"});
    require_particle({"OFF"});
    std::puts("ok  open < 2s stays OFF");
}

static void test_open_debounce_then_clear() {
    boot_dry();
    go_sewerBackup();
    go_clear();
    require_ha({"OFF", "ON", "OFF"});
    require_particle({"OFF", "ON", "OFF"});
    std::puts("ok  2s open posts ON, close posts OFF");
}

static void test_three_sewerBackup_cycles() {
    boot_dry();
    go_sewerBackup();
    go_clear();
    go_sewerBackup();
    go_clear();
    go_sewerBackup();
    go_clear();
    require_ha({"OFF", "ON", "OFF", "ON", "OFF", "ON", "OFF"});
    require_particle({"OFF", "ON", "OFF", "ON", "OFF", "ON", "OFF"});
    std::puts("ok  three sewerBackup/clear cycles");
}

static void test_debounce_restarts_after_cancel() {
    boot_dry();
    Host::pin[D2] = HIGH;
    spin(1500);
    go_clear();
    Host::pin[D2] = HIGH;
    spin(1500);
    require_ha({"OFF"});
    require_particle({"OFF"});

    spin(700);
    require_ha({"OFF", "ON"});
    require_particle({"OFF", "ON"});
    std::puts("ok  cancelled debounce does not count toward sewerBackup");
}

static void test_heartbeat() {
    boot_dry();
    spin(60000);
    require_ha({"OFF", "OFF"});
    require_particle({"OFF"});
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
    REQUIRE(Host::cloud[0].event == "sewer-ha");
    REQUIRE(Host::cloud[1].event == "sewer-state");

    spin(4000);
    REQUIRE(Host::posts.size() == 1);
    REQUIRE(cloud_count("sewer-ha") == 1);

    Host::http_status = 200;
    spin(2000);
    require_ha({"OFF", "OFF"});
    require_particle({"OFF"});
    REQUIRE(cloud_count("sewer-ha") == 2);
    REQUIRE(last_cloud_is("sewer-ha", "ok"));
    std::puts("ok  failed POST retries after 5s");
}

static void test_ha_fail_on_second_sewerBackup_then_clear() {
    boot_dry();
    go_sewerBackup();
    go_clear();
    require_ha({"OFF", "ON", "OFF"});

    Host::http_status = 500;
    go_sewerBackup();
    REQUIRE(body_is(last_post(), "ON"));
    REQUIRE(cloud_count("sewer-ha") == 1);
    require_particle({"OFF", "ON", "OFF", "ON"});

    Host::http_status = 200;
    spin(5100);
    require_ha({"OFF", "ON", "OFF", "ON", "ON"});
    REQUIRE(last_cloud_is("sewer-ha", "ok"));

    go_clear();
    require_ha({"OFF", "ON", "OFF", "ON", "ON", "OFF"});
    require_particle({"OFF", "ON", "OFF", "ON", "OFF"});
    std::puts("ok  HA fail on later sewerBackup, then clear");
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
    require_ha({"OFF"});
    require_particle({"OFF"});
    REQUIRE(cloud_count("sewer-ha") == 0);
    std::puts("ok  no POST until Wi-Fi is ready");
}

int main() {
    test_boot_posts_off();
    test_short_open_stays_dry();
    test_open_debounce_then_clear();
    test_three_sewerBackup_cycles();
    test_debounce_restarts_after_cancel();
    test_heartbeat();
    test_http_retry();
    test_ha_fail_on_second_sewerBackup_then_clear();
    test_wifi_down();
    std::puts("all host firmware tests passed");
    return 0;
}
