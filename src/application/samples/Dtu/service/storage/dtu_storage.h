#ifndef DTU_STORAGE_H
#define DTU_STORAGE_H

/* storage 头文件：
 * 1. 描述运行配置、模式状态和持久化接口
 * 2. 其他模块只通过这些函数访问状态，不直接持有全局变量
 */

#include <stdbool.h>
#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"
#include "uart.h"

dtu_runtime_cfg_t *dtu_storage_runtime(void);
const dtu_runtime_cfg_t *dtu_storage_runtime_const(void);
dtu_mode_t dtu_storage_current_mode(void);
bool dtu_storage_is_reboot_pending(void);
void dtu_storage_set_current_mode(dtu_mode_t mode);
void dtu_storage_set_reboot_pending(bool pending);
bool dtu_storage_is_valid_mode(uint8_t mode);
bool dtu_storage_is_valid_role(uint8_t role);
bool dtu_storage_is_valid_uart_cfg(const dtu_uart_cfg_t *cfg);
bool dtu_storage_is_valid_dev_type(uint8_t dev_type);
int32_t dtu_storage_find_wl_item(const uint8_t *mac);
void dtu_storage_init_wl_item_cfg(dtu_wl_item_t *item);
dtu_rx_profile_t dtu_storage_rx_profile(void);
uint16_t dtu_storage_rx_notify_length(void);
uint8_t dtu_storage_rx_int_threshold(void);
uint32_t dtu_storage_uart_baudrate(uint8_t baud_level);
void dtu_storage_fill_uart_attr(uart_attr_t *uart_attr, const dtu_uart_cfg_t *cfg);
void dtu_storage_set_default(dtu_runtime_cfg_t *cfg);
void dtu_storage_get_device_mac(uint8_t *mac);
uint8_t dtu_storage_get_device_name(uint8_t *name_buf, uint8_t name_buf_len);
const char *dtu_storage_role_name(uint8_t role);
const char *dtu_storage_parity_name(uint8_t parity);
const char *dtu_storage_mode_name(dtu_mode_t mode);
const char *dtu_storage_rx_profile_name(dtu_rx_profile_t profile);
errcode_t dtu_storage_load(void);
errcode_t dtu_storage_commit(void);
errcode_t dtu_storage_factory_reset(void);

#endif
