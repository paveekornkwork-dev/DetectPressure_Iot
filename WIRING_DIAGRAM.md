# 🔌 Wiring Diagram — Pressure Monitoring System

## Components

| # | Component | Description |
|---|-----------|-------------|
| 1 | **ESP32 Dev Board** | 30-pin or 38-pin DevKit V1 |
| 2 | **RFP-602 Thin Film Pressure Sensor** | 0–5 kg capacity, resistance decreases with applied force |
| 3 | **Film Pressure Sensor Conversion Module** | Converts sensor resistance to analog voltage (0–3.3 V) and digital HIGH/LOW output |

---

## Pin-to-Pin Wiring

### 1. RFP-602 Sensor → Conversion Module

The RFP-602 has **2 wires** (no polarity). Connect them to the **sensor input terminals** of the conversion module.

```
RFP-602 Wire A  ───────►  Conversion Module [S+]
RFP-602 Wire B  ───────►  Conversion Module [S-]
```

> **Note:** The RFP-602 is a resistive sensor with no polarity — either wire can go to either terminal.

---

### 2. Conversion Module → ESP32

The conversion module has **4 output pins**: `VCC`, `GND`, `AO` (Analog Out), `DO` (Digital Out).

```
┌──────────────────────┐          ┌────────────────────┐
│  Conversion Module   │          │       ESP32        │
│                      │          │                    │
│   VCC ───────────────┼─────────►│   3V3 (3.3V)       │
│   GND ───────────────┼─────────►│   GND              │
│   AO  ───────────────┼─────────►│   GPIO 34 (ADC1_6) │
│   DO  ───────────────┼─────────►│   GPIO 35 (ADC1_7) │
│                      │          │                    │
└──────────────────────┘          └────────────────────┘
```

| Module Pin | ESP32 Pin | Purpose |
|------------|-----------|---------|
| **VCC** | **3V3** | Power supply (3.3 V — do **NOT** use 5 V) |
| **GND** | **GND** | Common ground |
| **AO** | **GPIO 34** | Analog pressure reading (ADC input, read-only pin) |
| **DO** | **GPIO 35** | Digital threshold output (optional, read-only pin) |

---

### 3. LED Indicators → ESP32

| LED | ESP32 Pin | Purpose |
|-----|-----------|---------|
| 🔵 **Blue LED** (WiFi Status) | **GPIO 18** | ON = WiFi connected, OFF = disconnected |
| 🟡 **Press LED** (Pressure) | **GPIO 19** | ON = pressure detected, OFF = idle |

> Connect each LED with a **220Ω resistor** in series: `ESP32 GPIO → 220Ω → LED (+) → LED (-) → GND`

---

## Important Notes

> [!CAUTION]
> - **Use 3.3 V only.** The ESP32 ADC tolerates a maximum of 3.3 V. Supplying 5 V to AO may permanently damage the ESP32.
> - **GPIO 34 and 35** are input-only pins on ESP32 — perfect for sensor readings.
> - Ensure all grounds are connected together (common ground).

> [!TIP]
> - The conversion module usually has an **on-board potentiometer**. This sets the digital output (DO) threshold. Adjust it with a small screwdriver to your desired trigger point.
> - The **AO** output gives a continuous voltage proportional to the applied pressure, which is what the code primarily uses.
> - For a stable power supply, add a **100 µF decoupling capacitor** between VCC and GND near the conversion module.

---

## Physical Layout

```
                ┌─────────────┐
                │   RFP-602   │
                │  (on bed /  │
                │  surface)   │
                └──┬─────┬────┘
                   │     │  (2 wires, no polarity)
            ┌──────┴─────┴──────┐
            │  Conversion Module │
            │  ┌──────────────┐ │
            │  │ Potentiometer│ │  ← Adjust DO threshold
            │  └──────────────┘ │
            │ VCC GND AO DO    │
            └──┬──┬───┬───┬────┘
               │  │   │   │
               │  │   │   └───── GPIO 35 (optional digital)
               │  │   └──────── GPIO 34 (analog read)
               │  └──────────── GND
               └─────────────── 3V3
            ┌──┴──┴───┴───┴────┐
            │                   │
            │      ESP32        │
            │   (DevKit V1)     │
            │                   │
            │   [USB] ← Power   │
            │         + Serial  │
            └───────────────────┘
```
