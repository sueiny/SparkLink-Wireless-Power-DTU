#ifndef DTU_BUILD_CONFIG_H
#define DTU_BUILD_CONFIG_H

/* DTU 构建与协议常量：
 * 1. 放固定硬件映射、日志开关、资源限制、命令字、状态码
 * 2. 不放函数声明和复杂类型
 * 3. 作为所有模块共享的常量定义入口
 */

#include "gpio.h"
#include "pinctrl.h"
#include "uart.h"

/* ==================== 固定硬件映射区 ==================== */
#define DTU_CFG_UART_BUS                      UART_BUS_0
#define DTU_CFG_UART_TX_PIN                   S_MGPIO17
#define DTU_CFG_UART_RX_PIN                   S_MGPIO18
#define DTU_CFG_UART_PIN_MODE                 PIN_MODE_1
#define DTU_CFG_MODE_SWITCH_PIN               S_MGPIO13
#define DTU_CFG_MODE_SWITCH_PIN_MODE          PIN_MODE_0
#define DTU_CFG_MODE_SWITCH_PIN_PULL          PIN_PULL_TYPE_UP

/* ==================== 日志与调试开关区 ==================== */
#if defined(CONFIG_DTU_TRACE_LOG)
#define DTU_CFG_LOG_TRACE_ENABLE              1
#else
#define DTU_CFG_LOG_TRACE_ENABLE              0
#endif

#if defined(CONFIG_DTU_DEVICE_NAME)
#define DTU_CFG_DEVICE_NAME                   CONFIG_DTU_DEVICE_NAME
#else
#define DTU_CFG_DEVICE_NAME                   "DTU_N01"
#endif

#if defined(CONFIG_DTU_FIXED_MAC)
#define DTU_CFG_FIXED_MAC                     CONFIG_DTU_FIXED_MAC
#else
#define DTU_CFG_FIXED_MAC                     "A1:A2:A3:A4:A5:A6"
#endif

/* ==================== 模式与串口 profile 区 ==================== */
#define DTU_CFG_MODE_CONFIG_RX_NOTIFY_LENGTH  1
#define DTU_CFG_MODE_RUN_RX_NOTIFY_LENGTH     32
#define DTU_CFG_MODE_CONFIG_RX_INT_THRESHOLD  UART_FIFO_INT_RX_LEVEL_1_CHARACTER
#define DTU_CFG_MODE_RUN_RX_INT_THRESHOLD     UART_FIFO_INT_RX_LEVEL_1_2

/* ==================== 协议与资源限制区 ==================== */
#define DTU_CFG_RX_DRIVER_BUFFER_SIZE         512
#define DTU_CFG_RING_BUFFER_SIZE              2048
#define DTU_CFG_RUN_PACKET_MAX_PAYLOAD        192
#define DTU_CFG_RUN_PACKET_HEADER_SIZE        10
#define DTU_CFG_MAX_FRAME_BODY                192
#define DTU_CFG_MAX_NAME_LEN                  31
#define DTU_CFG_MAX_MODBUS_ITEMS              8
#define DTU_CFG_MAX_WL_ITEMS                  128
#define DTU_CFG_NV_WL_SHARD_COUNT             8
#define DTU_CFG_NV_WL_ITEMS_PER_SHARD         16
#define DTU_CFG_WL_FRAGMENT_BODY_MAX          89
#define DTU_CFG_NV_MAGIC                      0x44545532U
#define DTU_CFG_NV_VERSION                    0x0005

#define DTU_CFG_ROLE_NODE                     0x00
#define DTU_CFG_ROLE_ROOT                     0x01

#define DTU_CFG_STATUS_SUCC                   0x00
#define DTU_CFG_STATUS_CRC_ERR                0x01
#define DTU_CFG_STATUS_LEN_ERR                0x02
#define DTU_CFG_STATUS_CMD_ERR                0x03
#define DTU_CFG_STATUS_PARAM_ERR              0x04
#define DTU_CFG_STATUS_NOT_CONFIG             0x05
#define DTU_CFG_STATUS_ROLE_MISMATCH          0x06
#define DTU_CFG_STATUS_WL_FULL                0x07
#define DTU_CFG_STATUS_NOT_FOUND              0x08
#define DTU_CFG_STATUS_SAVE_FAIL              0x09
#define DTU_CFG_STATUS_BUSY                   0x0A

#define DTU_CFG_CMD_READ_DEV_INFO             0x01
#define DTU_CFG_CMD_READ_UART_CFG             0x02
#define DTU_CFG_CMD_READ_MODBUS_CFG           0x03
#define DTU_CFG_CMD_READ_ROOT_WL_ALL          0x04
#define DTU_CFG_CMD_READ_ROOT_POWER           0x05
#define DTU_CFG_CMD_GET_MODE_STATUS           0x06
#define DTU_CFG_CMD_READ_WL_NODE_CFG          0x07
#define DTU_CFG_CMD_SET_ROLE                  0x10
#define DTU_CFG_CMD_SET_UART_CFG              0x11
#define DTU_CFG_CMD_SET_MODBUS_CFG            0x12
#define DTU_CFG_CMD_SET_ROOT_POWER            0x13
#define DTU_CFG_CMD_ADD_WL_ITEM               0x14
#define DTU_CFG_CMD_DEL_WL_ITEM               0x15
#define DTU_CFG_CMD_CLEAR_WL                  0x16
#define DTU_CFG_CMD_SET_WL_NODE_CFG           0x17
#define DTU_CFG_CMD_COMMIT                    0x20
#define DTU_CFG_CMD_REBOOT                    0x21
#define DTU_CFG_CMD_FACTORY_RESET             0x22

#define DTU_CFG_SOF0                          0xAA
#define DTU_CFG_SOF1                          0x55

#endif
