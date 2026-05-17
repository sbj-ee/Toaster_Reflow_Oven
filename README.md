# Toaster Reflow Oven Controller

Arduino PID controller for a DIY SMD solder reflow oven built from a cheap toaster oven.

![Toaster reflow oven](reflow_oven.png)

## Hardware

| Component | Notes |
|-----------|-------|
| Arduino Uno (or compatible) | Main controller |
| Adafruit MAX31855 thermocouple breakout | K-type thermocouple, SPI |
| Solid State Relay (SSR) | Controls oven heating element |
| SSD1306 OLED display | 128×32, I2C |
| Passive buzzer | Startup beeps and completion alert |
| 5× LEDs | Status indicators for each reflow stage |

## Pin Assignments

| Pin | Function |
|-----|----------|
| 3 | MAX31855 DO (MISO) |
| 4 | MAX31855 CS |
| 5 | MAX31855 CLK |
| 6 | SSR |
| 8 | Error LED |
| 9 | Buzzer |
| 10 | Preheat LED |
| 11 | Soak LED |
| 12 | Reflow LED |
| 13 | Cooling LED |
| A4 (SDA) | SSD1306 display |
| A5 (SCL) | SSD1306 display |

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

## State Machine

```
IDLE → PREHEAT → SOAK → REFLOW → COOL → COMPLETE → IDLE
                                              ↑
         TOO_HOT (oven above 50°C at start) ──┘
         ERROR   (thermocouple fault)        ──┘
```

The controller starts a reflow cycle automatically when powered on and the oven temperature is below 50 °C. If the oven is too warm at startup it waits in `TOO_HOT` until it cools.

## Display

The SSD1306 shows a two-row layout updated every second:

```
PREHEAT         45s
185/200C
```

- Top row: current state (left) and elapsed seconds (right, only while running)
- Bottom row: measured temperature / setpoint in °C. Shows `ERR` if the thermocouple is disconnected.

## Serial Output

Connect at **57600 baud**. While a cycle is running, one CSV-style line is printed per second:

```
Time Setpoint Input Output
1 100.00 23.45 1800.00
2 100.00 41.20 1950.00
...
```

Paste the output into a spreadsheet or use the Arduino Serial Plotter to visualise the temperature profile.
