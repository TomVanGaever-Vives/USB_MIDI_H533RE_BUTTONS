#include "mcp23s17.h"

static inline void CS_Low (void) { HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_RESET); }
static inline void CS_High(void) { HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_SET);   }

void MCP23S17_Init(SPI_HandleTypeDef *hspi)
{
    MCP23S17_WriteReg(hspi, MCP_IODIRA, 0x00); /* PORTA = outputs (columns) */
    MCP23S17_WriteReg(hspi, MCP_IODIRB, 0x0F); /* PORTB GPB0-3 = inputs (rows) */
    MCP23S17_WriteReg(hspi, MCP_GPPUB,  0x0F); /* Pull-ups on GPB0-3 */
    MCP23S17_WriteReg(hspi, MCP_GPIOA,  0x0F); /* All columns inactive (HIGH) */
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
