#include "dtu_service.h"
#include "dtu_service_inner.h"

/* service 门面职责：
 * 1. 管理 transport 注册表与初始化顺序
 * 2. 提供跨模块通用名称和日志工具
 * 3. 提供 telemetry 统计接口
 * 4. 维持“初始化与公共工具”的稳定入口
 */

/* ========================================================================== */
/* Trace 统计区                                                                */
/* ========================================================================== */

#if (DTU_CFG_LOG_TRACE_ENABLE != 0)
static uint32_t g_dtu_rx_batch_max = 0;
static uint32_t g_dtu_rx_batch_last = 0;
static uint32_t g_dtu_rx_total_bytes = 0;
static uint32_t g_dtu_rx_callback_count = 0;
static uint32_t g_dtu_rx_ring_high_watermark = 0;
static uint32_t g_dtu_rx_ring_overflow_count = 0;
static uint32_t g_dtu_rx_task_wakeup_count = 0;
#endif

/* ========================================================================== */
/* Transport 注册表区                                                         */
/* 说明：                                                                     */
/* 1. 当前只默认注册 UART。                                                   */
/* 2. 后续接 BLE/SLE 时，只需要在这里补 transport 接口对象。                 */
/* 3. service 本身不关心具体通道内部实现，只拿到统一的 init/send 接口。      */
/* ========================================================================== */

static const dtu_transport_if_t *g_dtu_transport_table[DTU_TRANSPORT_MAX] = {
    [DTU_TRANSPORT_UART] = &g_dtu_uart_transport,
    [DTU_TRANSPORT_BLE] = &g_dtu_ble_transport,
};

/* 返回 transport 接口对象，未注册时返回空。
 * 这是 service 层访问通道的统一入口，调用方不需要知道 UART/BLE 的具体文件。
 */
const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id)
{
    if (transport_id >= DTU_TRANSPORT_MAX) {
        return NULL;
    }
    return g_dtu_transport_table[transport_id];
}

/* ========================================================================== */
/* 名称映射区                                                                 */
/* 说明：                                                                     */
/* 1. 这些函数只做“枚举/命令值 -> 可读字符串”的映射。                        */
/* 2. 主要给统一日志层和错误日志使用。                                        */
/* 3. 这样日志输出不会在各模块里各写一套字符串常量。                          */
/* ========================================================================== */

/* 统一返回命令名称，供日志和错误信息复用。 */
const char *dtu_service_cmd_name(uint8_t cmd)
{
    switch (cmd) {
        case DTU_CFG_CMD_READ_DEV_INFO:
            return "READ_DEV_INFO";
        case DTU_CFG_CMD_READ_UART_CFG:
            return "READ_UART_CFG";
        case DTU_CFG_CMD_READ_MODBUS_CFG:
            return "READ_MODBUS_CFG";
        case DTU_CFG_CMD_READ_ROOT_WL_ALL:
            return "READ_ROOT_WL_ALL";
        case DTU_CFG_CMD_READ_ROOT_POWER:
            return "READ_ROOT_POWER";
        case DTU_CFG_CMD_GET_MODE_STATUS:
            return "GET_MODE_STATUS";
        case DTU_CFG_CMD_READ_WL_NODE_CFG:
            return "READ_WL_NODE_CFG";
        case DTU_CFG_CMD_SET_ROLE:
            return "SET_ROLE";
        case DTU_CFG_CMD_SET_UART_CFG:
            return "SET_UART_CFG";
        case DTU_CFG_CMD_SET_MODBUS_CFG:
            return "SET_MODBUS_CFG";
        case DTU_CFG_CMD_SET_ROOT_POWER:
            return "SET_ROOT_POWER";
        case DTU_CFG_CMD_ADD_WL_ITEM:
            return "ADD_WL_ITEM";
        case DTU_CFG_CMD_DEL_WL_ITEM:
            return "DEL_WL_ITEM";
        case DTU_CFG_CMD_CLEAR_WL:
            return "CLEAR_WL";
        case DTU_CFG_CMD_SET_WL_NODE_CFG:
            return "SET_WL_NODE_CFG";
        case DTU_CFG_CMD_COMMIT:
            return "COMMIT";
        case DTU_CFG_CMD_REBOOT:
            return "REBOOT";
        case DTU_CFG_CMD_FACTORY_RESET:
            return "FACTORY_RESET";
        default:
            return "UNKNOWN_CMD";
    }
}

