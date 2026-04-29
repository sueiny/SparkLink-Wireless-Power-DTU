#include "dtu_service_inner.h"

#include "osal_mutex.h"
#include "securec.h"

/* 配置模式职责：
 * 1. 只处理配置模式允许的命令
 * 2. 命令分发表驱动，不在入口保留超长 switch
 * 3. 只通过 service 回复，不直接处理 transport 或协议组包
 */

/* ==================== 白名单辅助函数区 ==================== */
/* 说明：
 * 1. 白名单读取需要支持分片返回。
 * 2. 这些辅助函数只服务于 ROOT 白名单读写，不对外暴露。
 */
typedef struct {
    uint8_t start;
    uint8_t end;
} dtu_wl_frag_range_t;

typedef struct {
    uint8_t total;
    dtu_wl_frag_range_t ranges[DTU_CFG_MAX_WL_ITEMS];
} dtu_wl_frag_plan_t;

/* 白名单分片计划和响应 body 放在静态区，避免 128 条白名单时压爆业务任务栈。 */
static dtu_wl_frag_plan_t g_wl_frag_plan;
static uint8_t g_wl_frag_body[DTU_CFG_WL_FRAGMENT_BODY_MAX];

/* 白名单分片缓存只有一份，因此用 mutex 串行保护。
 * 这样既能避免 UART/BLE 同时读取时互相覆盖，又不需要按 transport 额外复制缓存。
 */
static osal_mutex g_wl_frag_mutex;
static bool g_wl_frag_mutex_ready;

