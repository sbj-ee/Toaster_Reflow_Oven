# Toaster Reflow Oven Controller

Arduino PID controller for a DIY SMD solder reflow oven built from a cheap toaster oven.

![Toaster reflow oven](reflow_oven.png)

## Hardware

| Component | Notes |
|-----------|-------|
| Arduino Uno (or compatible) | Main controller |
| Adafruit MAX31855 thermocouple breakout | K-type thermocouple, SPI |
| Solid State Relay (SSR) | Controls oven heating element |
| SSD1306 OLED display | 128×64, I2C |
| Passive buzzer | Startup beeps and completion alert |
| 5× LEDs | Status indicators for each reflow stage |

## Pin Assignments

| Pin | Function |
|-----|----------|
| 3 | MAX31855 DO (MISO) |
| 4 | MAX31855 CS |
| 5 | MAX31855 CLK |
| 6 | SSR |
| 7 | Start button (to GND, no resistor needed) |
| 8 | Error LED |
| 9 | Buzzer |
| 10 | Preheat LED |
| 11 | Soak LED |
| 12 | Reflow LED |
| 13 | Cooling LED |
| A4 (SDA) | SSD1306 display |
| A5 (SCL) | SSD1306 display |

## Wiring

```
                       ┌──────────────────────┐
     MAX31855 DO  ─────┤ D3              3V3  ├──── MAX31855 VCC
     MAX31855 CS  ─────┤ D4              GND  ├──── Common GND
    MAX31855 CLK  ─────┤ D5                   │
            SSR   ─────┤ D6               A4  ├──── SSD1306 SDA
   Start button   ─────┤ D7               A5  ├──── SSD1306 SCL
      Error LED   ─────┤ D8              VCC  ├──── SSD1306 VCC
         Buzzer   ─────┤ D9                   │
     Preheat LED  ─────┤ D10                  │
        Soak LED  ─────┤ D11                  │
      Reflow LED  ─────┤ D12                  │
     Cooling LED  ─────┤ D13                  │
                       └──────────────────────┘
                             Arduino Uno
```

**LEDs** — each LED requires a 220 Ω current-limiting resistor in series to GND.

**Start button** — one leg to D7, other leg to GND. No resistor needed (`INPUT_PULLUP`).

**MAX31855** — VCC to 3V3, GND to GND, DO→D3, CS→D4, CLK→D5. Connect the K-type thermocouple to the screw terminals.

**SSD1306** — VCC to 5V (or 3V3 depending on your module), GND to GND, SDA→A4, SCL→A5. Default I2C address is `0x3C`; change `OLED_ADDRESS` in the sketch if yours is `0x3D`.

**SSR** — control input to D6 and GND. The SSR switches mains power to the oven heating element; follow all appropriate safety precautions when wiring mains voltage.

## Libraries Required

Install all four via Arduino Library Manager:

- **Adafruit MAX31855** — thermocouple driver
- **Adafruit SSD1306** — OLED display driver
- **Adafruit GFX** — graphics primitives (dependency of SSD1306)
- **PID** by Brett Beauregard — PID control loop

## Solder Profile Configuration

Two profiles are defined in the source. Only one set of `TEMPERATURE_*` defines should be active at a time.

**Non-RoHS (leaded, currently active):**
```
Soak:    100 – 150 °C
Reflow:  250 °C peak
```

**RoHS (lead-free):** uncomment the `///// RoHS Values` block and comment out the non-RoHS block:
```
Soak:    150 – 200 °C
Reflow:  250 °C peak
```

## Usage

1. Power on — the display shows `READY` and the oven waits.
2. If the oven is above 50 °C the display shows `TOO HOT`; wait for it to cool.
3. Press and hold the start button (pin 7 to GND) to begin the reflow cycle.
4. The buzzer sounds and the display shows the active stage, live temperature, and elapsed time.
5. At the end of the cool-down phase the buzzer sounds again and the oven returns to `READY`.

## State Machine

```
READY → PREHEAT → SOAK → REFLOW → COOL → COMPLETE → READY
                                               ↑
          TOO_HOT (oven above 50°C at start) ──┘
          ERROR   (thermocouple fault)        ──┘
```

The cycle only starts when the start button is pressed while the display shows `READY`. If the oven is too warm at startup it waits in `TOO HOT` until it cools.

## Display

The SSD1306 shows a three-section layout updated every second:

```
PREHEAT         45s
185/200C
[==========|------|---------]
  PRE        SOAK      RFLO
```

- Top row: current state (left) and elapsed seconds (right, only while running)
- Middle row: measured temperature / setpoint in °C. Shows `ERR` if the thermocouple is disconnected.
- Bar: temperature progress from 0 °C to the reflow peak (250 °C). Two tick marks divide the bar at the soak-min (100 °C) and soak-max (150 °C) boundaries. The fill grows as the oven heats and shrinks during cooling.
- Labels: `PRE` / `SOAK` / `RFLO` centred under each zone.

## Serial Output

Connect at **57600 baud**. While a cycle is running, one CSV-style line is printed per second:

```
Time Setpoint Input Output
1 100.00 23.45 1800.00
2 100.00 41.20 1950.00
...
```

Paste the output into a spreadsheet or use the Arduino Serial Plotter to visualise the temperature profile.
