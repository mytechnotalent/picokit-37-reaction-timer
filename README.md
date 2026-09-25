![picokit-37-reaction-timer](https://raw.githubusercontent.com/mytechnotalent/picokit-37-reaction-timer/main/picokit-37-reaction-timer.png)

<br>

## FREE Reverse Engineering Self-Study Course [HERE](https://github.com/mytechnotalent/reverse-engineering)
## FREE Embedded Hacking Course [HERE](https://github.com/mytechnotalent/Embedded-Hacking)

<br>

# PICOKIT-37 REACTION TIMER

### Random-Light Reaction Game, Button Time, and an Authenticated Heartbeat
#### Lesson 37 of the Picokit Series

<br>

***
**LEGAL DISCLAIMER:**
The information, tools, and code provided in this repository and course are strictly for educational, research, and defensive purposes only.

You are explicitly prohibited from using any materials contained herein to access, test, modify, or exploit any device, network, or system that you do not own 100% or for which you do not have explicit, documented, and legally binding authorization to interact with.

By using this repository and course, you acknowledge and agree that:

1. Any illegal, unauthorized, or malicious use of this information is solely your responsibility.
2. The author(s) and contributor(s) of this repository and course shall not be held liable for any damages, legal repercussions, criminal charges, or unauthorized actions resulting from the use, misuse, or abuse of the contents herein.
3. You will comply with all applicable local, state, national, and international laws regarding cybersecurity and computer fraud.

**IF YOU DO NOT AGREE WITH THESE TERMS, DO NOT USE THIS REPOSITORY AND COURSE.**
***

<br>
<br>

## Overview

The thirty-seventh Picokit lesson. The node plays a reaction game: after a
random delay it lights the onboard LED, the player presses the button, and the
node shows the reaction time on the 1602 LCD. A missed press window is reported
as a miss, and every authenticated heartbeat carries the last reaction time.

<br>

## What it teaches

- A random arm delay drawn from the hardware random source.
- A lit press window with a debounced button and a miss timeout.
- Measuring and displaying the reaction time on the 1602 LCD.
- Reporting the reaction time in the authenticated heartbeat.

<br>

## Hardware

| Peripheral | Pico 2 pin | Role |
| --- | --- | --- |
| 1602 LCD | GP2 SDA / GP3 SCL | reaction time readout |
| Button | GP15 | reaction input |
| Red / Yellow / Green | GP16 / GP17 / GP18 | annunciator status |
| Onboard LED | GP25 | reaction light and heartbeat |
| RYLR998 | GP8 TX / GP9 RX | LoRa heartbeat |
| Debug Probe | SWCLK/SWDIO/GND, GP0/GP1 | SWD and the console |

<br>

## How it works

The node runs `monitor_step` in a loop. It arms a round with a random delay of
0.5 to 2.5 seconds, lights the onboard LED, and opens a 3 second press window.
A press records the reaction time and shows it on the LCD; an expired window
records a miss. Every 5 seconds the heartbeat body
`{"n":37,"s":<seq>,"m":<millis>}` is sealed with the field key and sent over
LoRa.

<br>

## Build and flash

```bash
cd firmware
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s
cmake --build build
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program build/picokit_37_reaction_timer.elf verify reset exit"
```

<br>

## Watch the node

Open the console at 115200 and reset:

```text
BOOT
I2C scan:
  found 0x27
=== PICOKIT-37 REACTION TIMER // RANDOM LIGHT + BUTTON TIME ===
REACTION 214 ms seq=1
REACTION 188 ms seq=2
RX from 0x0001, N bytes
```

<br>

## The gateway

```bash
cd gateway
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python3 listen.py --port /dev/cu.usbserial-A50285BI --hub 0001 --network 18 --db gateway.db
```

It prints `OK node=37 rssi=...` per authenticated heartbeat. The terminal
dashboard `python3 tui.py --db gateway.db` and the web dashboard
`python3 web/app.py --db gateway.db` show the same rows.

<br>

## Verify

```bash
python3 .opencode/skill/embedded-c-standard/audit_c_standard.py
python3 .opencode/skill/embedded-python-standard/audit_python_standard.py
python3 .opencode/skill/iot-readme-standard/validate_readme.py
python3 .opencode/skill/iot-banner-standard/validate_banner.py
python3 scripts/run_tests.py
python3 scripts/check_coverage.py
```

<br>

# Next
[picokit-38-traffic-controller](https://github.com/mytechnotalent/picokit-38-traffic-controller)

<br>

# License
[MIT License](https://github.com/mytechnotalent/picokit-37-reaction-timer/blob/main/LICENSE)
