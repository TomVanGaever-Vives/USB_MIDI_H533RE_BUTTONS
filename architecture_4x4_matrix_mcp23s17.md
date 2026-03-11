# Architecture: 4×4 Button Matrix via MCP23S17 on STM32H533 Nucleo

> **Project:** MIDI Controller – Button Matrix Module
> **Hardware:** STM32H533CE Nucleo + MCP23S17 I/O Expander
> **Interface:** SPI2
> **Version:** 1.0
> **Date:** 2026-03-11

---

## 1. Overview

This document describes the architecture for reading a **4×4 button matrix** (16 buttons) via the **MCP23S17 SPI I/O expander** on the **STM32H533CE Nucleo board**. **SPI2** is used to avoid any conflict with the existing **USB (CDC/MIDI)** implementation.

```
┌───────────────────────────────────────────────┐
│             STM32H533CE Nucleo                 │
│                                               │
│  USB_DM (PA11) ──┐                            │
│  USB_DP (PA12) ──┴── USB FS ──── Host / DAW   │
│                                               │
│  SPI2_SCK  (PB13) ──────────────────────┐     │
│  SPI2_MISO (PB14) ───────────────────┐  │     │
│  SPI2_MOSI (PB15) ────────────────┐  │  │     │
│  MCP_CS    (PB12) ─────────────┐  │  │  │     │
└───────────────────────────────┼──┼──┼──┼─────┘
                                 │  │  │  │
                          ┌──────▼──▼──▼──▼──────┐
                          │      MCP23S17         │
                          │                       │
                          │  PORTA (GPA0–3) ──────┼── COL0..3
                          │  PORTB (GPB0–3) ──────┼── ROW0..3
                          └───────────────────────┘
                                      │
                               ┌──────▼──────┐
                               │ 4×4 Matrix  │
                               │ 16 buttons  │
                               └─────────────┘
```

---

## 2. Why SPI2?

| | SPI1 | SPI2 |
|---|---|---|
| SCK | PA5 | **PB13** |
| MISO | PA6 | **PB14** |
| MOSI | PA7 | **PB15** |
| USB pins | PA11 / PA12 | *(no overlap)* |
| ST-Link | PA13 / PA14 | *(no overlap)* |
| **Conflict** | PA5–PA7 may clash with Arduino header or other peripherals | ✅ **No conflict with USB or debugger** |

> SPI2 on **PB12–PB15** is fully isolated from the USB pins (PA11/PA12) and the ST-Link debug interface, making it the safest choice for simultaneous USB-MIDI and matrix operation.

---

## 3. Pin Connections

### 3.1 STM32H533 Nucleo → MCP23S17

| Nucleo Pin | Function | MCP23S17 Pin | Description |
|---|---|---|---|
| PB13 | SPI2_SCK | 12 (SCK) | SPI clock |
| PB15 | SPI2_MOSI | 13 (SI) | Data to MCP23S17 |
| PB14 | SPI2_MISO | 14 (SO) | Data from MCP23S17 |
| PB12 | GPIO Output (CS) | 11 (CS̄) | Chip Select (active low) |
| 3.3V | VDD | 9 (VDD) | Power supply |
| GND | VSS | 10 (VSS) | Ground |
| 3.3V | RESET̄ | 18 (RESET̄) | Tie high to keep chip active |
| GND | Address | 15/16/17 (A0/A1/A2) | Address = 0b000 → opcode 0x40/0x41 |

### 3.2 MCP23S17 → 4×4 Button Matrix

| MCP23S17 Pin | Port | Direction | Function | Matrix |
|---|---|---|---|---|
| GPA0 (pin 21) | PORTA | Output | Column 0 | Col 1 |
| GPA1 (pin 22) | PORTA | Output | Column 1 | Col 2 |
| GPA2 (pin 23) | PORTA | Output | Column 2 | Col 3 |
| GPA3 (pin 24) | PORTA | Output | Column 3 | Col 4 |
| GPB0 (pin 1) | PORTB | Input | Row 0 | Row 1 |
| GPB1 (pin 2) | PORTB | Input | Row 1 | Row 2 |
| GPB2 (pin 3) | PORTB | Input | Row 2 | Row 3 |
| GPB3 (pin 4) | PORTB | Input | Row 3 | Row 4 |

> GPA4–GPA7 and GPB4–GPB7 are unused. Internal pull-ups (100 kΩ) are enabled on GPB0–3.

