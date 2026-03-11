# SPI Debug Problem: MCP23S17 Not Responding Reliably

## Hardware

| Component | Details |
|---|---|
| MCU | STM32H533RE (Nucleo-64) |
| SPI expander | MCP23S17 |
| Interface | SPI2 |
| IDE | Keil MDK-ARM V6.24 |
| HAL | STM32H5xx HAL |

---

## Wiring

| Nucleo Pin | MCP23S17 Pin | Function |
|---|---|---|
| PB13 | 12 (SCK) | SPI clock |
| PB15 | 13 (SI) | MOSI → SI |
| PB14 | 14 (SO) | MISO ← SO |
| PB12 | 11 (CS) | Chip select, active low |
| 3.3V | 9 (VDD) | Power |
| GND | 10 (VSS) | Ground |
| 3.3V | 18 (RESET) | Tied high, chip active |
| GND | 15/16/17 (A0/A1/A2) | Address = 000 |

Connected on a **breadboard** with jumper wires.

---

## System Clock

CubeMX regeneration (when SPI2 was added) changed the clock from simple HSI to PLL:

| Parameter | Value |
|---|---|
| Source | CSI (4 MHz) |
| PLL1 N | x48 → VCO = 192 MHz |
| PLLP /2 | SYSCLK = 96 MHz |
| PLLQ /4 | **SPI2 clock = 48 MHz** |
| AHB /2 | HCLK = 48 MHz |
| USB clock | HSI48 (unchanged) |

---

## SPI2 Configuration (CubeMX generated)

```c
hspi2.Init.Mode                     = SPI_MODE_MASTER;
hspi2.Init.Direction                = SPI_DIRECTION_2LINES;
hspi2.Init.DataSize                 = SPI_DATASIZE_8BIT;
hspi2.Init.CLKPolarity              = SPI_POLARITY_LOW;     // CPOL=0
hspi2.Init.CLKPhase                 = SPI_PHASE_1EDGE;      // CPHA=0 → Mode 0
hspi2.Init.NSS                      = SPI_NSS_SOFT;         // CS via GPIO
hspi2.Init.BaudRatePrescaler        = SPI_BAUDRATEPRESCALER_64; // 48MHz/64 = 750 kHz
hspi2.Init.FirstBit                 = SPI_FIRSTBIT_MSB;
hspi2.Init.NSSPMode                 = SPI_NSS_PULSE_ENABLE; // <-- see suspects below
hspi2.Init.NSSPolarity              = SPI_NSS_POLARITY_LOW;
hspi2.Init.FifoThreshold            = SPI_FIFO_THRESHOLD_01DATA;
hspi2.Init.MasterSSIdleness         = SPI_MASTER_SS_IDLENESS_00CYCLE;
hspi2.Init.MasterInterDataIdleness  = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
```

**GPIO for SPI pins (HAL_SPI_MspInit):**
```c
GPIO_InitStruct.Pin       = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull      = GPIO_NOPULL;
GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;   // <-- see suspects below
GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
```

**GPIO for CS pin (MX_GPIO_Init):**
```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);  // initial HIGH
GPIO_InitStruct.Pin   = GPIO_PIN_12;
GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
GPIO_InitStruct.Pull  = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
```

---

## Test Code

A one-shot test reads the MCP23S17 **IODIRA register** (default value = `0xFF` after reset):

```c
void MCP23S17_SPI_Test(void)
{
  uint8_t tx[3] = { 0x41, 0x00, 0x00 }; // read opcode (addr=000), IODIRA, dummy
  uint8_t rx[3] = { 0x00, 0x00, 0x00 };
  HAL_StatusTypeDef status;

  HAL_Delay(100);                                          // MCP23S17 power-up time
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);  // CS low
  HAL_Delay(1);                                            // CS setup time
  status = HAL_SPI_TransmitReceive(&hspi2, tx, rx, 3, 100);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);    // CS high

  int len = snprintf(msg, sizeof(msg),
    "SPI status=%d IODIRA=0x%02X (expect 0xFF)\r\n", status, rx[2]);
  HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t*)msg, (uint16_t)len, 100);
}
```