static void dtu_mode_cfg_build_wl_frag_plan(dtu_wl_frag_plan_t *plan);
static uint8_t dtu_mode_cfg_pack_wl_fragment(const dtu_wl_frag_plan_t *plan, uint8_t frag_idx, uint8_t *body);
static bool dtu_mode_cfg_lock_wl_frag_cache(void);
static void dtu_mode_cfg_unlock_wl_frag_cache(void);
static bool dtu_mode_cfg_expect_empty_body(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 读取命令处理区 ==================== */
/* 说明：
 * 1. 这一组 handler 只负责“从 runtime 读取当前配置并回包”。
 * 2. 不修改运行状态。
 * 3. 读取成功时统一走 dtu_log_cfg_read_* 日志。
 */
static void dtu_mode_cfg_handle_read_dev_info(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_read_uart_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_read_modbus_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_read_root_wl_all(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_read_root_power(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_read_wl_node_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 写入命令处理区 ==================== */
/* 说明：
 * 1. 这一组 handler 只修改 RAM 中的 runtime 配置。
 * 2. 真正持久化由 COMMIT 负责。
 * 3. 其中 UART 参数仍然保持“重启后生效”的语义。
 */
static void dtu_mode_cfg_handle_set_role(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_set_uart_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_set_modbus_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_set_root_power(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_add_wl_item(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_del_wl_item(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_clear_wl(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_set_wl_node_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 控制命令处理区 ==================== */
/* 说明：
 * 1. 控制命令会触发持久化或恢复默认。
 * 2. 因为影响面更大，所以成功后会打印完整配置快照。
 */
static void dtu_mode_cfg_handle_commit(dtu_transport_id_t transport_id, const dtu_frame_t *frame);
static void dtu_mode_cfg_handle_factory_reset(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/* ==================== 命令注册表区 ==================== */
/* 说明：
 * 1. 配置模式下所有允许命令统一注册在这里。
 * 2. 新增命令时优先写独立 handler，再往表里加一项。
 * 3. 这样可以避免 dispatch 函数重新长成一个巨大 switch。
 */
static const dtu_cmd_entry_t g_dtu_config_cmd_table[] = {
    { DTU_CFG_CMD_READ_DEV_INFO, dtu_mode_cfg_handle_read_dev_info },
    { DTU_CFG_CMD_READ_UART_CFG, dtu_mode_cfg_handle_read_uart_cfg },
    { DTU_CFG_CMD_READ_MODBUS_CFG, dtu_mode_cfg_handle_read_modbus_cfg },
    { DTU_CFG_CMD_READ_ROOT_WL_ALL, dtu_mode_cfg_handle_read_root_wl_all },
    { DTU_CFG_CMD_READ_ROOT_POWER, dtu_mode_cfg_handle_read_root_power },
    { DTU_CFG_CMD_READ_WL_NODE_CFG, dtu_mode_cfg_handle_read_wl_node_cfg },
    { DTU_CFG_CMD_SET_ROLE, dtu_mode_cfg_handle_set_role },
    { DTU_CFG_CMD_SET_UART_CFG, dtu_mode_cfg_handle_set_uart_cfg },
    { DTU_CFG_CMD_SET_MODBUS_CFG, dtu_mode_cfg_handle_set_modbus_cfg },
    { DTU_CFG_CMD_SET_ROOT_POWER, dtu_mode_cfg_handle_set_root_power },
    { DTU_CFG_CMD_ADD_WL_ITEM, dtu_mode_cfg_handle_add_wl_item },
    { DTU_CFG_CMD_DEL_WL_ITEM, dtu_mode_cfg_handle_del_wl_item },
    { DTU_CFG_CMD_CLEAR_WL, dtu_mode_cfg_handle_clear_wl },
    { DTU_CFG_CMD_SET_WL_NODE_CFG, dtu_mode_cfg_handle_set_wl_node_cfg },
    { DTU_CFG_CMD_COMMIT, dtu_mode_cfg_handle_commit },
    { DTU_CFG_CMD_FACTORY_RESET, dtu_mode_cfg_handle_factory_reset }
};

/* 校验无 body 命令的长度。
 * 大量读取型命令都是“无 body 请求”，统一收在这里可以减少重复判断代码。
 */
static bool dtu_mode_cfg_expect_empty_body(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame->len == 0) {
        return true;
    }
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
    return false;
}

/* 读取设备信息，包括角色、MAC 和派生设备名。 */
static void dtu_mode_cfg_handle_read_dev_info(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t body[1 + 1 + WIFI_MAC_LEN + 1 + DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t mac[WIFI_MAC_LEN] = {0};
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len;
    uint16_t off = 0;

    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }

    dtu_storage_get_device_mac(mac);
    name_len = dtu_storage_get_device_name(name, sizeof(name));
    dtu_log_cfg_read_dev_info();

    body[off++] = DTU_CFG_STATUS_SUCC;
    body[off++] = cfg->role;
    if (memcpy_s(&body[off], sizeof(body) - off, mac, WIFI_MAC_LEN) != EOK) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
        return;
    }
    off = (uint16_t)(off + WIFI_MAC_LEN);
    body[off++] = name_len;
    if (name_len > 0) {
        if (memcpy_s(&body[off], sizeof(body) - off, name, name_len) != EOK) {
            dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
            return;
        }
        off = (uint16_t)(off + name_len);
    }
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, off);
}

/* 读取当前串口配置快照。 */
static void dtu_mode_cfg_handle_read_uart_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t body[5];

    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }

    dtu_log_cfg_read_uart(&cfg->uart_cfg);
    body[0] = DTU_CFG_STATUS_SUCC;
    body[1] = cfg->uart_cfg.baud_level;
    body[2] = cfg->uart_cfg.parity;
    body[3] = cfg->uart_cfg.stop_bits;
    body[4] = cfg->uart_cfg.data_bits;
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, sizeof(body));
}

/* 读取 Modbus 配置表。 */
static void dtu_mode_cfg_handle_read_modbus_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t body[2 + DTU_CFG_MAX_MODBUS_ITEMS * 2] = {0};
    uint16_t off = 0;

    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }

    dtu_log_cfg_read_modbus();
    body[off++] = DTU_CFG_STATUS_SUCC;
    body[off++] = cfg->modbus_count;
    for (uint8_t i = 0; i < cfg->modbus_count; i++) {
        body[off++] = cfg->modbus[i].addr;
        body[off++] = cfg->modbus[i].dev_type;
    }
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, off);
}

/* 生成白名单分片计划。
 * 计划只保存每个分片覆盖的 item 范围 [start, end)，生成后打包阶段不再重新扫描白名单。
 */
static void dtu_mode_cfg_build_wl_frag_plan(dtu_wl_frag_plan_t *plan)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint16_t used = 5;
    uint8_t frag = 0;

    if (plan == NULL) {
        return;
    }

    (void)memset_s(plan, sizeof(*plan), 0, sizeof(*plan));
    plan->total = 1;
    plan->ranges[0].start = 0;
    plan->ranges[0].end = 0;

    if (cfg->wl_count == 0) {
        return;
    }

    for (uint8_t i = 0; i < cfg->wl_count; i++) {
        if (used + WIFI_MAC_LEN > DTU_CFG_WL_FRAGMENT_BODY_MAX) {
            plan->ranges[frag].end = i;
            frag++;
            plan->total = (uint8_t)(frag + 1);
            plan->ranges[frag].start = i;
            plan->ranges[frag].end = i;
            used = 5;
        }
        used = (uint16_t)(used + WIFI_MAC_LEN);
    }

    plan->ranges[frag].end = cfg->wl_count;
}

