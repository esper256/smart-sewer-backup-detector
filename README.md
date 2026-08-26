# Sewer Backup Detector

A Particle Photon 2 plus a reed float in a sewer cleanout cap. The Photon publishes reed state over LAN MQTT. Home Assistant alerts you when the float is up, and when the device goes silent.

Built July 2025. Hardware ≈ $50–$75. Firmware: [`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino).

![Photon 2, IP68 box, 4-inch cleanout cap, and stainless float switch](images/entire_project.jpg)

## Why

A clogged lateral fills from the low point up. Toilets and showers then dump into a closed volume. If you unscrew the cleanout after that volume is full, you get a vertical column of sewage at the cap — worse on a multi-story house.

Put the float as low in the cleanout riser as the stem and pipe geometry allow. Earlier trip ⇒ more time to stop using water ⇒ less head at the cap when you open it.

This is a warning, not a pump. Warning time is unused pipe volume between the float and your lowest fixture, divided by the household drain rate.

```mermaid
flowchart LR
  lateral[Sewer lateral] -->|level rises| float[Float / reed]
  float --> photon[Photon 2]
  photon -->|LAN MQTT| broker[Mosquitto]
  broker --> ha[Home Assistant]
  ha --> phone[Phone]
```

A float switch is the right sensor: the question is binary (is this normally empty pipe now wet?) and the stem length sets the trip height.

The Photon is a sensor, not an alarm panel. It publishes `OPEN` / `CLOSED` on change (2 s debounce to `OPEN`) and every 60 s otherwise. HA owns two alerts:

1. Float is up → stop draining.
2. No timely MQTT traffic → the detector is dead.

Silence is not proof of health. If HA or your phone notify path is broken, both of those alerts fail closed. Keep some independent check that HA can still reach you — that is outside this project.

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
| Debug LED | On D7 (soldered on this build) | <$1 |
| Wire | 2-conductor from float to box | <$2 |
| Power | USB 5 V wall wart into the Photon’s USB jack | $5–10 |
| **Total** | | **$50–$75** |

Tools: drill and bit sized to the float’s gland, wire strippers, soldering iron, screwdriver. No PCB, no printer, no lathe.

**Walk away if you cannot do all of these:**

- Access a sewer cleanout and legally modify its cap.
- Work around sewage and sewer gas without turning it into a hazard for the house.
- Flash a Particle board and keep it on Wi-Fi that actually reaches the cleanout.
- Already run Home Assistant **and** an MQTT broker on the LAN (Mosquitto add-on is enough). This project does not include “also learn home automation from scratch.”
- Leave the cleanout serviceable: the cap must still come off in a backup. Permanent wiring through the cap is a failure.

If the cleanout is unsheltered, WAGO 221s are the wrong outdoor connector. Mine sit under a house overhang.

## Build

1. Measure the cleanout riser (ID and depth). Buy a float whose stem hangs the float low without scraping the wall or blocking a snake.
2. Drill the replacement cap on center. Mount the float with its gaskets so the cap still seals.
3. Wire the reed **D2–GND**. D2 is `INPUT_PULLUP`. Closed reed (dry) = LOW; open reed (float up, or cut wire) = HIGH. Debug LED on **D7** (this build has one soldered); do not hold D7 low at reset (Photon 2 test mode).
4. Put the Photon in the IP68 box. USB power through a gland. Optional: U.FL to an external antenna if the internal antenna is marginal. Fit is tight; that was the hardest mechanical step.

   ![Photon 2 in the IP68 box with U.FL pigtail and sensor wires](images/finished_electronics_box_closeup.jpg)

5. Run Mosquitto on the LAN. Create a broker user. Point HA’s MQTT integration at it. Do not expose port 1883.
6. Copy [`firmware/secrets.example.h`](firmware/secrets.example.h) → `firmware/secrets.h`. Fill in broker IP, username, password. `secrets.h` is gitignored.
7. Flash Device OS 5.3+. Particle Web IDE: new app, add both `.ino` and `secrets.h`, add the **MQTT** library (hirotakaster), flash OTA. Particle cloud is for OTA only; telemetry stays on the LAN.
8. Drop [`home-assistant/sewer.yaml`](home-assistant/sewer.yaml) in as a package. Replace `notify.notify` with your phone notify service.
9. Join the float leads with WAGOs (or a real weatherproof disconnect) so you can unplug and unscrew the cap for snaking.
10. Test: lift the float >2 s → HA `binary_sensor.sewer_cleanout_float` goes on and the backup notify fires. Unplug the Photon → within 5 minutes the silent-detector notify fires. Periodic MQTT publishes do **not** prove the float still moves. Lift it on a schedule.

```
USB 5V ── Photon 2 USB
Reed ──── D2   (INPUT_PULLUP)
Reed ──── GND
LED  ──── D7   (debug; soldered on this build)
LED  ──── GND  (resistor if the LED is not a module)
```

## Firmware

[`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino) plus [`firmware/secrets.example.h`](firmware/secrets.example.h).

| Topic | Payload | When |
|---|---|---|
| `sewer/reed` (retain) | `OPEN` / `CLOSED` | 2 s after the reed opens; immediately on close; every 60 s either way |
| `sewer/availability` (retain) | `online` | MQTT connect |
| `sewer/availability` (LWT) | `offline` | Unclean disconnect |

`OPEN` is debounced on the device so HA does not see reed bounce. Closed is immediate.

MQTT keepalive is 60 s (the library default of 15 s will drop you). `mqtt.loop()` runs every pass. Hardware watchdog is 60 s.

Secrets live only in `secrets.h`:

```cpp
const uint8_t MQTT_BROKER_IP[] = {192, 168, 0, 2};
const uint16_t MQTT_BROKER_PORT = 1883;
const char* MQTT_USERNAME = "replacemewithyourusername";
const char* MQTT_PASSWORD = "replacemewithyourpassword";
```

Plain MQTT on the LAN. No TLS, no Particle Cloud webhooks, no HTTP to HA.

## Failure modes

| Failure | What happens | Mitigation |
|---|---|---|
| Float jammed with debris | Stays `CLOSED` | Inspect after every trip; periodic lift test |
| Cut sensor wire | Reports `OPEN` (fail-safe) | You cannot tell “backup” from “broken wire” without a different circuit |
| Power / Wi-Fi / Photon down | LWT `offline` and/or HA `expire_after` 180 s | Silent-detector alert after 5 min |
| Mosquitto / HA / phone notify down | No remote alert | This detector cannot tell you that. Confirm HA notify separately. |
| MQTT connect hang | Loop stops | Hardware watchdog resets after 60 s |

Opening a sanitary sewer can expose you to pathogens and H₂S. Keep the cap sealed. Follow local plumbing code.

## v2

- Replace WAGOs with a small weatherproof disconnect at the cap.
- Supervised loop (two resistors) so open-wire ≠ float-up ≠ short.
- Local sounder that does not depend on HA.
- MQTT TLS if the broker is not on a trusted LAN segment.

License: MIT. This is an early-warning gadget, not a life-safety system.
