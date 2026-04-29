#include "dtu_service_inner.h"

#include "dma.h"
#include "hal_dma.h"
#include "hal_reboot.h"
#include "pinctrl.h"
#include "soc_osal.h"

/* UART transport 职责：
 * 1. 负责本地串口初始化、DMA 发送、接收缓存与解析任务
 * 2. 自己拥有 ring buffer / semaphore / DMA 配置
 * 3. 只通过 dtu_service_on_bytes() 把原始字节交给公共层
 */

typedef struct {
    uint8_t rx_driver_buffer[DTU_CFG_RX_DRIVER_BUFFER_SIZE];
    uart_buffer_config_t buffer_cfg;
    uart_write_dma_config_t dma_cfg;
    uint8_t rx_ring[DTU_CFG_RING_BUFFER_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    osal_semaphore rx_sem;
} dtu_uart_transport_ctx_t;

static dtu_uart_transport_ctx_t g_dtu_uart_ctx = {
    .buffer_cfg = {
        .rx_buffer = NULL,
        .rx_buffer_size = DTU_CFG_RX_DRIVER_BUFFER_SIZE
    },
    .dma_cfg = {
        .src_width = HAL_DMA_TRANSFER_WIDTH_8,
        .dest_width = HAL_DMA_TRANSFER_WIDTH_8,
        .burst_length = HAL_DMA_BURST_TRANSACTION_LENGTH_1,
        .priority = HAL_DMA_CH_PRIORITY_0
    },
    .rx_head = 0,
    .rx_tail = 0,
    .rx_sem = { 0 }
};

/* 返回 UART transport 私有上下文。 */
static dtu_uart_transport_ctx_t *dtu_uart_ctx(void)
{
    return &g_dtu_uart_ctx;
}

/* 计算 ring buffer 当前已使用字节数。 */
static uint16_t dtu_uart_ring_used(const dtu_uart_transport_ctx_t *ctx)
{
    if (ctx->rx_head >= ctx->rx_tail) {
        return (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    return (uint16_t)(DTU_CFG_RING_BUFFER_SIZE - ctx->rx_tail + ctx->rx_head);
}

/* 向 ring buffer 推入单字节。 */
static bool dtu_uart_ring_push(uint8_t byte)
{
    dtu_uart_transport_ctx_t *ctx = dtu_uart_ctx();
    uint16_t next = (uint16_t)((ctx->rx_head + 1) % DTU_CFG_RING_BUFFER_SIZE);

    if (next == ctx->rx_tail) {
        return false;
    }
    ctx->rx_ring[ctx->rx_head] = byte;
    ctx->rx_head = next;
    return true;
}

/* 从 ring buffer 弹出单字节。 */
static bool dtu_uart_ring_pop(uint8_t *byte)
{
    dtu_uart_transport_ctx_t *ctx = dtu_uart_ctx();

    if (ctx->rx_tail == ctx->rx_head) {
        return false;
    }
    *byte = ctx->rx_ring[ctx->rx_tail];
    ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) % DTU_CFG_RING_BUFFER_SIZE);
    return true;
}

/* UART RX callback 只做数据搬运和任务唤醒。 */
static void dtu_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    dtu_uart_transport_ctx_t *ctx = dtu_uart_ctx();
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t accepted = 0;

    unused(error);
    if (data == NULL || length == 0) {
        return;
    }

    for (uint16_t i = 0; i < length; i++) {
        if (!dtu_uart_ring_push(data[i])) {
            /* ring 满后停止接纳本批剩余字节，靠统计观测丢包规模。 */
            break;
        }
        accepted++;
    }

    dtu_service_trace_rx_batch(length, accepted, dtu_uart_ring_used(ctx));
    osal_sem_up(&ctx->rx_sem);
}

