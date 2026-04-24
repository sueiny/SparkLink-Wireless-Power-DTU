/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: UART Sample Source. \n
 *
 * History: \n
 * 2023-06-29, Create file. \n
 */
#include "pinctrl.h"
#include "uart.h"
#include "soc_osal.h"
#include "app_init.h"
#include "dma.h"
#include "hal_dma.h"

#include "sht30_uart.h"

#define DEFAULT_UART_BAUDRATE                      115200

#define UART_TASK_PRIO                     24
#define UART_TASK_STACK_SIZE               0x1000

static uint8_t default_uart_rx_buff[MAX_TRANSFER_SIZE] = { 0 };

static uint8_t default_uart_int_rx_flag = 0;
static volatile uint16_t default_uart_int_index = 0;
static uint8_t default_uart_int_rx_buff[MAX_TRANSFER_SIZE] = { 0 }; 

static uart_buffer_config_t default_uart_buffer_config = {
    .rx_buffer = default_uart_rx_buff,
    .rx_buffer_size = MAX_TRANSFER_SIZE
};

static uart_write_dma_config_t default_uart_dma_cfg = {
    .src_width = HAL_DMA_TRANSFER_WIDTH_8,
    .dest_width = HAL_DMA_TRANSFER_WIDTH_8,
    .burst_length = HAL_DMA_BURST_TRANSACTION_LENGTH_1,
    .priority = HAL_DMA_CH_PRIORITY_0
};

static bool app_uart_dma_write_succ(int32_t ret, uint32_t length)
{
#if defined(CONFIG_UART_USING_V151)
    unused(length);
    return (ret == ERRCODE_SUCC);
#else
    return (ret == (int32_t)length);
#endif
}

static void app_uart_log_dma_result(int32_t ret, uint32_t length)
{
    osal_printk("uart%d dma write ret=0x%x, expect=%u\r\n", DEFALUT_UART_BUS, (uint32_t)ret, length);
    if (app_uart_dma_write_succ(ret, length)) {
        osal_printk("uart%d dma mode send back succ!\r\n", DEFALUT_UART_BUS);
        return;
    }

    switch ((uint32_t)ret) {
        case UART_DMA_CFG_PARAM_INVALID:
            osal_printk("uart%d dma write failed: UART_DMA_CFG_PARAM_INVALID\r\n", DEFALUT_UART_BUS);
            break;
        case UART_DMA_BUFF_NULL:
            osal_printk("uart%d dma write failed: UART_DMA_BUFF_NULL\r\n", DEFALUT_UART_BUS);
            break;
        case UART_DMA_SHAKING_INVALID_OR_UART_FUNCS_NULL:
            osal_printk("uart%d dma write failed: UART_DMA_SHAKING_INVALID_OR_UART_FUNCS_NULL\r\n", DEFALUT_UART_BUS);
            break;
        case UART_DMA_CONFIGURE_FAIL:
            osal_printk("uart%d dma write failed: UART_DMA_CONFIGURE_FAIL\r\n", DEFALUT_UART_BUS);
            break;
        case UART_DMA_START_TRANSFER_FAIL:
            osal_printk("uart%d dma write failed: UART_DMA_START_TRANSFER_FAIL\r\n", DEFALUT_UART_BUS);
            break;
        case UART_DMA_TRANSFER_TIMEOUT:
            osal_printk("uart%d dma write failed: UART_DMA_TRANSFER_TIMEOUT\r\n", DEFALUT_UART_BUS);
            break;
        default:
            osal_printk("uart%d dma write failed: unknown ret=0x%x\r\n", DEFALUT_UART_BUS, (uint32_t)ret);
            break;
    }
}


static void app_uart_init_pin(void)
{
    uapi_pin_set_mode(DEFAULT_UART_TXD_PIN, PIN_MODE_1);
    uapi_pin_set_mode(DEFAULT_UART_RXD_PIN, PIN_MODE_1);
}

static void app_uart_init_config(void)
{
    uart_attr_t default_uart_attr = {
        .baud_rate = DEFAULT_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };

    uart_pin_config_t default_uart_pin_config = {
        .tx_pin = DEFAULT_UART_TXD_PIN,
        .rx_pin = DEFAULT_UART_RXD_PIN, 
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    uart_extra_attr_t extra_attr = {
        .tx_dma_enable = true,
        .tx_int_threshold = UART_FIFO_INT_TX_LEVEL_EQ_0_CHARACTER,
        .rx_dma_enable = true,
        .rx_int_threshold = UART_FIFO_INT_RX_LEVEL_1_CHARACTER
    };
    uapi_dma_init();
    uapi_dma_open();
    uapi_uart_deinit(DEFALUT_UART_BUS);
    uapi_uart_init(DEFALUT_UART_BUS, &default_uart_pin_config, &default_uart_attr, &extra_attr, &default_uart_buffer_config);
}

static void app_uart_read_int_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        osal_printk("uart%d int mode transfer illegal data!\r\n", DEFALUT_UART_BUS);
        return;
    }

    uint8_t *buff = (uint8_t *)buffer;
    osal_printk("uart%d  read data: ", DEFALUT_UART_BUS);
    for (uint16_t i = 0; i < length; i++) {
        osal_printk("%c ", buff[i]);
    }
    osal_printk("\r\n");
    if (default_uart_int_index + length > MAX_TRANSFER_SIZE) {
        default_uart_int_index = 0;
    }
    if (memcpy_s(default_uart_int_rx_buff + default_uart_int_index, length, buff, length) != EOK) {
        default_uart_int_index = 0;
        osal_printk("uart%d int mode data2 copy fail!\r\n", DEFALUT_UART_BUS);
    }
    default_uart_int_index += length;
    default_uart_int_rx_flag = 1;
}

static void app_uart_register_rx_callback(void)
{
    osal_printk("uart%d int mode register receive callback start!\r\n", DEFALUT_UART_BUS);
    if (uapi_uart_register_rx_callback(DEFALUT_UART_BUS, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE,
                                       1, app_uart_read_int_handler) == ERRCODE_SUCC) {
        osal_printk("uart%d int mode register receive callback succ!\r\n", DEFALUT_UART_BUS );
    }
}

static void *uart_task(void)
{
    app_uart_init_pin();
    app_uart_init_config();
    app_uart_register_rx_callback();

    while (1) {
        uint16_t recv_len;
        int32_t dma_ret;

        while (default_uart_int_rx_flag != 1) {
            osal_msleep(5);
        }

        default_uart_int_rx_flag = 0;
        recv_len = default_uart_int_index;
        if (recv_len == 0) {
            osal_printk("uart%d dma mode skip empty payload!\r\n", DEFALUT_UART_BUS);
            continue;
        }

        osal_printk("uart%d dma mode send back, len=%u\r\n", DEFALUT_UART_BUS, recv_len);
        dma_ret = uapi_uart_write_by_dma(DEFALUT_UART_BUS, default_uart_rx_buff, recv_len, &default_uart_dma_cfg);
        app_uart_log_dma_result(dma_ret, recv_len);
        default_uart_int_index = 0;
    }

    return NULL;
}

static void uart_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)uart_task, 0, "UartTask", UART_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, UART_TASK_PRIO);
    }
    osal_kthread_unlock();
}

/* Run the uart_entry. */
app_run(uart_entry);
