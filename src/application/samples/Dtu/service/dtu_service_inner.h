#ifndef DTU_SERVICE_INNER_H
#define DTU_SERVICE_INNER_H

/* DTU 内部统一入口：
 * 1. 汇总配置常量、共享类型和跨模块接口
 * 2. 所有内部模块优先 include 这个头，减少散乱依赖
 * 3. 适配当前 service + flow + mode + protocol + storage 的分层协作
 */

#include "dtu_build_config.h"
#include "dtu_types.h"
#include "osal_debug.h"
#include "dtu_protocol.h"
#include "dtu_storage.h"

/* ==================== 通道接口（原 dtu_channel.h） ==================== */
typedef struct {
    const char *name;
    errcode_t (*init)(void);
    errcode_t (*send)(const uint8_t *data, uint16_t len);
} dtu_transport_if_t;

extern const dtu_transport_if_t g_dtu_uart_transport;
extern const dtu_transport_if_t g_dtu_ble_transport;

/* ==================== 模式分发接口（原 dtu_mode.h） ==================== */
typedef void (*dtu_cmd_handler_t)(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

typedef struct {
    uint8_t cmd_id;
    dtu_cmd_handler_t handler;
} dtu_cmd_entry_t;

typedef struct {
    const char *name;
    void (*dispatch)(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
} dtu_mode_if_t;

void dtu_mode_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
void dtu_mode_run_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

extern const dtu_mode_if_t g_dtu_mode_config_if;
extern const dtu_mode_if_t g_dtu_mode_run_if;

/* ==================== 公共工具 ==================== */
/* 返回 transport 接口对象，供 sender/init 统一使用。 */
const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id);
/* 返回命令名称（日志使用）。 */
const char *dtu_service_cmd_name(uint8_t cmd);
/* 返回 transport 名称（日志使用）。 */
const char *dtu_service_transport_name(dtu_transport_id_t transport_id);
/* 在命令表中查找并执行 handler，命中返回 true。 */
bool dtu_service_dispatch_table(const dtu_cmd_entry_t *table, uint32_t table_size,
    dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 统一日志接口 ==================== */
void dtu_log_error(const char *fmt, ...);
void dtu_log_boot(errcode_t load_ret);
void dtu_log_commit(void);
void dtu_log_factory_reset(void);
void dtu_log_cfg_read_dev_info(void);
void dtu_log_cfg_read_uart(const dtu_uart_cfg_t *cfg);
void dtu_log_cfg_read_modbus(void);
void dtu_log_cfg_read_whitelist(void);
void dtu_log_cfg_read_power(uint8_t power);
void dtu_log_cfg_read_wl_node(const dtu_wl_item_t *item);
void dtu_log_cfg_write_role(uint8_t role);
void dtu_log_cfg_write_uart(const dtu_uart_cfg_t *cfg);
void dtu_log_cfg_write_modbus(void);
void dtu_log_cfg_write_power(uint8_t power);
void dtu_log_cfg_write_whitelist(void);
void dtu_log_cfg_write_wl_node(const dtu_wl_item_t *item);
void dtu_log_cfg_reject(dtu_transport_id_t transport_id, uint8_t cmd);

/* ==================== 输入入口 ==================== */
/* 由 transport 提交原始字节流。 */
void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

/* ==================== 分发入口 ==================== */
/* 由 parser 提交完整帧，router 负责公共命令与模式分发。 */
void dtu_service_on_frame(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 响应发送 ==================== */
/* 发送完整响应帧。 */
void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq,
    const uint8_t *body, uint16_t body_len);
/* 发送仅状态码响应帧。 */
void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status);

/* ==================== 运行统计 ==================== */
void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used);
void dtu_service_trace_rx_task_wakeup(void);

#endif
