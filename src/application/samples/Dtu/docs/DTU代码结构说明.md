# DTU 代码结构说明

当前这版已经收敛成"顶层少量核心文件"的结构，支持三种 transport 通道：`UART`、`BLE`、`SLE`。  
新增通道时只需"新增通道文件"，而不是改协议和保存逻辑。

---

## 1. 文件分层

| 路径 | 角色 | 主要职责 |
| --- | --- | --- |
| `dtu_main.c` | 启动层 | 拉起 `dtu_service_init()` |
| `service/dtu_service.h` | 对外头 | 只暴露 `dtu_service_init()` |
| `service/dtu_service_inner.h` | 内部总入口 | 常量、类型、跨模块接口声明 |
| `service/dtu_service.c` | Service 门面 | transport 注册与初始化、名称/日志工具、统计入口 |
| `service/flow/dtu_flow.c` | 主链路 | 输入解析、公共命令分发、模式路由、响应发送 |
| `service/dtu_log.c` | 统一日志 | 启动、提交、读写配置、reject、错误日志 |
| `service/protocol/dtu_protocol.c/.h` | 协议装配器 | 单字节状态机、CRC、响应打包 |
| `service/storage/dtu_storage.c/.h` | 存储中心 | 默认值、NV 读写、模式状态、运行配置 |
| `service/mode/dtu_mode_config.c` | 配置模式 | 读写配置、提交、恢复出厂 |
| `service/mode/dtu_mode_run.c` | 运行模式 | 运行态拒配边界 |
| `transport/dtu_channel_uart.c` | UART 通道 | UART 初始化、RX ring、DMA 发送、UART 任务 |
| `transport/dtu_channel_ble.c` | BLE 通道 | BLE GATT server、RX ring、notify 发送、BLE 任务 |
| `transport/dtu_channel_sle.c` | SLE 通道 | 星闪 SLE server、RX ring、notify 发送、SLE 任务 |
| `test_tools/dtu_ble_full_test.py` | BLE 测试工具 | BLE 全流程自动化测试脚本 |
| `test_tools/dtu_ble_client.py` | BLE 客户端 | BLE 连接与数据收发客户端工具 |
| `common/dtu_build_config.h` | 常量配置 | 引脚、命令字、状态码、资源限制 |
| `common/dtu_types.h` | 共享类型 | mode、transport、runtime cfg 等共享结构 |

---

## 2. 现在该怎么看代码

如果你要理解"收到一包数据后系统怎么走"，按这条主线看最清楚：

```text
transport/dtu_channel_uart.c
    -> dtu_service_on_bytes()
    -> service/protocol/dtu_protocol.c
    -> dtu_service_on_frame()
    -> service/mode/dtu_mode_config.c 或 service/mode/dtu_mode_run.c
    -> dtu_service_reply()
    -> channel send()
```

如果你要理解"配置为什么能保存、模式为什么能重启后生效"，按这条线看：

```text
mode handler
    -> dtu_storage_runtime()
    -> dtu_storage_commit()
    -> NV_ID_DTU_CFG
    -> reboot 后 dtu_storage_load()
```

---

## 3. 核心入口参照表

| 函数 | 文件 | 作用 |
| --- | --- | --- |
| `dtu_service_init()` | `service/dtu_service.c` | DTU 总初始化入口 |
| `dtu_service_on_bytes()` | `service/flow/dtu_flow.c` | 任意通道提交原始字节流 |
| `dtu_service_on_frame()` | `service/flow/dtu_flow.c` | 任意通道提交完整协议帧 |
| `dtu_service_reply()` | `service/flow/dtu_flow.c` | 公共回复入口 |
| `dtu_protocol_feed_byte()` | `service/protocol/dtu_protocol.c` | 单字节推进协议状态机 |
| `dtu_protocol_pack_response()` | `service/protocol/dtu_protocol.c` | 打包完整响应帧 |
| `dtu_storage_load()` | `service/storage/dtu_storage.c` | 从 NV 加载配置，并按 GPIO13 拨码决定当前模式 |
| `dtu_storage_commit()` | `service/storage/dtu_storage.c` | 将当前配置写入 NV |
| `dtu_storage_factory_reset()` | `service/storage/dtu_storage.c` | 恢复默认值并写入 NV |
| `dtu_mode_config_dispatch()` | `service/mode/dtu_mode_config.c` | 配置模式命令表分发 |
| `dtu_mode_run_dispatch()` | `service/mode/dtu_mode_run.c` | 运行模式命令表分发 |
| `g_dtu_uart_transport` | `transport/dtu_channel_uart.c` | UART 通道接口对象 |
| `g_dtu_ble_transport` | `transport/dtu_channel_ble.c` | BLE 通道接口对象 |
| `g_dtu_sle_transport` | `transport/dtu_channel_sle.c` | SLE 通道接口对象 |

---

## 4. 三个核心模块怎么分工

### `dtu_protocol.c`

这里只做纯数据运算：
- `SOF = AA 55`
- `CMD/SEQ/LEN/BODY/CRC` 解析
- `CRC16(Modbus)` 校验
- 响应帧打包

