# STM32 Synchronous Buck-Boost Converter

A digital synchronous Buck-Boost DC-DC converter based on **STM32F103**, designed for high efficiency and programmable CC/CV operation.

---

## Features

* Four-switch synchronous Buck-Boost topology
* STM32F103 MCU control
* MOSFET gate driver using **IR2101S**
* Power MOSFETs: **IRF3205**
* Constant Voltage (CV) mode
* Constant Current (CC) mode
* TFT SPI display user interface
* Rotary encoder parameter adjustment
* Current sensing using shunt resistor
* Differential amplifier current measurement circuit
* ADC + DMA sampling
* Center-aligned PWM
* Deadtime protection
* Soft-start
* Over-current protection (OCP)
* Over-voltage protection (OVP)

---

## Hardware Architecture

```
                +----------------+
                |   STM32F103    |
                +----------------+
                    |        |
               PWM1 |        | PWM2
                    |        |
              +-----+--------+-----+
              |     IR2101S Drivers |
              +-----+--------+-----+
                    |        |
               +----+--------+----+
               | 4x IRF3205 MOSFET |
               +----+--------+----+
                    |        |
                    +---47uH-+
                           |
                         VOUT

```

---

## Specifications

| Parameter         | Value                              |
| ----------------- | ---------------------------------- |
| Topology          | Four-switch synchronous Buck-Boost |
| Controller        | STM32F103                          |
| Gate Driver       | IR2101S                            |
| MOSFET            | IRF3205                            |
| Current Sense     | Shunt resistor                     |
| Current Amplifier | Differential amplifier             |
| User Interface    | TFT SPI + Rotary Encoder           |
| Control Method    | Digital CC/CV                      |
| PWM Mode          | Center-aligned                     |
| Protection        | OCP, OVP, Soft-start               |

---

## Current Measurement

Output current is measured using a low-value shunt resistor.

```
Iout
 ↓
Rshunt
 ↓
Differential Amplifier
 ↓
RC Low-pass Filter
 ↓
ADC (STM32)
```

### Differential amplifier

```
         R1              R2
V+ ----/\/\/\-----+----/\/\/\-----+
                  |               |
                  |             Vout
                  |               |
V- ----/\/\/\-----+----/\/\/\-----+
         R1              R2
```

Gain:

```
Gain = R2 / R1
```

The amplified signal is filtered before entering the ADC.

---

## User Interface

### TFT SPI Display

Display:

* Input voltage
* Output voltage
* Output current
* Output power
* Operating mode (CC/CV)
* Duty cycle

### Rotary Encoder

Adjustable parameters:

* Output voltage
* Current limit
* Menu navigation

---

## Control Structure

### Voltage Loop

```
Vref
 ↓
PI Controller
 ↓
Iref
```

### Current Loop

```
Iref
 ↓
PI Controller
 ↓
PWM Duty
```

---

## Operating Modes

### CV Mode

Maintain constant output voltage.

```
Iout < I_limit
```

### CC Mode

Limit output current.

```
Iout ≥ I_limit
```

---

## Protections

* Over Current Protection (OCP)
* Over Voltage Protection (OVP)
* Soft-start
* Deadtime insertion
* Shoot-through prevention

---

## Project Structure

```
Core/
├── Inc/
│   ├── buck_boost.h
│   ├── control.h
│   ├── adc_dma.h
│   ├── encoder.h
│   ├── display.h
│   └── protection.h
│
├── Src/
│   ├── main.c
│   ├── buck_boost.c
│   ├── control.c
│   ├── adc_dma.c
│   ├── encoder.c
│   ├── display.c
│   └── protection.c
│
└── Drivers/
```

---

## Future Improvements

* STM32G431 migration
* HRTIM support
* Average Current Mode Control
* USB-C PD support
* Data logging
* UART/USB monitoring
* PC software interface

---

## Author

Digital Synchronous Buck-Boost Converter Project

STM32F103 + IR2101S + IRF3205
