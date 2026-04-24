#include "dtu_service_inner.h"

#include <stdarg.h>

#include "securec.h"

#define DTU_LOG_PREFIX "[DTU LOG]"

/* 统一日志模块职责：
 * 1. 收敛 DTU 业务日志，只保留启动、提交、读写配置、reject、错误几类日志
 * 2. 避免 mode / storage / channel 各处直接散落 osal_printk
 * 3. 让日志风格保持一致，后续增减日志点时只需集中修改本文件
 */

/* ========================================================================== */
/* 私有打印辅助区                                                             */
/* 说明：                                                                     */
/* 1. 这里只放日志内部复用的小工具。                                          */
/* 2. 这些函数不对外导出，避免其它模块直接绕开统一日志接口。                  */
/* ========================================================================== */

/* 打印 MAC 地址，统一使用 AA:BB:CC:DD:EE:FF 这种紧凑格式。 */
static void dtu_log_print_mac(const uint8_t *mac)
{
    osal_printk("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* 打印统一前缀日志行。 */
static void dtu_log_printf(const char *fmt, ...)
{
    char buf[256] = {0};
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s <format failed>\r\n", DTU_LOG_PREFIX);
        return;
    }
    osal_printk("%s %s\r\n", DTU_LOG_PREFIX, buf);
}

/* 打印一行串口配置摘要。 */
static void dtu_log_print_uart_cfg(const char *prefix, const dtu_uart_cfg_t *cfg)
{
    dtu_log_printf("%s uart: baud=%u parity=%s stop=%u data=%u",
        prefix, dtu_storage_uart_baudrate(cfg->baud_level), dtu_storage_parity_name(cfg->parity),
        cfg->stop_bits, cfg->data_bits);
}

/* 打印完整 Modbus 配置表。 */
static void dtu_log_print_modbus_cfg(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s modbus_count=%u", prefix, cfg->modbus_count);
    for (uint8_t i = 0; i < cfg->modbus_count; i++) {
        dtu_log_printf("%s modbus[%u]: addr=%u dev_type=0x%02X",
            prefix, i, cfg->modbus[i].addr, cfg->modbus[i].dev_type);
    }
}

/* 打印完整白名单内容。 */
static void dtu_log_print_whitelist(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s whitelist_count=%u", prefix, cfg->wl_count);
    for (uint8_t i = 0; i < cfg->wl_count; i++) {
        osal_printk("%s %s whitelist[%u]: mac=", DTU_LOG_PREFIX, prefix, i);
        dtu_log_print_mac(cfg->whitelist[i].mac);
        osal_printk(" name=%.*s\r\n", cfg->whitelist[i].name_len, cfg->whitelist[i].name);
        dtu_log_print_uart_cfg("  node_cfg", &cfg->whitelist[i].uart_cfg);
        dtu_log_printf("  node_cfg modbus_count=%u", cfg->whitelist[i].modbus_count);
        for (uint8_t j = 0; j < cfg->whitelist[i].modbus_count; j++) {
            dtu_log_printf("  node_cfg modbus[%u]: addr=%u dev_type=0x%02X",
                j, cfg->whitelist[i].modbus[j].addr, cfg->whitelist[i].modbus[j].dev_type);
        }
    }
}

/* 打印完整运行配置快照。
 * 这类日志只在 boot / commit / factory reset 后使用，便于一次性看全量状态。
 */
static void dtu_log_runtime_snapshot(const char *prefix)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t mac[WIFI_MAC_LEN] = {0};
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len;

    dtu_storage_get_device_mac(mac);
    name_len = dtu_storage_get_device_name(name, sizeof(name));

    dtu_log_printf("%s begin", prefix);
    dtu_log_printf("%s mode: current=%s source=DIP pin=%u level=%s rx_profile=%s",
        prefix,
        dtu_storage_mode_name(dtu_storage_current_mode()),
        (uint32_t)DTU_CFG_MODE_SWITCH_PIN,
        (dtu_storage_current_mode() == DTU_MODE_CONFIG) ? "HIGH" : "LOW",
        dtu_storage_rx_profile_name(dtu_storage_rx_profile()));
    osal_printk("%s %s role=%s mac=", DTU_LOG_PREFIX, prefix, dtu_storage_role_name(cfg->role));
    dtu_log_print_mac(mac);
    osal_printk(" name=%.*s\r\n", name_len, (const char *)name);
    dtu_log_print_uart_cfg(prefix, &cfg->uart_cfg);
    dtu_log_print_modbus_cfg(prefix, cfg);
    dtu_log_printf("%s power=%u", prefix, cfg->power);
    dtu_log_print_whitelist(prefix, cfg);
    dtu_log_printf("%s end", prefix);
}

/* ========================================================================== */
/* 错误日志区                                                                 */
/* 说明：                                                                     */
/* 1. 错误日志统一带 DTU error 前缀。                                          */
/* 2. 这样串口上能快速把错误和普通业务日志区分开。                            */
/* ========================================================================== */