Output via ST-Link VCP (UART, 115200 baud) to PuTTY.

---

## Observed Behaviour

| Condition | Result |
|---|---|
| Normal run (750 kHz) | `status=0` (HAL_OK), `IODIRA=0x00` — **wrong**, ~95% of resets |
| Normal run (750 kHz) | `status=0` (HAL_OK), `IODIRA=0xFF` — **correct**, ~5% of resets |
| Analog Discovery scope probe connected to CS pin | `IODIRA=0xFF` **consistently** |
| Normal run at 6 MHz (prescaler /8) | Same intermittent behaviour |

- `status=0` always → SPI hardware completes without error
- Getting `0x00` not `0xFF` → MCP23S17 either not responding or returning wrong data
- Connecting a scope probe **to CS** fixes the problem → suggests signal integrity or impedance issue on CS

---

## What Has Been Ruled Out

- **HAL SPI error**: always `status=0` (HAL_OK)
- **Power-up timing**: 100ms delay before first transaction makes no difference
- **SPI speed**: tried both 6 MHz and 750 kHz, same behaviour
- **CS software timing**: CS held low for entire 3-byte blocking transfer, 1ms setup delay
- **CS GPIO init order**: GPIOB clock enabled, initial HIGH set before `HAL_GPIO_Init`
- **Wiring swap (MOSI/MISO)**: rewired multiple times, same result
- **Bad breadboard contact**: changed wires, rows and pins multiple times

---

## Suspected Causes (unresolved)

### 1. `NSSPMode = SPI_NSS_PULSE_ENABLE`
CubeMX set this when generating SPI2. Even with `NSS = SPI_NSS_SOFT` (CS via GPIO), the STM32H5 SPI hardware generates internal NSS pulses **between bytes** in a multi-byte transfer. This may cause the SPI clock to pause briefly between the 3 bytes. If the MCP23S17 is sensitive to mid-transaction clock gaps, it could reset its internal state machine and return 0x00.

**Suggested fix:** Set `NSSPMode = SPI_NSS_PULSE_DISABLE` in CubeMX → regenerate.

### 2. `GPIO_SPEED_FREQ_LOW` on SPI pins (RESOLVED ✅)
The SPI alternate function pins (PB13 SCK, PB14 MISO, PB15 MOSI) and CS (PB12) were initially configured with `GPIO_SPEED_FREQ_LOW`. On STM32H5, this limits slew rate to ~2 MHz. At 750 kHz SPI clock, rise/fall times of ~500ns represent ~37% of the clock period. This degraded signal quality enough to cause intermittent read failures on the breadboard.

**Resolution:** Changing the CS GPIO speed to `GPIO_SPEED_FREQ_VERY_HIGH` fixed the issue. The slow falling edge of the CS signal was failing to properly activate the MCP23S17 in time for the first clock edge. The scope probe's capacitance actually masked this by interacting with the slow drive strength, but native fast edges resolve it reliably.

### 3. Probe capacitance effect
The Analog Discovery scope probe (~24pF input capacitance, 1MΩ) connected to CS consistently fixes the problem. This is an unusual symptom. Possible explanations:
- The probe capacitance slows the CS falling edge, giving the MCP23S17 more time to recognise CS active
- The probe's ground connection improves the common ground between MCU and MCP23S17
- The probe is bridging a marginal breadboard contact (though wiring has been replaced)

---

## Files

| File | Description |
|---|---|
| `Core/Src/main.c` | SPI test function `MCP23S17_SPI_Test()`, button debug |
| `Core/Src/stm32h5xx_hal_msp.c` | `HAL_SPI_MspInit` — GPIO config for PB13/14/15 |
| `Core/Src/stm32h5xx_it.c` | USB ISR: `tud_int_handler(0)` in USER CODE block |
| `USB_MIDI2.ioc` | CubeMX project — SPI2, GPIO, clock config |
| `architecture_4x4_matrix_mcp23s17.md` | Full hardware/software architecture |

GitHub repo: https://github.com/TomVanGaever-Vives/USB_MIDI_H533RE_BUTTONS.git