它不应该知道：
- 当前是配置模式还是运行模式
- 配置要不要保存
- 数据来自 UART 还是 BLE

### `dtu_storage.c`

这里是唯一"真状态拥有者"：
- `dtu_runtime_cfg_t`
- `current_mode`
- `mode_source`
- `reboot_pending`

其他层只能通过接口访问它，不能自己维护一份"影子状态"。

### `service/dtu_service.c` + `service/flow/dtu_flow.c`

service 现在拆成两个主子模块协作：
- `dtu_service.c`：初始化 storage、初始化已启用 transport、提供公共名称工具
- `flow/dtu_flow.c`：处理输入解析、公共命令、模式路由和统一回复

日志统一收在：
- `dtu_log.c`：只保留启动、提交、读写配置、reject、错误几类日志

分发层里的公共命令：
  - `GET_MODE_STATUS`
  - `REBOOT`

其余命令按当前模式路由到 mode 层，回复统一走 `dtu_service_reply()`。

---

## 5. transport 接口设计

现在发包不再靠枚举分支判断：

```c
if (transport == UART) { ... }
```

而是统一走接口表：

```c
const dtu_transport_if_t *transport_if = transport_table[id];
transport_if->send(data, len);
```

当前已注册三种 transport：
- `g_dtu_uart_transport`：UART 通道
- `g_dtu_ble_transport`：BLE 通道
- `g_dtu_sle_transport`：SLE 通道

后面加新通道时：
- 新建一个 `dtu_channel_xxx.c`
- 实现自己的 `init/send`
- 在 `dtu_service.c` 的 `g_dtu_transport_table` 中注册

就够了，不需要去全局搜所有 `switch(transport)` 再一处处补。

---

## 6. mode 为什么现在更好扩命令

配置模式和运行模式都已经改成"命令表分发"，不是一个越来越大的总 `switch`。

### 配置模式

在 `dtu_mode_config.c` 里，每条命令是独立小函数，然后注册到：

```c
static const dtu_cmd_entry_t g_dtu_config_cmd_table[]
```

### 运行模式

在 `dtu_mode_run.c` 里，用：

```c
static const dtu_cmd_entry_t g_dtu_run_cmd_table[]
```

控制哪些命令在运行态被拒绝。

所以以后你加命令时，优先做的是：
1. 写一个小 handler
2. 在对应命令表里加一行

而不是往一个巨型分发函数里继续堆 `case`。

---

## 7. 模式与配置生效规则

| 项 | 运行时修改 | `COMMIT` 后 | `REBOOT` 后 |
| --- | --- | --- | --- |
| `role` | 改内存 | 写入 NV | 重启后恢复 |
| `uart_cfg` | 改内存 | 写入 NV | 按新串口参数初始化 |
| `current_mode` | 由 GPIO13 拨码决定 | 不作为生效依据 | 重启时重新读取拨码 |
| `modbus / power / whitelist` | 改内存 | 写入 NV | 重启后恢复 |

也就是说：
- `SET_UART_CFG` 不立即重配当前串口
- 模式不再通过协议切换，而是由 `GPIO13` 上拉输入决定：
  - 高电平：配置模式
  - 低电平：工作模式
- 模式在启动时采样，当前这版以重启后重新读取拨码为准

---

## 8. BLE / SLE 通道接入说明

当前版本已实现三种 transport 通道：
- **UART**：本地串口，配置模式和运行模式均可用
- **BLE**：低功耗蓝牙，配置模式下提供无线配置接入
- **SLE**：星闪（Sparkling Low Energy），运行模式下提供组网通信

接入新通道时，不要改：
- `dtu_protocol.c`
- `dtu_storage.c`
- 大部分 `mode` 处理函数

正确做法是：
1. 新建 `dtu_channel_xxx.c`
2. 自己维护该通道的接收缓存、连接句柄、发送接口
3. 收到原始字节后统一喂：
   - `dtu_service_on_bytes()`
   或
   - 如果上层已拿到完整帧，就喂 `dtu_service_on_frame()`
4. 回复仍由 `dtu_service_reply()` 统一下发到该 transport 的 `send()`

这就是现在这版"高内聚、低耦合"的关键价值：
- 协议不跟 transport 绑死
- 保存不跟 UART 绑死
- mode 不跟 BLE/UART 绑死

---

## 9. 现在最推荐的阅读顺序

### 想看整体框架
1. `dtu_service_inner.h`
2. `dtu_service.c`
3. `service/storage/dtu_storage.c`
4. `service/protocol/dtu_protocol.c`

### 想看配置协议
1. `service/mode/dtu_mode_config.c`
2. `service/mode/dtu_mode_run.c`

### 想看 UART 怎么接进来
1. `dtu_channel_uart.c`

### 想看 BLE 怎么接进来
1. `dtu_channel_ble.c`

### 想看 SLE 怎么接进来
1. `dtu_channel_sle.c`

### 想接新通道
1. `dtu_channel_uart.c`（参考基本结构）
2. 对照它做新的 `transport` 文件
3. 最后在 `dtu_service.c` 的 `g_dtu_transport_table` 中注册
