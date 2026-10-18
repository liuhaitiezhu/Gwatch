#ifndef BSP_I2C_BUS_H
#define BSP_I2C_BUS_H

#include "stm32u5xx_hal.h"

/* Initialize once before starting the scheduler. Lock/Unlock are task-only;
 * pair Unlock with a successful Lock after the scheduler has started. */
HAL_StatusTypeDef BSP_I2C1_InitMutex(void);
HAL_StatusTypeDef BSP_I2C1_Lock(uint32_t timeout_ms);
void BSP_I2C1_Unlock(void);

/* Owns the bus lock. Address is the HAL-style, left-shifted 7-bit address.
 * Delays STOP after the command ACK, as required by SHT20 no-hold commands. */
HAL_StatusTypeDef BSP_I2C1_WriteCommand(uint16_t address, uint8_t command,
                                     uint32_t timeout_ms);

#endif