/* 打印统一错误日志。 */
void dtu_log_error(const char *fmt, ...)
{
    char buf[192] = {0};
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s error: <format failed>\r\n", DTU_LOG_PREFIX);
        return;
    }
    osal_printk("%s error: %s\r\n", DTU_LOG_PREFIX, buf);
}

/* ========================================================================== */
/* 生命周期日志区                                                             */
/* ========================================================================== */

/* 启动完成后打印当前已加载配置。 */
void dtu_log_boot(errcode_t load_ret)
{
    dtu_log_printf("DTU cfg load ret=0x%x", load_ret);
    dtu_log_runtime_snapshot("DTU boot config");
}

/* COMMIT 成功后打印当前完整配置快照。 */
void dtu_log_commit(void)
{
    dtu_log_runtime_snapshot("DTU commit config");
}

/* 恢复出厂后打印新的完整配置快照。 */
void dtu_log_factory_reset(void)
{
    dtu_log_runtime_snapshot("DTU factory config");
}

/* ========================================================================== */
/* 配置读取日志区                                                             */
/* ========================================================================== */

/* 打印设备信息读取日志。 */
void dtu_log_cfg_read_dev_info(void)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len = dtu_storage_get_device_name(name, sizeof(name));

    dtu_log_printf("DTU cfg read dev_info: role=%s name=%.*s",
        dtu_storage_role_name(cfg->role), name_len, (const char *)name);
}

/* 打印串口配置读取日志。 */
void dtu_log_cfg_read_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg read", cfg);
}

/* 打印 Modbus 配置读取日志。 */
void dtu_log_cfg_read_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg read", dtu_storage_runtime_const());
}

/* 打印白名单读取日志。 */
void dtu_log_cfg_read_whitelist(void)
{
    dtu_log_print_whitelist("DTU cfg read", dtu_storage_runtime_const());
}

/* 打印 ROOT 功率读取日志。 */
void dtu_log_cfg_read_power(uint8_t power)
{
    dtu_log_printf("DTU cfg read power=%u", power);
}

/* 打印白名单 node 子配置读取日志。 */
void dtu_log_cfg_read_wl_node(const dtu_wl_item_t *item)
{
    if (item == NULL) {
        return;
    }
    osal_printk("%s DTU cfg read wl_node: mac=", DTU_LOG_PREFIX);
    dtu_log_print_mac(item->mac);
    osal_printk(" name=%.*s\r\n", item->name_len, item->name);
    dtu_log_print_uart_cfg("DTU cfg read wl_node", &item->uart_cfg);
    dtu_log_printf("DTU cfg read wl_node modbus_count=%u", item->modbus_count);
    for (uint8_t i = 0; i < item->modbus_count; i++) {
        dtu_log_printf("DTU cfg read wl_node modbus[%u]: addr=%u dev_type=0x%02X",
            i, item->modbus[i].addr, item->modbus[i].dev_type);
    }
}

/* ========================================================================== */
/* 配置写入日志区                                                             */
/* ========================================================================== */

/* 打印角色写入日志。 */
void dtu_log_cfg_write_role(uint8_t role)
{
    dtu_log_printf("DTU cfg set role=%s", dtu_storage_role_name(role));
}

/* 打印串口配置写入日志。 */
void dtu_log_cfg_write_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg set", cfg);
}

/* 打印 Modbus 配置写入日志。 */
void dtu_log_cfg_write_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg set", dtu_storage_runtime_const());
}
  
/* 打印 ROOT 功率写入日志。 */
void dtu_log_cfg_write_power(uint8_t power)
{
    dtu_log_printf("DTU cfg set power=%u", power);
}

/* 打印白名单写入日志。 */
void dtu_log_cfg_write_whitelist(void)
{
    dtu_log_print_whitelist("DTU cfg set", dtu_storage_runtime_const());
}

/* 打印白名单 node 子配置写入日志。 */
void dtu_log_cfg_write_wl_node(const dtu_wl_item_t *item)
{
    if (item == NULL) {
        return;
    }
    osal_printk("%s DTU cfg set wl_node: mac=", DTU_LOG_PREFIX);
    dtu_log_print_mac(item->mac);
    osal_printk(" name=%.*s\r\n", item->name_len, item->name);
    dtu_log_print_uart_cfg("DTU cfg set wl_node", &item->uart_cfg);
    dtu_log_printf("DTU cfg set wl_node modbus_count=%u", item->modbus_count);
    for (uint8_t i = 0; i < item->modbus_count; i++) {
        dtu_log_printf("DTU cfg set wl_node modbus[%u]: addr=%u dev_type=0x%02X",
            i, item->modbus[i].addr, item->modbus[i].dev_type);
    }
}

/* ========================================================================== */
/* Reject 日志区                                                              */
/* ========================================================================== */

/* 打印运行模式下的拒配日志。 */
void dtu_log_cfg_reject(dtu_transport_id_t transport_id, uint8_t cmd)
{
    dtu_log_printf("DTU reject: transport=%s cmd=%s mode=%s",
        dtu_service_transport_name(transport_id),
        dtu_service_cmd_name(cmd),
        dtu_storage_mode_name(dtu_storage_current_mode()));
}