/* 打包指定分片的白名单内容。 */
static uint8_t dtu_mode_cfg_pack_wl_fragment(const dtu_wl_frag_plan_t *plan, uint8_t frag_idx, uint8_t *body)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    const dtu_wl_frag_range_t *range;
    uint16_t used = 5;

    if (plan == NULL || body == NULL || frag_idx == 0 || frag_idx > plan->total) {
        return 0;
    }

    range = &plan->ranges[frag_idx - 1];

    body[0] = DTU_CFG_STATUS_SUCC;
    body[1] = frag_idx;
    body[2] = plan->total;
    body[3] = cfg->wl_count;
    body[4] = (uint8_t)(range->end - range->start);

    for (uint8_t i = range->start; i < range->end; i++) {
        if (memcpy_s(&body[used], DTU_CFG_WL_FRAGMENT_BODY_MAX - used, cfg->whitelist[i].mac, WIFI_MAC_LEN) != EOK) {
            return 0;
        }
        used = (uint16_t)(used + WIFI_MAC_LEN);
    }
    return (uint8_t)used;
}

/* 获取白名单分片缓存锁。 */
static bool dtu_mode_cfg_lock_wl_frag_cache(void)
{
    if (!g_wl_frag_mutex_ready) {
        if (osal_mutex_init(&g_wl_frag_mutex) != 0) {
            dtu_log_error("wl frag mutex init failed");
            return false;
        }
        g_wl_frag_mutex_ready = true;
    }

    if (osal_mutex_lock(&g_wl_frag_mutex) != 0) {
        dtu_log_error("wl frag mutex lock failed");
        return false;
    }
    return true;
}

/* 释放白名单分片缓存锁。 */
static void dtu_mode_cfg_unlock_wl_frag_cache(void)
{
    if (g_wl_frag_mutex_ready) {
        (void)osal_mutex_unlock(&g_wl_frag_mutex);
    }
}

/* 读取 ROOT 白名单。 */
static void dtu_mode_cfg_handle_read_root_wl_all(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }
    if (transport_id >= DTU_TRANSPORT_MAX) {
        return;
    }

    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }

    dtu_log_cfg_read_whitelist();
    if (!dtu_mode_cfg_lock_wl_frag_cache()) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_BUSY);
        return;
    }

    dtu_mode_cfg_build_wl_frag_plan(&g_wl_frag_plan);
    for (uint8_t frag = 1; frag <= g_wl_frag_plan.total; frag++) {
        uint8_t body_len = dtu_mode_cfg_pack_wl_fragment(&g_wl_frag_plan, frag, g_wl_frag_body);
        dtu_service_reply(transport_id, frame->cmd, frame->seq, g_wl_frag_body, body_len);
    }
    dtu_mode_cfg_unlock_wl_frag_cache();
}

/* 读取 ROOT 功率配置。 */
static void dtu_mode_cfg_handle_read_root_power(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    uint8_t body[2];

    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }

    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }

    body[0] = DTU_CFG_STATUS_SUCC;
    body[1] = dtu_storage_runtime_const()->power;
    dtu_log_cfg_read_power(dtu_storage_runtime_const()->power);
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, sizeof(body));
}

