#include "app_init.h"
#include "common_def.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "gpio.h"
#include "pinctrl.h"
#include "string.h"

#include "common_uart.h"
#include "cloud_common.h"

uint8_t gnss_rx_buffer[MAX_BUFFER_SIZE];
uint8_t l610_rx_buffer[MAX_BUFFER_SIZE];

uart_buffer_config_t gnss_buffer_config = {.rx_buffer = gnss_rx_buffer, .rx_buffer_size = MAX_BUFFER_SIZE}; // gnss uart

uart_buffer_config_t l610_buffer_config = {.rx_buffer = l610_rx_buffer, .rx_buffer_size = MAX_BUFFER_SIZE}; // l610 uart

void dtu_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    unused(error);
//    osal_printk("From the dtu buffer :\r\n");
    uapi_uart_write(DEFAULT_UART_BUS, (uint8_t *)buffer, length, 0);
//    osal_printk("End by the dtu buffer!!!\r\n");


}

void print_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    // osal_printk("From the print buffer :\r\n");
    
    // // 打印接收到的 HEX 原码内容
    // for (uint16_t i = 0; i < length; i++) {
    //     osal_printk("%02X ", ((const uint8_t *)buffer)[i]);
    // }
    // osal_printk("\r\n");

    // uapi_uart_write(DEFAULT_UART_BUS, (uint8_t *)buffer, length, 0);
    uapi_uart_write(GNSS_UART_BUS, (uint8_t *)buffer, length, 0);
    // osal_printk("\r\nEnd by the print buffer!!!\r\n");
}

void dtu_task(void)
{
    while (1) {
        // osal_printk("DTU task is running...\r\n");
        osal_msleep(5000); // Sleep for 5 seconds
    }
}

errcode_t uart_init(void)
{

    uapi_pin_set_mode(GNSS_TXD_PIN, (pin_mode_t)GNSS_TXD_MODE);
    uapi_pin_set_mode(GNSS_RXD_PIN, (pin_mode_t)GNSS_RXD_MODE); // gnss

    uapi_pin_set_mode(L610_TXD_PIN, (pin_mode_t)L610_UART_TXD_PIN_MODE);
    uapi_pin_set_mode(L610_RXD_PIN, (pin_mode_t)L610_UART_RXD_PIN_MODE); // l610

    uart_attr_t gnss_attr = {.baud_rate = GNSS_UART_BAUDRATE,
                             .data_bits = UART_DATA_BIT_8,
                             .stop_bits = UART_STOP_BIT_1,
                             .parity = UART_PARITY_NONE};

    uart_pin_config_t gnss_pin_config = {
        .tx_pin = GNSS_TXD_PIN, .rx_pin = GNSS_RXD_PIN, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE}; // gnss

    uart_attr_t l610_attr = {.baud_rate = L610_UART_BAUDRATE,
                             .data_bits = UART_DATA_BIT_8,
                             .stop_bits = UART_STOP_BIT_1,
                             .parity = UART_PARITY_NONE};

    uart_pin_config_t l610_pin_config = {
        .tx_pin = L610_TXD_PIN, .rx_pin = L610_RXD_PIN, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE}; // print

    uapi_uart_deinit(GNSS_UART_BUS);
    uapi_uart_deinit(L610_UART_BUS);

    errcode_t ret = uapi_uart_init(GNSS_UART_BUS, &gnss_pin_config, &gnss_attr, NULL, &gnss_buffer_config);
    if (ret != ERRCODE_SUCC) {
        osal_printk("GNSS UART init failed: 0x%x\r\n", ret);
        return ret;
    }

    ret = uapi_uart_register_rx_callback(GNSS_UART_BUS, UART_RX_CONDITION_FULL_OR_IDLE, 1, dtu_uart_rx_callback);
    if (ret != ERRCODE_SUCC) {
        osal_printk("GNSS Register callback failed: 0x%x\r\n", ret);
        return ret;
    } // gnss

    ret = uapi_uart_init(L610_UART_BUS, &l610_pin_config, &l610_attr, NULL, &l610_buffer_config);
    if (ret != ERRCODE_SUCC) {
        osal_printk("L610 UART init failed: 0x%x\r\n", ret);
        return ret;
    }

    ret = uapi_uart_register_rx_callback(L610_UART_BUS, UART_RX_CONDITION_FULL_OR_IDLE, 1, print_uart_rx_callback);
    if (ret != ERRCODE_SUCC) {
        osal_printk("L610 Register callback failed: 0x%x\r\n", ret);
        return ret;
    } // l610
    osal_printk("UART init success!!\r\n");
    return ERRCODE_SUCC;
}
