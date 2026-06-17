# Digital Synchronous Buck-Boost Converter

High-efficiency four-switch synchronous Buck-Boost converter based on **STM32F103**, featuring digital CC/CV control, TFT user interface, rotary encoder adjustment, and shunt resistor current sensing with differential amplifier.

---

# Features

* Four-switch synchronous Buck-Boost topology
* Digital control using STM32F103
* Gate drivers based on IR2101S
* Power MOSFETs using IRF3205
* Constant Voltage (CV) mode
* Constant Current (CC) mode
* Center-aligned PWM
* Programmable deadtime
* Soft-start function
* SPI TFT display interface
* Rotary encoder parameter adjustment
* Output current measurement using shunt resistor
* Differential amplifier current sensing circuit
* ADC + DMA acquisition
* Over-current protection (OCP)
* Over-voltage protection (OVP)

---

# Hardware

## Microcontroller

* STM32F103C8T6

## Gate Driver

* 2 × IR2101S half-bridge drivers

## Power MOSFET

* 4 × IRF3205

## Inductor

* High-current power inductor

## User Interface

* SPI TFT display
* Rotary encoder

## Current Sensing

* Low-value shunt resistor
* Differential amplifier
* RC low-pass filter
* STM32 ADC

---

# System Architecture

```text
                    +----------------+
                    |   STM32F103    |
                    +----------------+
                     |             |
                PWM1 |             | PWM2
                     |             |
             +-------+-------------+-------+
             |         IR2101S Drivers      |
             +-------+-------------+-------+
                     |             |
                +----+-------------+----+
                |      4 × IRF3205       |
                +----+-------------+----+
                     |             |
                     +---- L ----+-+
                                 |
                               VOUT
                                 |
                             Shunt Resistor
                                 |
                       Differential Amplifier
                                 |
                         RC Low-pass Filter
                                 |
                              ADC DMA
```

---

# Current Measurement

Output current is measured through a low-value shunt resistor.

```text
Iout
 ↓
Rshunt
 ↓
Differential Amplifier
 ↓
RC Filter
 ↓
ADC
```

The differential amplifier converts the small shunt voltage into a suitable level for the STM32 ADC.

---

# Control Structure

## Voltage Loop

```text
Vref
 ↓
Voltage PI Controller
 ↓
Current Reference
```

## Current Loop

```text
Current Reference
 ↓
Current PI Controller
 ↓
PWM Duty Cycle
```

This dual-loop structure provides:

* Constant Voltage (CV) regulation
* Constant Current (CC) limiting
* Smooth transition between CC and CV modes

---

# User Interface

## TFT SPI Display

Display information:

* Input voltage
* Output voltage
* Output current
* Output power
* Duty cycle
* Operating mode (CC/CV)

## Rotary Encoder

Adjustable parameters:

* Output voltage setpoint
* Current limit
* Menu navigation

---

# Protection Functions

* Over Current Protection (OCP)
* Over Voltage Protection (OVP)
* Shoot-through prevention
* Soft-start
* Deadtime insertion

---

# Firmware Structure

```text
Core
├── main.c
├── pwm.c
├── adc_dma.c
├── current_sense.c
├── voltage_sense.c
├── control_pi.c
├── buck_boost.c
├── encoder.c
├── display_tft.c
├── protection.c
└── ui.c
```

---

# Main Components

| Component         | Description            |
| ----------------- | ---------------------- |
| MCU               | STM32F103              |
| Gate Driver       | IR2101S                |
| MOSFET            | IRF3205                |
| Current Sensor    | Shunt Resistor         |
| Current Amplifier | Differential Amplifier |
| Display           | SPI TFT                |
| User Input        | Rotary Encoder         |
| Control Method    | Digital CC/CV          |
| PWM Mode          | Center-aligned PWM     |

---

# Future Improvements

* STM32G431 migration
* Average Current Mode Control
* Adaptive deadtime
* USB communication
* UART monitoring
* Data logging
* PC software interface

---

## Project Name

**STM32 Digital Synchronous Buck-Boost Converter**

**STM32F103 + IR2101S + IRF3205 + CC/CV Control**