/* 读取指定白名单 node 的子配置。 */
static void dtu_mode_cfg_handle_read_wl_node_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t body[1 + 4 + 1 + DTU_CFG_MAX_MODBUS_ITEMS * 2] = {0};
    uint16_t off = 0;
    int32_t idx;

    if (cfg->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (frame->len != WIFI_MAC_LEN) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    idx = dtu_storage_find_wl_item(frame->body);
    if (idx < 0) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_NOT_FOUND);
        return;
    }

    dtu_log_cfg_read_wl_node(&cfg->whitelist[idx]);
    body[off++] = DTU_CFG_STATUS_SUCC;
    body[off++] = cfg->whitelist[idx].uart_cfg.baud_level;
    body[off++] = cfg->whitelist[idx].uart_cfg.parity;
    body[off++] = cfg->whitelist[idx].uart_cfg.stop_bits;
    body[off++] = cfg->whitelist[idx].uart_cfg.data_bits;
    body[off++] = cfg->whitelist[idx].modbus_count;
    for (uint8_t i = 0; i < cfg->whitelist[idx].modbus_count; i++) {
        body[off++] = cfg->whitelist[idx].modbus[i].addr;
        body[off++] = cfg->whitelist[idx].modbus[i].dev_type;
    }
    dtu_service_reply(transport_id, frame->cmd, frame->seq, body, off);
}

/* 设置设备角色。 */
static void dtu_mode_cfg_handle_set_role(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame->len != 1 || !dtu_storage_is_valid_role(frame->body[0])) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
        return;
    }

    dtu_storage_runtime()->role = frame->body[0];
    dtu_log_cfg_write_role(dtu_storage_runtime_const()->role);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 设置串口参数，重启后生效。 */
static void dtu_mode_cfg_handle_set_uart_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    dtu_uart_cfg_t cfg;

    if (frame->len != 4) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    cfg.baud_level = frame->body[0];
    cfg.parity = frame->body[1];
    cfg.stop_bits = frame->body[2];
    cfg.data_bits = frame->body[3];
    if (!dtu_storage_is_valid_uart_cfg(&cfg)) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
        return;
    }

    dtu_storage_runtime()->uart_cfg = cfg;
    dtu_log_cfg_write_uart(&dtu_storage_runtime_const()->uart_cfg);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 替换 Modbus 设备表。 */