---

## 4. MCP23S17 Register Configuration

### 4.1 Initialization Registers

| Register | Address | Value | Description |
|---|---|---|---|
| IODIRA | 0x00 | 0x00 | PORTA fully output (columns) |
| IODIRB | 0x01 | 0x0F | GPB0–3 input, GPB4–7 output (unused) |
| GPPUB | 0x0D | 0x0F | Enable pull-ups on GPB0–3 (rows) |
| GPIOA | 0x12 | 0x0F | All columns inactive (HIGH) |

### 4.2 SPI Transaction Format

Every transaction is 3 bytes:

```
Byte 1 – Opcode:   0 1 0 0  A2 A1 A0  R/W
                   A2=A1=A0=0  →  Write: 0x40 | Read: 0x41

Byte 2 – Register: e.g. 0x12 = GPIOA, 0x13 = GPIOB
Byte 3 – Data:     byte to write, or received byte on read
```

CS must be held **LOW** for the entire 3-byte transaction, then brought **HIGH**.

---

## 5. Scan Algorithm

```
For each column k in {0, 1, 2, 3}:
  1. Write GPIOA = ~(1 << k)      // Pull column k LOW, rest HIGH
  2. Wait ~1 µs                   // Settle time
  3. Read GPIOB                   // Read row inputs (0 = pressed)
  4. Compare with previous state  // Detect changes
  5. On change → send MIDI Note On / Note Off
```

### Column Drive Values (GPIOA)

| Column | GPIOA | Binary |
|---|---|---|
| COL0 | 0x0E | `1110` |
| COL1 | 0x0D | `1101` |
| COL2 | 0x0B | `1011` |
| COL3 | 0x07 | `0111` |

---

## 6. STM32CubeMX Configuration

### 6.1 SPI2 Settings

```
Peripheral     : SPI2
Mode           : Full-Duplex Master
Data Size      : 8-bit
Clock Polarity : Low  (CPOL = 0)
Clock Phase    : 1st Edge (CPHA = 0)  →  SPI Mode 0
NSS            : Software (manual via PB12)
Baud Rate      : ≤ 10 MHz  (MCP23S17 max)
Bit Order      : MSB first
```

### 6.2 GPIO Settings

```
PB12  GPIO_Output  Push-Pull  No Pull  Initial: HIGH  (CS inactive)
PB13  Alternate Function  →  SPI2_SCK
PB14  Alternate Function  →  SPI2_MISO
PB15  Alternate Function  →  SPI2_MOSI
```

### 6.3 USB (no conflicts)

```
USB_OTG_FS:
  PA11  →  USB_DM  (Alternate Function)
  PA12  →  USB_DP  (Alternate Function)
  Class :  Audio / MIDI  (or CDC)

Completely separate from SPI2 (PB12–PB15) ✅
```

---

## 7. Software Structure

```
midi_controller/
├── Core/
│   ├── Src/
│   │   ├── main.c            ← Main loop: scan + MIDI output
│   │   ├── mcp23s17.c        ← MCP23S17 SPI driver
│   │   └── matrix.c          ← Scan logic + debounce
│   └── Inc/
│       ├── mcp23s17.h
│       └── matrix.h
├── USB_DEVICE/
│   └── App/
│       └── usbd_midi_if.c    ← USB MIDI interface
└── ...
```

### 7.1 `mcp23s17.h`

```c
#ifndef MCP23S17_H
#define MCP23S17_H

#include "stm32h5xx_hal.h"

/* Register addresses (IOCON.BANK = 0, default) */
#define MCP_IODIRA  0x00
#define MCP_IODIRB  0x01
#define MCP_GPPUB   0x0D
#define MCP_GPIOA   0x12
#define MCP_GPIOB   0x13

/* SPI opcodes (A2=A1=A0=0) */
#define MCP_WRITE   0x40
#define MCP_READ    0x41

/* Chip Select */
#define MCP_CS_PIN  GPIO_PIN_12
#define MCP_CS_PORT GPIOB

void    MCP23S17_Init    (SPI_HandleTypeDef *hspi);
void    MCP23S17_WriteReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t data);
uint8_t MCP23S17_ReadReg (SPI_HandleTypeDef *hspi, uint8_t reg);

#endif /* MCP23S17_H */
```

### 7.2 `mcp23s17.c`

