#include "dtu_service.h"
#include "dtu_service_inner.h"

#define DTU_SERVICE_MAX_FRAME_SIZE (DTU_CFG_MAX_FRAME_BODY + 8)

/* flow 子模块职责：
 * 1. 统一收拢输入、分发、输出三条主链路，减少 service 层来回跳转
 * 2. 对外仍保留 on_bytes / on_frame / reply 这组稳定入口
 * 3. 只负责协议流转，不承担配置存储与 transport 初始化职责
 */

/* ========================================================================== */
/* 解析器上下文区                                                             */
/* 说明：                                                                     */
/* 1. 每个 transport 各自拥有一份协议解析器状态。                             */
/* 2. 这样 UART/BLE/SLE 即使未来同时存在，也不会互相污染协议状态机。          */
/* 3. parser 只在第一次收到该 transport 数据时初始化一次。                    */
/* ========================================================================== */

static dtu_protocol_parser_t g_dtu_parsers[DTU_TRANSPORT_MAX];
static bool g_dtu_parser_ready[DTU_TRANSPORT_MAX];

/* ========================================================================== */
/* 模式路由表区                                                               */
/* 说明：                                                                     */
/* 1. flow 不直接写死“CONFIG 调哪个函数、RUN 调哪个函数”的 if-else。          */
/* 2. 统一通过 mode 接口表取 dispatch 入口。                                  */
/* 3. 后续如果新增模式，这里是最明显的扩展点。                                */
/* ========================================================================== */

static const dtu_mode_if_t *g_dtu_mode_table[] = {
    [DTU_MODE_CONFIG] = &g_dtu_mode_config_if,
    [DTU_MODE_RUN] = &g_dtu_mode_run_if
};

/* ========================================================================== */
/* 基础辅助区                                                                 */
/* ========================================================================== */

/* 获取指定 transport 的 parser。
 * 第一次访问时完成初始化，后续直接复用已有状态。
 */
static dtu_protocol_parser_t *dtu_flow_get_parser(dtu_transport_id_t transport_id)
{
    if (!g_dtu_parser_ready[transport_id]) {
        dtu_protocol_parser_init(&g_dtu_parsers[transport_id]);
        g_dtu_parser_ready[transport_id] = true;
    }
    return &g_dtu_parsers[transport_id];
}

/* 根据当前模式找到对应的 mode 接口对象。 */
static const dtu_mode_if_t *dtu_flow_get_mode_if(dtu_mode_t mode)
{
    if ((uint8_t)mode >= (sizeof(g_dtu_mode_table) / sizeof(g_dtu_mode_table[0]))) {
        return NULL;
    }
    return g_dtu_mode_table[mode];
}

/* ========================================================================== */
/* 公共命令处理区                                                             */
/* 说明：                                                                     */
/* 1. 这里处理不属于 config/run 私有语义的公共命令。                          */
/* 2. 当前只保留 GET_MODE_STATUS 和 REBOOT。                                  */
/* 3. 其余命令统一交给 mode 层。                                               */
/* ========================================================================== */

/* 处理 GET_MODE_STATUS：
 * 返回当前模式、角色、串口参数、固定模式来源标记、RX profile 和 reboot 状态。
 * 当前模式来源已经固定为 DIP，因此这里直接回传 DIP 标记值。
 */
static void dtu_flow_handle_get_mode_status(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    uint8_t body[10];
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();

    if (frame->len != 0) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    body[0] = DTU_CFG_STATUS_SUCC;
    body[1] = (uint8_t)dtu_storage_current_mode();
    body[2] = cfg->role;
    body[3] = cfg->uart_cfg.baud_level;
    body[4] = cfg->uart_cfg.parity;
    body[5] = cfg->uart_cfg.stop_bits;
    body[6] = cfg->uart_cfg.data_bits;
    body[7] = 0x01; /* mode_source 固定为 DIP */
    body[8] = (uint8_t)dtu_storage_rx_profile();
    body[9] = dtu_storage_is_reboot_pending() ? 1U : 0U;
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, sizeof(body));
}

/* 处理 REBOOT：
 * 当前不立即直接重启，而是先置位 reboot_pending，
 * 由 transport 任务在合适时机执行真正重启。
 */
