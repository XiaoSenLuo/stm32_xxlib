/**
  ******************************************************************************
  * File Name          : USART.c
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

/* Includes ------------------------------------------------------------------*/


/* USER CODE BEGIN 0 */
#include "stm32l4xx_it.h"
#include "usart.h"
#include "string.h"
#include "mem_dma.h"


#define UART_BUFFER_MALLOC


#ifdef UART_BUFFER_MALLOC
#include "stdlib.h"
#endif


#define USART1_DATA_BUFFER_SIZE               (512)            /// 定义一次性接收最大数据长度

typedef struct uart_circular_transmit_s {
    uint8_t *data;
    int16_t length;
    int16_t tail;
    int16_t head;
}uart_circular_transmit_t;


typedef struct uart_user_message_s{
    uint8_t *data;
    uint16_t length;
    union{
        struct{
            uint8_t read : 1;
            uint8_t lock : 1;
        };
        uint8_t val;
    }action;
}uart_user_message_t;


#ifdef UART_BUFFER_MALLOC
static uart_circular_transmit_t __attribute__((section(".ram1"))) uart1_rx_message = {
    .data = NULL,
    .length = -1,
    .head = 0,
    .tail = 0,
};

static uart_user_message_t __attribute__((section(".ram1"))) uart1_user_rx_message = {
        .data = NULL,
        .length = 0,
        .action.val = 0,
};

static uart_circular_transmit_t uart1_tx_message = {
        .data = NULL,
        .length = -1,
        .head = 0,
        .tail = 0,
};
#else

static uint8_t usart1_receive_data_buffer[USART1_DATA_BUFFER_SIZE] = { 0 };
static uint8_t usart1_user_receive_data_buffer[USART1_DATA_BUFFER_SIZE] = { 0 };
static uint8_t usart1_transmit_data_buffer[USART1_DATA_BUFFER_SIZE] = { 0 };


static uart_circular_transmit_t __attribute__((section(".ram1"))) uart1_rx_message = {
    .data = usart1_receive_data_buffer,
    .length = -1,
    .head = 0,
    .tail = 0,
};

static uart_user_message_t __attribute__((section(".ram1"))) uart1_user_rx_message = {
        .data = usart1_user_receive_data_buffer,
        .length = 0,
        .action.val = 0,
};

static uart_circular_transmit_t uart1_tx_message = {
        .data = usart1_transmit_data_buffer,
        .length = -1,
        .head = 0,
        .tail = 0,
};
#endif

static UART_HandleTypeDef huart1 = { 0 };
static DMA_HandleTypeDef hdma_usart1_rx = { 0 };
static DMA_HandleTypeDef hdma_usart1_tx = { 0 };

typedef struct uart_dma_state_s{
    union{
        struct{
            uint8_t dma_tx_tc : 1;
            uint8_t dma_rx_tc : 1;
        };
    };
    uint8_t val;
}uart_dma_state_t;


static void dma_usart1_tx_isr_handler(void* ctx){
    HAL_DMA_IRQHandler((DMA_HandleTypeDef*)ctx);
}

static void dma_usart1_rx_isr_handler(void* ctx){
    HAL_DMA_IRQHandler((DMA_HandleTypeDef*)ctx);
}