static void dtu_mode_cfg_handle_set_modbus_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    uint8_t item_count;

    if (frame->len < 1) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    item_count = frame->body[0];
    if (item_count > DTU_CFG_MAX_MODBUS_ITEMS || frame->len != (uint16_t)(1 + item_count * 2)) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
        return;
    }

    for (uint8_t i = 0; i < item_count; i++) {
        uint8_t addr = frame->body[1 + i * 2];
        uint8_t dev_type = frame->body[2 + i * 2];

        if (addr == 0 || !dtu_storage_is_valid_dev_type(dev_type)) {
            dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
            return;
        }
        dtu_storage_runtime()->modbus[i].addr = addr;
        dtu_storage_runtime()->modbus[i].dev_type = dev_type;
    }

    dtu_storage_runtime()->modbus_count = item_count;
    dtu_log_cfg_write_modbus();
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 设置 ROOT 功率。 */
static void dtu_mode_cfg_handle_set_root_power(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (frame->len != 1) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    dtu_storage_runtime()->power = frame->body[0];
    dtu_log_cfg_write_power(dtu_storage_runtime_const()->power);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 新增或覆盖白名单项。 */
static void dtu_mode_cfg_handle_add_wl_item(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    dtu_runtime_cfg_t *runtime = dtu_storage_runtime();
    int32_t exist_idx;
    bool new_item = false;

    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (frame->len != WIFI_MAC_LEN) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    exist_idx = dtu_storage_find_wl_item(frame->body);
    if (exist_idx < 0) {
        if (dtu_storage_runtime_const()->wl_count >= DTU_CFG_MAX_WL_ITEMS) {
            dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_WL_FULL);
            return;
        }
        exist_idx = runtime->wl_count;
        new_item = true;
    }

    if (memcpy_s(runtime->whitelist[exist_idx].mac, WIFI_MAC_LEN, frame->body, WIFI_MAC_LEN) != EOK) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
        return;
    }
    if (new_item) {
        dtu_storage_init_wl_item_cfg(&runtime->whitelist[exist_idx]);
        runtime->wl_count++;
    }
    dtu_log_cfg_write_whitelist();
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 删除指定白名单项。 */
static void dtu_mode_cfg_handle_del_wl_item(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    int32_t idx;

    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (frame->len != WIFI_MAC_LEN) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    idx = dtu_storage_find_wl_item(frame->body);
    if (idx < 0) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_NOT_FOUND);
        return;
    }

    for (uint8_t i = (uint8_t)idx; i + 1 < dtu_storage_runtime_const()->wl_count; i++) {
        if (memcpy_s(&dtu_storage_runtime()->whitelist[i], sizeof(dtu_storage_runtime()->whitelist[i]),
            &dtu_storage_runtime()->whitelist[i + 1], sizeof(dtu_storage_runtime()->whitelist[i + 1])) != EOK) {
            dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
            return;
        }
    }
    dtu_storage_runtime()->wl_count--;
    dtu_log_cfg_write_whitelist();
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 清空 ROOT 白名单。 */
static void dtu_mode_cfg_handle_clear_wl(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }

    dtu_storage_runtime()->wl_count = 0;
    dtu_log_cfg_write_whitelist();
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 设置指定白名单 node 的子配置。 */
static void dtu_mode_cfg_handle_set_wl_node_cfg(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    dtu_uart_cfg_t uart_cfg;
    uint8_t modbus_count;
    int32_t idx;

    if (dtu_storage_runtime_const()->role != DTU_CFG_ROLE_ROOT) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_ROLE_MISMATCH);
        return;
    }
    if (frame->len < (uint16_t)(WIFI_MAC_LEN + 5)) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }

    idx = dtu_storage_find_wl_item(frame->body);
    if (idx < 0) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_NOT_FOUND);
        return;
    }

    uart_cfg.baud_level = frame->body[WIFI_MAC_LEN + 0];
    uart_cfg.parity = frame->body[WIFI_MAC_LEN + 1];
    uart_cfg.stop_bits = frame->body[WIFI_MAC_LEN + 2];
    uart_cfg.data_bits = frame->body[WIFI_MAC_LEN + 3];
    modbus_count = frame->body[WIFI_MAC_LEN + 4];

    if (!dtu_storage_is_valid_uart_cfg(&uart_cfg)) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
        return;
    }
    if (modbus_count > DTU_CFG_MAX_MODBUS_ITEMS ||
        frame->len != (uint16_t)(WIFI_MAC_LEN + 5 + modbus_count * 2)) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
        return;
    }

    dtu_storage_runtime()->whitelist[idx].uart_cfg = uart_cfg;
    for (uint8_t i = 0; i < modbus_count; i++) {
        uint8_t addr = frame->body[WIFI_MAC_LEN + 5 + i * 2];
        uint8_t dev_type = frame->body[WIFI_MAC_LEN + 6 + i * 2];

        if (!dtu_storage_is_valid_dev_type(dev_type)) {
            dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_PARAM_ERR);
            return;
        }
        dtu_storage_runtime()->whitelist[idx].modbus[i].addr = addr;
        dtu_storage_runtime()->whitelist[idx].modbus[i].dev_type = dev_type;
    }
    dtu_storage_runtime()->whitelist[idx].modbus_count = modbus_count;
    dtu_log_cfg_write_wl_node(&dtu_storage_runtime_const()->whitelist[idx]);
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
}

/* 持久化当前配置。 */
static void dtu_mode_cfg_handle_commit(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (!dtu_mode_cfg_expect_empty_body(transport_id, frame)) {
        return;
    }
    if (dtu_storage_commit() != ERRCODE_SUCC) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
        return;
    }
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
    dtu_log_commit();
}

/* 恢复出厂配置。 */
static void dtu_mode_cfg_handle_factory_reset(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame->len != 0 && frame->len != 1) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_LEN_ERR);
        return;
    }
    if (dtu_storage_factory_reset() != ERRCODE_SUCC) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SAVE_FAIL);
        return;
    }
    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_SUCC);
    dtu_log_factory_reset();
}

/* 配置模式命令表分发入口。 */
void dtu_mode_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (dtu_service_dispatch_table(g_dtu_config_cmd_table,
        (uint32_t)(sizeof(g_dtu_config_cmd_table) / sizeof(g_dtu_config_cmd_table[0])),
        transport_id, frame)) {
        return;
    }

    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_CMD_ERR);
}

const dtu_mode_if_t g_dtu_mode_config_if = {
    .name = "CONFIG",
    .dispatch = dtu_mode_config_dispatch
};