/* UART 解析任务，从 ring buffer 批量取数据并喂给公共层。 */
static void *dtu_uart_task(const char *arg)
{
    uint8_t batch[64];

    unused(arg);
    while (1) {
        uint16_t count = 0;

        while (count < sizeof(batch) && dtu_uart_ring_pop(&batch[count])) {
            count++;
        }
        if (count > 0) {
            dtu_service_on_bytes(DTU_TRANSPORT_UART, batch, count);
            continue;
        }

        if (dtu_storage_is_reboot_pending()) {
            osal_msleep(20);
            hal_reboot_chip();
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&dtu_uart_ctx()->rx_sem) != OSAL_SUCCESS) {
            osal_msleep(1);
        }
    }

    return NULL;
}

/* 初始化 UART transport。 */
static errcode_t dtu_uart_transport_init_impl(void)
{
    dtu_uart_transport_ctx_t *ctx = dtu_uart_ctx();
    uart_attr_t uart_attr;
    uart_extra_attr_t extra_attr = {
        .tx_dma_enable = true,
        .tx_int_threshold = UART_FIFO_INT_TX_LEVEL_EQ_0_CHARACTER,
        .rx_dma_enable = false,
        .rx_int_threshold = UART_FIFO_INT_RX_LEVEL_1_CHARACTER
    };
    uart_pin_config_t uart_pins = {
        .tx_pin = DTU_CFG_UART_TX_PIN,
        .rx_pin = DTU_CFG_UART_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };
    osal_task *task;
    errcode_t ret;

    ctx->buffer_cfg.rx_buffer = ctx->rx_driver_buffer;
    dtu_storage_fill_uart_attr(&uart_attr, &dtu_storage_runtime_const()->uart_cfg);
    extra_attr.rx_int_threshold = dtu_storage_rx_int_threshold();

    uapi_dma_init();
    uapi_dma_open();
    uapi_pin_set_mode(DTU_CFG_UART_TX_PIN, DTU_CFG_UART_PIN_MODE);
    uapi_pin_set_mode(DTU_CFG_UART_RX_PIN, DTU_CFG_UART_PIN_MODE);
    uapi_uart_deinit(DTU_CFG_UART_BUS);

    ret = osal_sem_binary_sem_init(&ctx->rx_sem, 0);
    if (ret != OSAL_SUCCESS) {
        dtu_log_error("uart sem init failed: 0x%x", ret);
        return ERRCODE_FAIL;
    }

    ret = uapi_uart_init(DTU_CFG_UART_BUS, &uart_pins, &uart_attr, &extra_attr, &ctx->buffer_cfg);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("uart init failed: 0x%x", ret);
        return ret;
    }

    ret = uapi_uart_register_rx_callback(DTU_CFG_UART_BUS, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE,
        1, dtu_uart_rx_callback);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("uart callback register failed: 0x%x", ret);
        return ret;
    }

    task = osal_kthread_create((osal_kthread_handler)dtu_uart_task, NULL, "DtuUartTask", 0x1200);
    if (task == NULL) {
        return ERRCODE_FAIL;
    }
    osal_kthread_set_priority(task, 25);

    return ERRCODE_SUCC;
}

/* 通过 UART DMA 发送完整协议帧。 */
static errcode_t dtu_uart_transport_send_impl(const uint8_t *data, uint16_t len)
{
    int32_t ret;

    if (data == NULL || len == 0) {
        return ERRCODE_FAIL;
    }

    ret = uapi_uart_write_by_dma(DTU_CFG_UART_BUS, data, len, &dtu_uart_ctx()->dma_cfg);
#if defined(CONFIG_UART_USING_V151)
    return (ret == ERRCODE_SUCC) ? ERRCODE_SUCC : (errcode_t)ret;
#else
    return (ret == (int32_t)len) ? ERRCODE_SUCC : (errcode_t)ret;
#endif
}

const dtu_transport_if_t g_dtu_uart_transport = {
    .name = "UART",
    .init = dtu_uart_transport_init_impl,
    .send = dtu_uart_transport_send_impl
};