static void usart1_isr_handler(void* ctx){
    UART_HandleTypeDef* handle = (UART_HandleTypeDef*)ctx;
    uint32_t uart_isr_flag = READ_REG(handle->Instance->ISR);
    if((uart_isr_flag & UART_FLAG_IDLE) && (handle->Instance == USART1)){   /// 空闲
        __HAL_UART_CLEAR_FLAG(handle, UART_FLAG_IDLE);

        uint32_t pos = __HAL_DMA_GET_COUNTER(handle->hdmarx);

        uart1_rx_message.tail = USART1_DATA_BUFFER_SIZE - pos;  /// 计算队尾

        uart1_rx_message.length = uart1_rx_message.tail - uart1_rx_message.head;  /// 计算队列长度, 默认队尾大于队首

        if((uart1_user_rx_message.action.lock) || (uart1_rx_message.length == 0)) goto end_section;
        uart1_user_rx_message.action.lock = 1;
        uart1_user_rx_message.action.read = 0;

        if(uart1_rx_message.length < 0){ /// 队尾小于队首
            uart1_rx_message.length = USART1_DATA_BUFFER_SIZE + uart1_rx_message.length;
            for(int i = 0; i < 2; i++){
                uint32_t cpl = 0, start = 0;
                cpl = (i == 0) ? (USART1_DATA_BUFFER_SIZE - uart1_rx_message.head) : uart1_rx_message.tail;
                start = (i == 0) ? (uart1_rx_message.head) : 0;

                if(cpl < 64){
                    memcpy(&uart1_user_rx_message.data[i * cpl], &uart1_rx_message.data[start], cpl);
                }else{
                    memcpy_dma(&uart1_user_rx_message.data[i * cpl], &uart1_rx_message.data[start], cpl);
                }
            }
        }else{
            if(uart1_rx_message.length < 64){
                memcpy(uart1_user_rx_message.data, &uart1_rx_message.data[uart1_rx_message.head], uart1_rx_message.length);
            }else{
                memcpy_dma(uart1_user_rx_message.data, &uart1_rx_message.data[uart1_rx_message.head], uart1_rx_message.length);
            }
        }
        uart1_user_rx_message.length = uart1_rx_message.length;
        uart1_user_rx_message.action.lock = 0;
        uart1_user_rx_message.action.read = 1;

        uart1_rx_message.head = uart1_rx_message.tail;    /// 更新队首
    }
    end_section:
    HAL_UART_IRQHandler((UART_HandleTypeDef*)ctx);
}

#if(0)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
    uart_sta.dma_rx_tc = 1;
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    __HAL_DMA_ENABLE_IT(huart->hdmarx, DMA_IT_TC | DMA_IT_TE);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart){
    uart_sta.dma_tx_tc = 1;
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart){

}

void HAL_UART_TxHalfCpltCallback(UART_HandleTypeDef *huart){

}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart){

}

#endif

int usart1_initialize(UART_HandleTypeDef* *uart_handle, uint32_t baud){
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    int err = 0;

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /**USART1 GPIO Configuration
        PB6     ------> USART1_TX
        PB7     ------> USART1_RX
   */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA1_Channel5;
    hdma_usart1_rx.Init.Request = DMA_REQUEST_2;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    HAL_DMA_Init(&hdma_usart1_rx);
    __HAL_LINKDMA(&huart1, hdmarx, hdma_usart1_rx);

    /* USART1_TX Init */
    hdma_usart1_tx.Instance = DMA1_Channel4;
    hdma_usart1_tx.Init.Request = DMA_REQUEST_2;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    HAL_DMA_Init(&hdma_usart1_tx);
    __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);

    ll_peripheral_isr_install(DMA1_Channel4_IRQn, dma_usart1_tx_isr_handler, huart1.hdmatx);
    ll_peripheral_isr_install(DMA1_Channel5_IRQn, dma_usart1_rx_isr_handler, huart1.hdmarx);
    ll_peripheral_isr_install(USART1_IRQn, usart1_isr_handler, &huart1);


    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = baud;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    err = HAL_UART_DeInit(&huart1);
    err = HAL_UART_Init(&huart1);

    if(uart_handle) *uart_handle = &huart1;

#ifdef UART_BUFFER_MALLOC
    if(uart1_rx_message.data == NULL){
        uart1_rx_message.data = (uint8_t *)malloc(USART1_DATA_BUFFER_SIZE);
    }
    if(uart1_user_rx_message.data == NULL){
        uart1_user_rx_message.data = (uint8_t *)malloc(USART1_DATA_BUFFER_SIZE);
    }
    if(uart1_tx_message.data == NULL){
        uart1_tx_message.data = (uint8_t *)malloc(USART1_DATA_BUFFER_SIZE);
    }
#endif

    return err;
}

int usart1_deinitialize(UART_HandleTypeDef* uart_handle){
    int err = 0;

    UNUSED(uart_handle);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);
    err = HAL_UART_DeInit(&huart1);
    HAL_DMA_DeInit(huart1.hdmarx);
    HAL_DMA_DeInit(huart1.hdmatx);

    HAL_NVIC_DisableIRQ(USART1_IRQn);

    ll_peripheral_isr_uninstall(USART1_IRQn);
    ll_peripheral_isr_uninstall(DMA1_Channel5_IRQn);
    ll_peripheral_isr_uninstall(DMA1_Channel4_IRQn);

    __HAL_RCC_USART1_CLK_DISABLE();
