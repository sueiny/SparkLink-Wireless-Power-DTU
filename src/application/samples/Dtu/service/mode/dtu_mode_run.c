#include "dtu_service_inner.h"

/* 运行模式职责：
 * 1. 明确运行态允许和禁止的命令边界
 * 2. 通过命令表注册允许处理的运行态命令
 * 3. 未注册的配置类命令统一拒绝
 */

/* ==================== Reject 处理区 ==================== */
/* 说明：
 * 1. 当前 RUN 模式还没有完整业务协议，所以配置类命令统一拒绝。
 * 2. reject 日志统一交给 dtu_log.c，避免这里自己拼日志字符串。
 */

/* 拒绝运行模式下不允许的配置命令。 */
static void dtu_mode_run_reject(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    dtu_log_cfg_reject(transport_id, frame->cmd);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_NOT_CONFIG);
}

/* ==================== 运行模式命令表区 ==================== */
/* 说明：
 * 1. 这里显式列出 RUN 模式下“不允许作为配置命令执行”的指令。
 * 2. 未来如果 RUN 模式加入真正业务命令，可以在这里新增允许处理项。
 */
static const dtu_cmd_entry_t g_dtu_run_cmd_table[] = {
    { DTU_CFG_CMD_READ_DEV_INFO, dtu_mode_run_reject },
    { DTU_CFG_CMD_READ_UART_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_READ_MODBUS_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_READ_ROOT_WL_ALL, dtu_mode_run_reject },
    { DTU_CFG_CMD_READ_ROOT_POWER, dtu_mode_run_reject },
    { DTU_CFG_CMD_READ_WL_NODE_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_SET_ROLE, dtu_mode_run_reject },
    { DTU_CFG_CMD_SET_UART_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_SET_MODBUS_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_SET_ROOT_POWER, dtu_mode_run_reject },
    { DTU_CFG_CMD_ADD_WL_ITEM, dtu_mode_run_reject },
    { DTU_CFG_CMD_DEL_WL_ITEM, dtu_mode_run_reject },
    { DTU_CFG_CMD_CLEAR_WL, dtu_mode_run_reject },
    { DTU_CFG_CMD_SET_WL_NODE_CFG, dtu_mode_run_reject },
    { DTU_CFG_CMD_COMMIT, dtu_mode_run_reject },
    { DTU_CFG_CMD_FACTORY_RESET, dtu_mode_run_reject }
};

/* ==================== 分发入口区 ==================== */

/* 运行模式命令表分发入口。 */
void dtu_mode_run_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (dtu_service_dispatch_table(g_dtu_run_cmd_table,
        (uint32_t)(sizeof(g_dtu_run_cmd_table) / sizeof(g_dtu_run_cmd_table[0])),
        transport_id, frame)) {
        return;
    }

    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_CMD_ERR);
}

const dtu_mode_if_t g_dtu_mode_run_if = {
    .name = "RUN",
    .dispatch = dtu_mode_run_dispatch
};
