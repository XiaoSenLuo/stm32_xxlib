/**
  ******************************************************************************
  * File Name          : USART.h
  * Description        : This file provides code for the configuration
  *                      of the USART instances.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __usart_H
#define __usart_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/


/* USER CODE BEGIN Includes */
#include "stm32l4xx_hal.h"

/* USER CODE END Includes */


int usart1_initialize(UART_HandleTypeDef* *uart_handle, uint32_t baud);

int usart1_deinitialize(UART_HandleTypeDef* uart_handle);

uint16_t usart1_start_receive(UART_HandleTypeDef *uart_handle);

void usart1_stop_receive(UART_HandleTypeDef *uart_handle);

uint16_t usart1_write_bytes(UART_HandleTypeDef *uart_handle, const void* data, uint16_t length, uint32_t timeout);

uint16_t usart1_read_bytes(UART_HandleTypeDef *uart_handle, void* data, uint16_t length, uint32_t timeout);

int lpuart1_initialize(UART_HandleTypeDef* *uart_handle, uint32_t baud);

int lpuart1_deinitialize(UART_HandleTypeDef* uart_handle);

uint16_t lpuart1_start_receive(UART_HandleTypeDef *uart_handle);

void lpuart1_stop_receive(UART_HandleTypeDef *uart_handle);

uint16_t lpuart1_read_bytes(UART_HandleTypeDef *uart_handle, void* data, uint16_t length, uint32_t timeout);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ usart_H */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
