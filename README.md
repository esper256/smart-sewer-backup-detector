# Sewer Backup Detector

Particle Photon 2 and a magnetic float switch mounted through a sewer cleanout cap. The Photon publishes the switch state to an MQTT broker on the LAN. Home Assistant notifies you if the float rises, or if the device stops reporting.

Built July 2025. Hardware about $50–$75. Firmware: [`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino).

![Photon 2, IP68 box, 4-inch cleanout cap, and stainless float switch](images/entire_project.jpg)

## Why

A clogged sewer lateral fills from the low point of the pipe upward. The house is still draining into that volume. By the time a toilet will not flush, much of the piping may already be full. Opening the cleanout then releases that column of wastewater next to the house. The column is taller in a multi-story house.

A float in the cleanout can trip while the backup is still in the lateral and the riser, before it reaches fixtures. Warning time is the unused pipe volume between the float and the lowest fixture, divided by how fast water is entering the sewer. This does not clear a clog.

The float hangs in the **vertical cleanout riser**. It must not extend into the horizontal lateral. A float sitting in the flow path will catch paper and roots and can cause a blockage. Hang it as low as the riser allows, with the entire float still above the tee or wye into the lateral. Measure that depth before buying the stem.

```
        cleanout cap
              |
     vertical riser      float stays in this bore
              |
    ==========+==========  lateral
                         do not hang the float into this pipe
```

The Photon reports switch state. Home Assistant sends the alerts:

1. Float up: stop draining water.
2. No MQTT traffic: the detector lost power, Wi-Fi, or the broker.

If Home Assistant or phone notifications are down, you will not get either alert. Confirm that path separately; it is not part of this project.

```mermaid
flowchart LR
  lateral[Sewer lateral] -->|level rises| float[Float switch]
  float --> photon[Photon 2]
  photon -->|LAN MQTT| broker[Mosquitto]
  broker --> ha[Home Assistant]
  ha --> phone[Phone]
```

## Parts and cost

| Item | What I used | ≈ |
|---|---|---:|
| Photon 2 | Particle Photon 2 | $18 |
| Float | Stainless vertical magnetic reed float. Stem long enough to sit low in *your* riser without entering the lateral. | $8–15 |
| Cleanout cap | New 4" threaded PVC/ABS cap matching the existing cleanout | $5–15 |
| Enclosure | Helunsi IP68 junction box, Amazon `B07TGHYQF4` | $5–10 |
| Antenna | Dual-band paddle + U.FL pigtail, Amazon `B07R21LN5P` | $5–10 |
| Connectors | WAGO 221 at the cap so the cap still unscrews | $1–3 |
| Headers / terminals | Soldered onto the Photon or a small proto board | $2–5 |
| Debug LED | On D7 (soldered on this build) | <$1 |
| Wire | 2-conductor from float to box | <$2 |
| Power | USB 5 V wall wart into the Photon’s USB jack | $5–10 |
| **Total** | | **$50–$75** |

Tools: drill and a bit matching the float gland, wire strippers, soldering iron, screwdriver. No custom PCB or 3D printing.

## What you need already

- A sewer cleanout you can open, and permission to modify a replacement cap.
- Ability to work around sewage and sewer gas.
- Wi-Fi that reaches the cleanout, and the ability to flash a Particle board.
- Home Assistant and an MQTT broker on the LAN (the Mosquitto add-on is enough).
- A disconnect at the cap so the cleanout can still be opened for snaking. Do not hard-wire the cap to the electronics box.

WAGO 221 connectors are convenient under a roof overhang. They are not a weatherproof outdoor connector.

## Build

1. Measure the riser inside diameter and the depth from the underside of the cap to the lateral opening. Order a vertical float whose stem puts the float near the bottom of that riser and fully above the lateral.
2. Drill the replacement cap on center. Mount the float with its gaskets so the cap still seals. Confirm the float slides freely and does not enter the lateral when the cap is tight.
3. Wire the reed **D2–GND**. D2 is `INPUT_PULLUP`. Closed reed (dry) reads LOW. Open reed (float up, or a cut wire) reads HIGH. Debug LED on **D7** (soldered on this build). Do not hold D7 low at reset; that puts a Photon 2 into test mode.
4. Put the Photon in the IP68 box. USB power through a gland. Use the U.FL pigtail and an external antenna if the internal antenna is marginal at the cleanout. Fitting the board, glands, and antenna in the small box was the slowest mechanical step.

   ![Photon 2 in the IP68 box with U.FL pigtail and sensor wires](images/finished_electronics_box_closeup.jpg)

5. Run Mosquitto on the LAN. Create a broker user. Point Home Assistant’s MQTT integration at it. Do not expose port 1883.
6. Copy [`firmware/secrets.example.h`](firmware/secrets.example.h) to `firmware/secrets.h`. Fill in broker IP, username, and password. `secrets.h` is gitignored.
7. Flash Device OS 5.3 or later. In Particle Web IDE: new app, add the `.ino` and `secrets.h`, add the **MQTT** library (hirotakaster), flash OTA. Particle Cloud is used for OTA. Telemetry stays on the LAN.
8. Install [`home-assistant/sewer.yaml`](home-assistant/sewer.yaml) as a package. Replace `notify.notify` with your phone notify service.
9. Join the float leads with WAGOs or a weatherproof disconnect so you can unplug and unscrew the cap.
10. Test by lifting the float for more than 2 seconds. `binary_sensor.sewer_cleanout_float` should go on and the backup notification should fire. Unplug the Photon; within 5 minutes the silent-detector notification should fire. Periodic MQTT messages do not prove the float still moves. Lift it occasionally.

```
USB 5V ── Photon 2 USB
Reed ──── D2   (INPUT_PULLUP)
Reed ──── GND
LED  ──── D7   (debug; soldered on this build)
LED  ──── GND  (series resistor unless the LED is a module)
```

## Firmware

[`firmware/sewer-backup-detector.ino`](firmware/sewer-backup-detector.ino) and [`firmware/secrets.example.h`](firmware/secrets.example.h).

| Topic | Payload | When |
|---|---|---|
| `sewer/reed` (retain) | `OPEN` / `CLOSED` | 2 s after the reed opens; immediately on close; every 60 s either way |
| `sewer/availability` (retain) | `online` | MQTT connect |
| `sewer/availability` (LWT) | `offline` | Unclean disconnect |

The 2 s delay is reed debounce. Closed is reported immediately.

MQTT keepalive is 60 s. The library default is 15 s, which is short enough that Mosquitto will drop the client. `mqtt.loop()` runs every pass. The hardware watchdog is 90 s.

Broker credentials belong in `secrets.h` only:

```cpp
const uint8_t MQTT_BROKER_IP[] = {192, 168, 0, 2};
const uint16_t MQTT_BROKER_PORT = 1883;
const char* MQTT_USERNAME = "replacemewithyourusername";
const char* MQTT_PASSWORD = "replacemewithyourpassword";
```

MQTT on the LAN is unencrypted. Do not route it through the internet.

## Failure modes

| Failure | What happens | What to do |
|---|---|---|
| Float jammed with debris | Stays `CLOSED` | Inspect after every trip. Lift-test on a schedule. |
| Float hanging in the lateral | Can snag debris and clog the line | Keep the float entirely in the vertical riser. |
| Cut sensor wire | Reports `OPEN` | Same signal as a real backup. A different circuit is required to tell them apart. |
| Power, Wi-Fi, or Photon down | LWT `offline` and/or HA `expire_after` 180 s | Silent-detector alert after 5 min. |
| Mosquitto, HA, or phone notify down | No remote alert | This project cannot detect that. |
| MQTT connect hang | Loop stops | Hardware watchdog resets after 90 s. |

Opening a sanitary sewer exposes you to pathogens and hydrogen sulfide. Keep the cap sealed. Follow local plumbing code.

## Later changes worth making

- Weatherproof disconnect at the cap instead of WAGOs, if the cleanout is exposed.
- Supervised loop (two resistors) so an open wire, a raised float, and a short are three different states.
- A local sounder that does not depend on Home Assistant.
- MQTT TLS if the broker is not on a trusted LAN.

License: MIT. Early warning only; not a life-safety device.