static void dtu_flow_handle_reboot(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame->len != 0) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    dtu_storage_set_reboot_pending(true);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 先尝试处理公共命令；命中返回 true，未命中继续交给 mode 层。 */
static bool dtu_flow_handle_common(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    switch (frame->cmd) {
        case DTU_CFG_CMD_GET_MODE_STATUS:
            dtu_flow_handle_get_mode_status(transport_id, frame);
            return true;
        case DTU_CFG_CMD_REBOOT:
            dtu_flow_handle_reboot(transport_id, frame);
            return true;
        default:
            return false;
    }
}

/* ========================================================================== */
/* 输入主链路区                                                               */
/* 说明：                                                                     */
/* 1. transport 把原始字节喂进来后，flow 负责逐字节推进协议状态机。           */
/* 2. 一旦组出完整帧，立即上抛到 on_frame。                                   */
/* 3. CRC/LEN 这种协议错误在这里统一转换成状态码回包。                        */
/* ========================================================================== */

/* 原始字节流入口。 */
void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    dtu_protocol_parser_t *parser;

    if (transport_id >= DTU_TRANSPORT_MAX || data == NULL || len == 0) {
        return;
    }

    parser = dtu_flow_get_parser(transport_id);
    for (uint16_t i = 0; i < len; i++) {
        dtu_frame_t frame = {0};
        dtu_protocol_status_t status = dtu_protocol_feed_byte(parser, data[i], &frame);

        switch (status) {
            case DTU_PROTOCOL_STATUS_OK:
                dtu_service_on_frame(transport_id, &frame);
                break;
            case DTU_PROTOCOL_STATUS_CRC_ERR:
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_CRC_ERR);
                break;
            case DTU_PROTOCOL_STATUS_LEN_ERR:
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_LEN_ERR);
                break;
            case DTU_PROTOCOL_STATUS_INCOMPLETE:
            case DTU_PROTOCOL_STATUS_INVALID_SOF:
            default:
                break;
        }
    }
}

/* ========================================================================== */
/* 分发主链路区                                                               */
/* 说明：                                                                     */
/* 1. 完整帧先经过 reboot pending 拦截。                                      */
/* 2. 然后尝试命中公共命令。                                                  */
/* 3. 最后按 current_mode 路由到 config/run。                                 */
/* 4. 这部分是整个 DTU 协议主链最核心的“中央路由”。                           */
/* ========================================================================== */

/* 完整帧入口。 */
void dtu_service_on_frame(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    const dtu_mode_if_t *mode_if;

    if (frame == NULL) {
        return;
    }

    if (dtu_storage_is_reboot_pending()) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_BUSY);
        return;
    }

    if (dtu_flow_handle_common(transport_id, frame)) {
        return;
    }

    mode_if = dtu_flow_get_mode_if(dtu_storage_current_mode());
    if (mode_if == NULL || mode_if->dispatch == NULL) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_CMD_ERR);
        return;
    }

    mode_if->dispatch(transport_id, frame);
}

/* ========================================================================== */
/* 输出主链路区                                                               */
/* 说明：                                                                     */
/* 1. 所有业务回复最终都统一走这里。                                          */
/* 2. 先由 protocol 打包完整帧，再交给具体 transport 发送。                   */
/* 3. 业务层不需要知道 UART DMA / BLE notify 等发送细节。                     */
/* ========================================================================== */

/* 发送完整响应帧。 */
void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);
    uint8_t frame[DTU_SERVICE_MAX_FRAME_SIZE];
    uint16_t frame_len = 0;
    errcode_t ret;

    if (transport_if == NULL || transport_if->send == NULL) {
        dtu_log_error("tx transport unsupported: %s", dtu_service_transport_name(transport_id));
        return;
    }

    ret = dtu_protocol_pack_response(cmd, seq, body, body_len, frame, sizeof(frame), &frame_len);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx pack failed: cmd=%s ret=0x%X", dtu_service_cmd_name(cmd), ret);
        return;
    }

    ret = transport_if->send(frame, frame_len);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx send failed: transport=%s cmd=%s ret=0x%X",
            dtu_service_transport_name(transport_id), dtu_service_cmd_name(cmd), ret);
    }
}

/* 发送仅包含状态码的最小响应帧。 */
void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status)
{
    dtu_service_reply(transport_id, cmd, seq, &status, 1);
}