/* 统一返回 transport 名称。
 * 如果 transport 已经注册了 name，就直接使用接口对象里的名字；
 * 否则回退到本地默认名称，避免未注册 transport 打日志时变成空字符串。
 */
const char *dtu_service_transport_name(dtu_transport_id_t transport_id)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);

    if (transport_if != NULL && transport_if->name != NULL) {
        return transport_if->name;
    }
    switch (transport_id) {
        case DTU_TRANSPORT_UART:
            return "UART";
        case DTU_TRANSPORT_BLE:
            return "BLE";
        case DTU_TRANSPORT_SLE:
            return "SLE";
        default:
            return "UNKNOWN";
    }
}

/* ========================================================================== */
/* 命令表辅助区                                                               */
/* 说明：                                                                     */
/* 1. config/run 模式都采用表驱动分发。                                       */
/* 2. 这里提供一个通用查表函数，避免每个 mode 文件重复写一遍 for 循环。       */
/* 3. 命中即执行 handler，未命中返回 false。                                  */
/* ========================================================================== */

/* 在命令表中查找命令并执行对应 handler。 */
bool dtu_service_dispatch_table(const dtu_cmd_entry_t *table, uint32_t table_size,
    dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (table == NULL || frame == NULL || table_size == 0) {
        return false;
    }

    for (uint32_t i = 0; i < table_size; i++) {
        if (table[i].cmd_id == frame->cmd) {
            table[i].handler(transport_id, frame);
            return true;
        }
    }
    return false;
}

/* ========================================================================== */
/* Trace 统计入口区                                                           */
/* 说明：                                                                     */
/* 1. trace 默认关闭，只在 Kconfig 打开后生效。                               */
/* 2. 这里不主动打印大量日志，而是先累计指标，避免串口被统计信息刷屏。        */
/* 3. 当前主要观测：接收批量大小、ring 高水位、任务唤醒次数、溢出量。         */
/* ========================================================================== */

/* 在 trace 打开时累计接收批量统计。 */
void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
    unused(length);
    unused(accepted);
    unused(ring_used);
#else
    g_dtu_rx_callback_count++;
    g_dtu_rx_total_bytes += accepted;
    g_dtu_rx_batch_last = accepted;
    if (accepted > g_dtu_rx_batch_max) {
        g_dtu_rx_batch_max = accepted;
    }
    if (ring_used > g_dtu_rx_ring_high_watermark) {
        g_dtu_rx_ring_high_watermark = ring_used;
    }
    if (accepted < length) {
        g_dtu_rx_ring_overflow_count += (uint32_t)(length - accepted);
    }
#endif
}

/* 在 trace 打开时累计解析任务唤醒次数。 */
void dtu_service_trace_rx_task_wakeup(void)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
#else
    g_dtu_rx_task_wakeup_count++;
#endif
}

/* ========================================================================== */
/* 初始化入口区                                                               */
/* 说明：                                                                     */
/* 1. 先加载 storage，让运行配置和模式先稳定下来。                            */
/* 2. 再按注册表初始化各 transport。                                           */
/* 3. 所有 transport 都起来以后，最后再打印 boot 日志快照。                   */
/* 4. 这样日志里看到的配置就是当前真实会生效的配置。                          */
/* ========================================================================== */

/* DTU 服务总初始化入口。 */
errcode_t dtu_service_init(void)
{
    errcode_t load_ret;
    errcode_t ret;

    load_ret = dtu_storage_load();

    for (uint8_t i = 0; i < DTU_TRANSPORT_MAX; i++) {
        const dtu_transport_if_t *transport_if = dtu_service_transport_if((dtu_transport_id_t)i);

        if (transport_if == NULL || transport_if->init == NULL) {
            continue;
        }
        ret = transport_if->init();
        if (ret != ERRCODE_SUCC) {
            dtu_log_error("transport init failed: %s ret=0x%x",
                dtu_service_transport_name((dtu_transport_id_t)i), ret);
            return ret;
        }
    }

    dtu_log_boot(load_ret);
    return ERRCODE_SUCC;
}
