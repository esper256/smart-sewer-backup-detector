# Sewer Backup Detector

A Particle Photon 2 plus a reed float in a sewer cleanout cap. When wastewater starts climbing the cleanout, Home Assistant gets an HTTP POST. You stop draining water before the house plumbing fills.

Built July 2025. Hardware ≈ $50–$75. Firmware: [`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino).

![Photon 2, IP68 box, 4-inch cleanout cap, and stainless float switch](images/entire_project.jpg)

## Why

A clogged lateral fills from the low point up. Toilets and showers then dump into a closed volume. If you unscrew the cleanout after that volume is full, you get a vertical column of sewage at the cap — worse on a multi-story house.

Put the float as low in the cleanout riser as the stem and pipe geometry allow. Earlier trip ⇒ more time to stop using water ⇒ less head at the cap when you open it.

This is a warning, not a pump. Warning time is whatever unused pipe volume sits between the float and your lowest fixture, divided by the household drain rate.

```mermaid
flowchart LR
  lateral[Sewer lateral] -->|level rises| float[Float / reed]
  float --> photon[Photon 2]
  photon -->|LAN HTTP POST| ha[Home Assistant]
  ha --> phone[Phone]
```

A float switch is the right sensor: the question is binary (is this normally empty pipe now wet?) and the stem length sets the trip height. Conductivity probes, ultrasonics, and pressure sensors add failure modes without answering a better question.

## Cost, parts, skills

| Item | What I used | ≈ |
|---|---|---:|
| Photon 2 | Particle Photon 2 | $18 |
| Float | Stainless vertical magnetic reed float, stem long enough to hang near the bottom of *your* riser | $8–15 |
| Cleanout cap | New 4" threaded PVC/ABS cap matching the existing cleanout | $5–15 |
| Enclosure | Helunsi IP68 junction box, Amazon `B07TGHYQF4` | $5–10 |
| Antenna | Dual-band paddle + U.FL pigtail, Amazon `B07R21LN5P` | $5–10 |
| Connectors | WAGO 221 at the cap (so the cap still unscrews) | $1–3 |
| Headers / terminals | Soldered onto the Photon or a small proto board | $2–5 |
| Wire | 2-conductor from float to box | <$2 |
| Power | USB 5 V wall wart into the Photon’s USB jack | $5–10 |
| **Total** | | **$50–$75** |

Tools: drill and bit sized to the float’s gland, wire strippers, soldering iron, screwdriver. No PCB, no printer, no lathe.

**Walk away if you cannot do all of these:**

- Access a sewer cleanout and legally modify its cap.
- Work around sewage and sewer gas without turning it into a hazard for the house.
- Flash a Particle board and keep it on Wi-Fi that actually reaches the cleanout.
- Already run Home Assistant (or another HTTP endpoint on the LAN). This project does not include “also learn home automation from scratch.”
- Leave the cleanout serviceable: the cap must still come off in a backup. Permanent wiring through the cap is a failure.

If the cleanout is unsheltered, WAGO 221s are the wrong outdoor connector. Mine sit under a house overhang.

## Build

1. Measure the cleanout riser (ID and depth). Buy a float whose stem hangs the float low without scraping the wall or blocking a snake.
2. Drill the replacement cap on center. Mount the float with its gaskets so the cap still seals.
3. Wire the reed **D2–GND**. Photon pin D2 is `INPUT_PULLUP`. Closed reed (dry) = LOW; open reed (float up, or cut wire) = HIGH.
4. Put the Photon in the IP68 box. USB power through a gland. Optional: U.FL to an external antenna if the internal antenna is marginal. Fit is tight; that was the hardest mechanical step.

   ![Photon 2 in the IP68 box with U.FL pigtail and sensor wires](images/finished_electronics_box_closeup.jpg)

5. Create two Home Assistant webhook automations ([`home-assistant/automations.yaml`](home-assistant/automations.yaml)). Copy the IDs into the firmware. Do not commit them.
6. Flash Device OS 5.3+ firmware. In Particle Web IDE: new app, paste [`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino), add the **HttpClient** library, set `HA_HOST` and the two webhook paths, flash OTA.
7. Join the float leads with WAGOs (or a real weatherproof disconnect) so you can unplug and unscrew the cap for snaking.
8. Test by lifting the float for >2 s. Confirm: Photon logs the event, HA fires the alarm, phone buzzes. Then confirm the heartbeat path once. A 28-day HTTP heartbeat proves power + Wi-Fi + HA. It does **not** prove the float still moves. Lift it on a schedule.

```
USB 5V ── Photon 2 USB
Reed ──── D2   (INPUT_PULLUP)
Reed ──── GND
```

## Firmware

The Photon does four things:

1. Debounce the reed: alarm only after **2 s** continuous open.
2. POST the alarm webhook. Retry every **5 s** until HA returns HTTP 200, then latch until the reed closes.
3. Also `Particle.publish` the alarm so you still have a cloud event if the LAN POST fails.
4. POST a heartbeat webhook every **28 days** (and at boot). HA should nag you if that stops.

Fill in:

```cpp
const char* HA_HOST = "192.168.0.2";
const int HA_PORT = 8123;
const char* HA_ALARM_PATH = "/api/webhook/YOUR_ALARM_WEBHOOK_ID";
const char* HA_HEARTBEAT_PATH = "/api/webhook/YOUR_HEARTBEAT_WEBHOOK_ID";
```

HttpClient talks **HTTP**, not HTTPS. Point it at HA’s LAN IP on port 8123. Stock HttpClient always uses `request.hostname` (the `request.ip` field is dead code because `String` is never NULL), so a dotted-quad in `HA_HOST` is the working setup.

Photon 2 has an RGB status LED, not a D7 user LED. `D7` in firmware is leftover GPIO; wire a real LED there if you want a local blink.

Serial logs: `particle serial monitor` after USB connect. `Log.info` at 115200.

## Failure modes

| Failure | What happens | Mitigation |
|---|---|---|
| Float jammed with debris | No alarm | Inspect after every trip; periodic lift test |
| Cut sensor wire | Looks like an alarm (fail-safe) | You cannot tell “backup” from “broken wire” without a different circuit |
| Power / Wi-Fi / HA down | No remote alert | 28-day heartbeat catches *sustained* silence, not a 2-hour outage during a clog |
| HA unreachable during a clog | LAN POST fails; Particle Cloud event still fires if cellular/Wi-Fi cloud is up | Watch the Particle event stream as a backup |
| HttpClient hang | Loop stops | Hardware watchdog resets after 60 s |

Opening a sanitary sewer can expose you to pathogens and H₂S. Keep the cap sealed. Follow local plumbing code.

## v2

What I would change after running this:

- Replace WAGOs with a small weatherproof disconnect at the cap.
- Supervised loop (two resistors, analog or window-comparator) so open-wire ≠ float-up ≠ short.
- Local sounder that does not depend on HA.
- MQTT to HA instead of HttpClient. HttpClient is blocking; Particle themselves do not recommend it for production. Watchdog + timeout make it tolerable, not elegant.
- Do not hard-code webhook IDs; use a `Particle.function` or a config file you never commit.
- `millis()` wraps at ~49.7 days. The 28-day heartbeat still fires, but the first post-wrap interval can be skewed. Use `System.millis()` / a RTC if you care.
- Keep OTA: that is why the Photon 2 is here. Once the box is outside, you will not want to open it to change debounce times.

License: MIT. This is an early-warning gadget, not a life-safety system.