#ifdef UART_BUFFER_MALLOC
    if(uart1_rx_message.data){
        free(uart1_rx_message.data);
        uart1_rx_message.data = NULL;
    }
    if(uart1_user_rx_message.data){
        free(uart1_user_rx_message.data);
        uart1_user_rx_message.data = NULL;
    }
    if(uart1_tx_message.data){
        free(uart1_tx_message.data);
        uart1_tx_message.data = NULL;
    }
#endif
    return err;
}

uint16_t usart1_start_receive(UART_HandleTypeDef *uart_handle){
    uint32_t err = 0;
    UNUSED(uart_handle);

    ll_peripheral_isr_install(DMA1_Channel5_IRQn, dma_usart1_rx_isr_handler, huart1.hdmarx);

    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    err = HAL_UART_Receive_DMA(&huart1, uart1_rx_message.data, USART1_DATA_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT | DMA_IT_TC);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
    return err;
}

void usart1_stop_receive(UART_HandleTypeDef *uart_handle){

    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    ll_peripheral_isr_uninstall(USART1_IRQn);
}

uint16_t usart1_write_bytes(UART_HandleTypeDef *uart_handle, const void* data, uint16_t length, uint32_t timeout){
    uint32_t err = 0, start_tick = 0, tick = 0, tl = 0;
    int32_t remain = length;
    (void)(uart_handle);
    start_tick = HAL_GetTick();
    tick = start_tick;

    do{
        if((huart1.gState != HAL_UART_STATE_READY) && (huart1.hdmatx->State != HAL_DMA_STATE_READY)) goto continue_section;

        uint32_t ll = (remain <= USART1_DATA_BUFFER_SIZE) ? remain : USART1_DATA_BUFFER_SIZE;

        if(ll > 64){
            memcpy_dma(uart1_tx_message.data, &data[tl], ll);
        }else{
            memcpy(uart1_tx_message.data, &data[tl], ll);
        }
        err = HAL_UART_Transmit_DMA(&huart1, uart1_tx_message.data, ll);
        __HAL_DMA_DISABLE_IT(huart1.hdmatx, DMA_IT_HT);

        if(err) goto continue_section;
        tl += ll;
        remain -= ll;
        continue_section:
        tick = HAL_GetTick();
    }while(((tick - start_tick) <= timeout) && (remain > 0));
	return tl;
}

uint16_t usart1_read_bytes(UART_HandleTypeDef *uart_handle, void *data, uint16_t length, uint32_t timeout){
    uint32_t start_tick = 0, tick = 0;
    uint16_t rl = 0;
    start_tick = HAL_GetTick();
    tick = start_tick;

    do{
//        if((data == NULL) || (uart1_user_rx_message.length == 0)) break;
        if((uart1_user_rx_message.action.read == 0) || (data == NULL) || (uart1_user_rx_message.length == 0)) goto continue_section;

        uart1_user_rx_message.action.lock = 1;
        rl = (length >= uart1_user_rx_message.length) ? uart1_user_rx_message.length : length;

        if(rl > 64){
            memcpy_dma(data, uart1_user_rx_message.data, rl);
        }else{
            memcpy(data, uart1_user_rx_message.data, rl);
        }
        uart1_user_rx_message.length -= rl;
        uart1_user_rx_message.action.lock = 0;
        break;
        continue_section:
        tick = HAL_GetTick();
    }while(((tick - start_tick) < timeout) && timeout);

    return rl;
}


#define LPUART1_DATA_BUFFER_SIZE        (1024)

static DMA_HandleTypeDef hdma_lpuart1_rx = {0 };
static UART_HandleTypeDef hlpuart1 = { 0 };

static uart_circular_transmit_t __attribute__((section(".ram1"))) lpuart1_rx_message = {
        .data = NULL,
        .length = -1,
        .head = 0,
        .tail = 0,
};

static uart_user_message_t __attribute__((section(".ram1"))) lpuart1_user_rx_message = {
        .data = NULL,
        .length = 0,
        .action.val = 0,
};