```c
#include "mcp23s17.h"

static inline void CS_Low (void) { HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_RESET); }
static inline void CS_High(void) { HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_SET);   }

void MCP23S17_Init(SPI_HandleTypeDef *hspi)
{
    MCP23S17_WriteReg(hspi, MCP_IODIRA, 0x00); /* PORTA = outputs (columns) */
    MCP23S17_WriteReg(hspi, MCP_IODIRB, 0x0F); /* PORTB GPB0-3 = inputs (rows) */
    MCP23S17_WriteReg(hspi, MCP_GPPUB,  0x0F); /* Pull-ups on GPB0-3 */
    MCP23S17_WriteReg(hspi, MCP_GPIOA,  0x0F); /* All columns inactive */
}

void MCP23S17_WriteReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t data)
{
    uint8_t buf[3] = { MCP_WRITE, reg, data };
    CS_Low();
    HAL_SPI_Transmit(hspi, buf, 3, HAL_MAX_DELAY);
    CS_High();
}

uint8_t MCP23S17_ReadReg(SPI_HandleTypeDef *hspi, uint8_t reg)
{
    uint8_t tx[3] = { MCP_READ, reg, 0x00 };
    uint8_t rx[3] = { 0 };
    CS_Low();
    HAL_SPI_TransmitReceive(hspi, tx, rx, 3, HAL_MAX_DELAY);
    CS_High();
    return rx[2];
}
```

### 7.3 `matrix.c` (scan + debounce)

```c
#include "mcp23s17.h"
#include "matrix.h"

extern SPI_HandleTypeDef hspi2;

static const uint8_t COL_MASK[4] = { 0x0E, 0x0D, 0x0B, 0x07 };
static uint8_t prev_state[4] = { 0x0F, 0x0F, 0x0F, 0x0F };

/* Call this every ~5 ms from main loop or timer interrupt */
void Matrix_Scan(void)
{
    for (uint8_t col = 0; col < 4; col++)
    {
        /* Drive one column LOW */
        MCP23S17_WriteReg(&hspi2, MCP_GPIOA, COL_MASK[col]);
        HAL_Delay(1); /* ~1 µs settle – replace with DWT delay in production */

        /* Read rows */
        uint8_t rows = MCP23S17_ReadReg(&hspi2, MCP_GPIOB) & 0x0F;

        /* Detect changes */
        uint8_t changed = rows ^ prev_state[col];
        if (changed)
        {
            for (uint8_t row = 0; row < 4; row++)
            {
                if (changed & (1 << row))
                {
                    uint8_t button = col * 4 + row;
                    uint8_t pressed = !(rows & (1 << row)); /* LOW = pressed */
                    MIDI_SendNoteOnOff(button, pressed);
                }
            }
            prev_state[col] = rows;
        }
    }

    /* Release all columns */
    MCP23S17_WriteReg(&hspi2, MCP_GPIOA, 0x0F);
}
```

---

## 8. MIDI Note Mapping

| Button | Col | Row | MIDI Note |
|---|---|---|---|
| 0 | 0 | 0 | 36 (C2) |
| 1 | 0 | 1 | 37 |
| … | … | … | … |
| 15 | 3 | 3 | 51 (D#2) |

> Note numbers are `36 + button_index`. Adjust the base note to fit your MIDI layout.

---

## 9. Timing Budget

| Step | Time |
|---|---|
| SPI transaction (3 bytes @ 10 MHz) | ~2.4 µs |
| Settle delay after column drive | ~1 µs |
| Full 4-column scan (4 × write + read) | ~30 µs |
| Recommended scan interval | 5–10 ms |
| USB MIDI latency (full-speed USB) | ~1 ms |
| **Total button-to-MIDI latency** | **< 10 ms** ✅ |

---

## 10. Potential Issues & Solutions

| Issue | Cause | Solution |
|---|---|---|
| Ghost presses | No diodes in matrix | Add 1N4148 per button, or limit simultaneous presses in firmware |
| CS glitch during USB IRQ | Interrupt preempts SPI | Wrap SPI transfer in `__disable_irq()` / `__enable_irq()`, or use SPI DMA with mutex |
| Slow scan causes missed MIDI events | HAL_Delay blocking | Replace `HAL_Delay(1)` with a DWT cycle counter delay |
| SPI clock too fast | PCB trace capacitance | Keep SPI2 ≤ 4 MHz if traces are long (> 10 cm) |