static void dma_lpuart1_tx_isr_handler(void* ctx){
    HAL_DMA_IRQHandler((DMA_HandleTypeDef*)ctx);
}

static void dma_lpuart1_rx_isr_handler(void* ctx){
    HAL_DMA_IRQHandler((DMA_HandleTypeDef*)ctx);
}

static void lpuart1_isr_handler(void *ctx){
    UART_HandleTypeDef* handle = (UART_HandleTypeDef*)ctx;
    uint32_t uart_isr_flag = READ_REG(handle->Instance->ISR);
    if((uart_isr_flag & UART_FLAG_IDLE) && (handle->Instance == LPUART1)){   /// 空闲
        __HAL_UART_CLEAR_FLAG(handle, UART_FLAG_IDLE);

        uint32_t pos = __HAL_DMA_GET_COUNTER(handle->hdmarx);

        lpuart1_rx_message.tail = LPUART1_DATA_BUFFER_SIZE - pos;  /// 计算队尾

        lpuart1_rx_message.length = lpuart1_rx_message.tail - lpuart1_rx_message.head;  /// 计算队列长度, 默认队尾大于队首

        if((lpuart1_user_rx_message.action.lock) || (lpuart1_rx_message.length == 0)) goto end_section;
        lpuart1_user_rx_message.action.lock = 1;
        lpuart1_user_rx_message.action.read = 0;

        if(lpuart1_rx_message.length < 0){ /// 队尾小于队首
            lpuart1_rx_message.length = LPUART1_DATA_BUFFER_SIZE + lpuart1_rx_message.length;
            for(int i = 0; i < 2; i++){
                uint32_t cpl = 0, start = 0;
                cpl = (i == 0) ? (LPUART1_DATA_BUFFER_SIZE - lpuart1_rx_message.head) : lpuart1_rx_message.tail;
                start = (i == 0) ? (lpuart1_rx_message.head) : 0;

                if(cpl < 64){
                    memcpy(&lpuart1_user_rx_message.data[i * cpl], &lpuart1_rx_message.data[start], cpl);
                }else{
                    memcpy_dma(&lpuart1_user_rx_message.data[i * cpl], &lpuart1_rx_message.data[start], cpl);
                }
            }
        }else{
            if(lpuart1_rx_message.length < 64){
                memcpy(lpuart1_user_rx_message.data, &lpuart1_rx_message.data[lpuart1_rx_message.head], lpuart1_rx_message.length);
            }else{
                memcpy_dma(lpuart1_user_rx_message.data, &lpuart1_rx_message.data[lpuart1_rx_message.head], lpuart1_rx_message.length);
            }
        }
        lpuart1_user_rx_message.length = lpuart1_rx_message.length;
        lpuart1_user_rx_message.action.lock = 0;
        lpuart1_user_rx_message.action.read = 1;

        lpuart1_rx_message.head = lpuart1_rx_message.tail;    /// 更新队首
    }
    end_section:
    HAL_UART_IRQHandler((UART_HandleTypeDef*)ctx);
}


int lpuart1_initialize(UART_HandleTypeDef* *uart_handle, uint32_t baud){
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    int err = 0;

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART1;
    PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_LPUART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /**LPUART1 GPIO Configuration
    PB10     ------> LPUART1_RX
    PB11     ------> LPUART1_TX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10 | GPIO_PIN_11);

    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_lpuart1_rx.Instance = DMA2_Channel7;
    hdma_lpuart1_rx.Init.Request = DMA_REQUEST_4;
    hdma_lpuart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_lpuart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_lpuart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_lpuart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_lpuart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_lpuart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_lpuart1_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    HAL_DMA_Init(&hdma_lpuart1_rx);
    __HAL_LINKDMA(&hlpuart1, hdmarx, hdma_lpuart1_rx);

    ll_peripheral_isr_install(DMA2_Channel7_IRQn, dma_lpuart1_rx_isr_handler, hlpuart1.hdmarx);
    ll_peripheral_isr_install(LPUART1_IRQn, lpuart1_isr_handler, &hlpuart1);

    HAL_NVIC_SetPriority(DMA2_Channel7_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel7_IRQn);

    HAL_NVIC_SetPriority(LPUART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);

    hlpuart1.Instance = LPUART1;
    hlpuart1.Init.BaudRate = baud;
    hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits = UART_STOPBITS_1;
    hlpuart1.Init.Parity = UART_PARITY_NONE;
    hlpuart1.Init.Mode = UART_MODE_RX;
    hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlpuart1.Init.OverSampling = UART_OVERSAMPLING_8;
    hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    err = HAL_UART_DeInit(&hlpuart1);
    err = HAL_UART_Init(&hlpuart1);

    if(uart_handle) *uart_handle = &hlpuart1;
#ifdef UART_BUFFER_MALLOC
    if(lpuart1_rx_message.data == NULL){
        lpuart1_rx_message.data = (uint8_t *)malloc(LPUART1_DATA_BUFFER_SIZE);
    }
    if(lpuart1_user_rx_message.data == NULL){
        lpuart1_user_rx_message.data = (uint8_t *)malloc(LPUART1_DATA_BUFFER_SIZE);
    }
#endif
    return err;
}

int lpuart1_deinitialize(UART_HandleTypeDef* uart_handle){
    int err = 0;

    UNUSED(uart_handle);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10 | GPIO_PIN_11);
    err = HAL_UART_DeInit(&hlpuart1);

    HAL_DMA_DeInit(hlpuart1.hdmarx);

    HAL_NVIC_DisableIRQ(LPUART1_IRQn);

    ll_peripheral_isr_uninstall(LPUART1_IRQn);
    ll_peripheral_isr_uninstall(DMA2_Channel7_IRQn);

    __HAL_RCC_LPUART1_CLK_DISABLE();
#ifdef UART_BUFFER_MALLOC
    if(lpuart1_rx_message.data){
        free(lpuart1_user_rx_message.data);
        lpuart1_user_rx_message.data = NULL;
    }
    if(lpuart1_user_rx_message.data){
        free(lpuart1_user_rx_message.data);
        lpuart1_user_rx_message.data = NULL;
    }
#endif
    return err;
}



uint16_t lpuart1_start_receive(UART_HandleTypeDef *uart_handle){
    uint32_t err = 0;
    UNUSED(uart_handle);

    ll_peripheral_isr_install(DMA2_Channel7_IRQn, dma_lpuart1_rx_isr_handler, hlpuart1.hdmarx);

    HAL_NVIC_SetPriority(DMA2_Channel7_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel7_IRQn);

    err = HAL_UART_Receive_DMA(&hlpuart1, lpuart1_rx_message.data, LPUART1_DATA_BUFFER_SIZE);

    __HAL_DMA_DISABLE_IT(hlpuart1.hdmarx, DMA_IT_HT | DMA_IT_TC);
    __HAL_UART_ENABLE_IT(&hlpuart1, UART_IT_IDLE);
    return err;
}


void lpuart1_stop_receive(UART_HandleTypeDef *uart_handle){
    HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
    ll_peripheral_isr_uninstall(LPUART1_IRQn);
}



uint16_t lpuart1_read_bytes(UART_HandleTypeDef *uart_handle, void* data, uint16_t length, uint32_t timeout){
    uint32_t start_tick = 0, tick = 0;
    uint16_t rl = 0;
    start_tick = HAL_GetTick();
    tick = start_tick;

    do{
//        if((data == NULL) || (lpuart1_user_rx_message.length == 0)) goto continue_section;
        if((lpuart1_user_rx_message.action.read == 0) || (data == NULL) || (lpuart1_user_rx_message.length == 0)) goto continue_section;

        lpuart1_user_rx_message.action.lock = 1;
        rl = (length >= lpuart1_user_rx_message.length) ? lpuart1_user_rx_message.length : length;

        if(rl > 64){
            memcpy_dma(data, lpuart1_user_rx_message.data, rl);
        }else{
            memcpy(data, lpuart1_user_rx_message.data, rl);
        }
        lpuart1_user_rx_message.length -= rl;
        lpuart1_user_rx_message.action.lock = 0;
        break;
        continue_section:
        tick = HAL_GetTick();
    }while(((tick - start_tick) < timeout) && timeout);
    return rl;
}



/* USER CODE END 0 */

/* USER CODE END 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
