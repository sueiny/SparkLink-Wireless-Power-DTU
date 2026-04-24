用途：说明配置协议中每个字节、字段、枚举值和数据体代表的含义

# 1. 文档说明

| **项目** | **说明**                                                           |
|----------|--------------------------------------------------------------------|
| 整理目标 | 把 DTU 配置协议整理成“字段说明表”，重点说明每个数据/字节代表什么。 |
| 适用范围 | UART 配置通道、BLE 配置通道共用的 DTU 配置协议。                   |
| 资料依据 | DTU配置协议.md、DTU测试流程（串口）.md、DTU完整测试流程.md。       |
| 重要约定 | 表内 Hex 报文按线上实际发送顺序展示；SOF 显示为 AA 55。            |
| 注意     | SEQ 是请求序号；如果 SEQ 改变，CRC 必须重新计算。                  |

# 2. 固定帧格式

| **字段** | **长度/字节数** | **线上顺序/示例** | **含义**     | **说明**                                                                   |
|----------|-----------------|-------------------|--------------|----------------------------------------------------------------------------|
| SOF      | 2               | AA 55             | 固定帧头     | 协议文档中可写作 0x55AA；实际发送字节顺序为 AA 55。                        |
| CMD      | 1               | 01 / 10 / 20 ...  | 命令字       | 区分读取、写入、控制等不同指令。响应命令字 = 请求 CMD \| 0x80。            |
| SEQ      | 1               | 01 / 02 / 0D ...  | 请求序号     | 前端每发一条命令可递增；设备响应时原样带回，用于请求/响应配对。            |
| LEN      | 2               | LEN_L LEN_H       | BODY 长度    | 小端格式，只表示 BODY 字节数，不包含 SOF/CMD/SEQ/LEN/CRC。                 |
| BODY     | N               | 按命令变化        | 命令数据体   | 读取命令通常为空；写入命令携带配置数据；响应 BODY 第 1 字节固定为 status。 |
| CRC16    | 2               | CRC_L CRC_H       | CRC16-Modbus | 覆盖 CMD + SEQ + LEN + BODY，不覆盖 SOF。                                  |

# 3. 解析规则

| **顺序** | **解析动作**             | **关键点**                            |
|----------|--------------------------|---------------------------------------|
| 1        | 查找固定帧头 SOF = AA 55 | 串口流中先同步到帧头。                |
| 2        | 读取 CMD                 | 决定后续 BODY 应按哪种命令解释。      |
| 3        | 读取 SEQ                 | 用于响应匹配和多帧归组。              |
| 4        | 读取 LEN_L、LEN_H        | 小端组合为 BODY 长度。                |
| 5        | 读取 LEN 个 BODY 字节    | BODY 到哪里结束只看 LEN，不靠猜。     |
| 6        | 读取 CRC_L、CRC_H        | CRC 紧跟在 BODY 后面 2 字节。         |
| 7        | 计算并比较 CRC16-Modbus  | calc_crc == recv_crc 才进入命令处理。 |

# 4. 响应统一规则

| **项目**   | **规则**               | **说明**                                       |
|------------|------------------------|------------------------------------------------|
| 响应命令字 | RESP_CMD = CMD \| 0x80 | 例如 0x01 的响应是 0x81，0x20 的响应是 0xA0。  |
| 响应 SEQ   | 原样返回请求 SEQ       | 用于前端判断这条响应对应哪条请求。             |
| 响应 BODY  | status(1) + data(...)  | BODY 第 1 字节一定是状态码，后面才是业务数据。 |
| TLV        | 不使用                 | 不做 TLV，不单独做 result TLV。                |

# 5. 基础枚举表

| **枚举类别** | **值** | **含义**                      |
|--------------|--------|-------------------------------|
| 状态码       | 0x00   | SUCC / 成功                   |
| 状态码       | 0x01   | CRC_ERR / CRC 错误            |
| 状态码       | 0x02   | LEN_ERR / 长度错误            |
| 状态码       | 0x03   | CMD_ERR / 命令错误            |
| 状态码       | 0x04   | PARAM_ERR / 参数错误          |
| 状态码       | 0x05   | NOT_CONFIG / 当前不在配置模式 |
| 状态码       | 0x06   | ROLE_MISMATCH / 角色不匹配    |
| 状态码       | 0x07   | WL_FULL / 白名单已满          |
| 状态码       | 0x08   | NOT_FOUND / 未找到            |
| 状态码       | 0x09   | SAVE_FAIL / 保存失败          |
| 状态码       | 0x0A   | BUSY / 忙                     |
| 角色         | 0x00   | NODE                          |
| 角色         | 0x01   | ROOT                          |
| 波特率挡位   | 0x00   | 1200                          |
| 波特率挡位   | 0x01   | 2400                          |
| 波特率挡位   | 0x02   | 4800                          |
| 波特率挡位   | 0x03   | 9600                          |
| 波特率挡位   | 0x04   | 19200                         |
| 波特率挡位   | 0x05   | 38400                         |
| 波特率挡位   | 0x06   | 57600                         |
| 波特率挡位   | 0x07   | 115200                        |
| 校验位       | 0x00   | None                          |
| 校验位       | 0x01   | Even                          |
| 校验位       | 0x02   | Odd                           |
| 停止位       | 0x01   | 1 位停止位                    |
| 停止位       | 0x02   | 2 位停止位                    |
| 数据位       | 0x07   | 7 位                          |
| 数据位       | 0x08   | 8 位                          |
| 设备类型     | 0x01   | 三相电表                      |
| 设备类型     | 0x02   | 单相电表                      |
| 设备类型     | 0x03   | 温湿度变送器                  |
| 设备类型     | 0x04   | 继电器                        |
| 设备类型     | 0x05   | 其他预留                      |

# 6. 命令总表

| **分类** | **CMD** | **名称**         | **请求 BODY**                               | **响应 BODY / 说明**                                                                                                   |
|----------|---------|------------------|---------------------------------------------|------------------------------------------------------------------------------------------------------------------------|
| 读取     | 0x01    | READ_DEV_INFO    | 无                                          | status + role + mac + name_len + name                                                                                  |
| 读取     | 0x02    | READ_UART_CFG    | 无                                          | status + baud_level + parity + stop_bits + data_bits                                                                   |
| 读取     | 0x03    | READ_MODBUS_CFG  | 无                                          | status + item_count + (addr + dev_type) \* N                                                                           |
| 读取     | 0x04    | READ_ROOT_WL_ALL | 无                                          | status + frag_idx + frag_total + wl_total + item_count + items                                                         |
| 读取     | 0x05    | READ_ROOT_POWER  | 无                                          | status + power                                                                                                         |
| 读取     | 0x06    | GET_MODE_STATUS  | 无                                          | status + current_mode + role + baud_level + parity + stop_bits + data_bits + mode_source + rx_profile + reboot_pending |
| 读取     | 0x07    | READ_WL_NODE_CFG | mac(6)                                      | status + baud_level + parity + stop_bits + data_bits + item_count + (addr + dev_type) * N                            |
| 写入     | 0x10    | SET_ROLE         | role                                        | status                                                                                                                 |
| 写入     | 0x11    | SET_UART_CFG     | baud_level + parity + stop_bits + data_bits | status                                                                                                                 |
| 写入     | 0x12    | SET_MODBUS_CFG   | item_count + (addr + dev_type) \* N         | status                                                                                                                 |
| 写入     | 0x13    | SET_ROOT_POWER   | power                                       | status                                                                                                                 |
| 写入     | 0x14    | ADD_WL_ITEM      | mac + name_len + name                       | status                                                                                                                 |
| 写入     | 0x15    | DEL_WL_ITEM      | mac                                         | status                                                                                                                 |
| 写入     | 0x16    | CLEAR_WL         | 无                                          | status                                                                                                                 |
| 写入     | 0x17    | SET_WL_NODE_CFG  | mac(6) + uart_cfg(4) + item_count + (addr + dev_type) * N | status                                                                                                    |
| 控制     | 0x20    | COMMIT           | 无                                          | status；保存当前配置到 NV                                                                                              |
| 控制     | 0x21    | REBOOT           | 无                                          | status；响应后重启                                                                                                     |
| 控制     | 0x22    | FACTORY_RESET    | 无或确认字节                                | status；恢复默认配置                                                                                                   |

# 7. 各命令 BODY 字段说明

| **命令/结构**    | **方向**  | **BODY 偏移** | **长度** | **字段**       | **含义**                                                     |
|------------------|-----------|---------------|----------|----------------|--------------------------------------------------------------|
| READ_DEV_INFO    | 响应      | 0             | 1        | status         | 状态码，0x00 表示成功                                        |
| READ_DEV_INFO    | 响应      | 1             | 1        | role           | 设备角色：0x00=NODE，0x01=ROOT                               |
| READ_DEV_INFO    | 响应      | 2             | 6        | mac            | 设备 MAC，6 字节                                             |
| READ_DEV_INFO    | 响应      | 8             | 1        | name_len       | name 字节长度                                                |
| READ_DEV_INFO    | 响应      | 9             | N        | name           | 设备名称，ASCII 字节，例如 DTU_xxxxxx                        |
| READ_UART_CFG    | 请求/响应 | 0             | 1        | baud_level     | 波特率挡位，0x07=115200                                      |
| READ_UART_CFG    | 请求/响应 | 1             | 1        | parity         | 校验位，0x00=None，0x01=Even，0x02=Odd                       |
| READ_UART_CFG    | 请求/响应 | 2             | 1        | stop_bits      | 停止位，0x01=1 位，0x02=2 位                                 |
| READ_UART_CFG    | 请求/响应 | 3             | 1        | data_bits      | 数据位，0x07=7 位，0x08=8 位                                 |
| READ_MODBUS_CFG  | 请求/响应 | 0             | 1        | item_count     | Modbus 预设项数量，范围 0~8                                  |
| READ_MODBUS_CFG  | 请求/响应 | 1+2n          | 1        | addr           | 第 n 项设备站号，即 Modbus 地址                              |
| READ_MODBUS_CFG  | 请求/响应 | 2+2n          | 1        | dev_type       | 第 n 项设备类型，例如 0x01=三相电表，0x03=温湿度             |
| READ_ROOT_POWER  | 请求/响应 | 0             | 1        | power          | ROOT 发射/功率参数，示例值 0x05                              |
| READ_ROOT_WL_ALL | 响应      | 0             | 1        | status         | 状态码                                                       |
| READ_ROOT_WL_ALL | 响应      | 1             | 1        | frag_idx       | 当前分片序号，从 1 开始                                      |
| READ_ROOT_WL_ALL | 响应      | 2             | 1        | frag_total     | 总分片数                                                     |
| READ_ROOT_WL_ALL | 响应      | 3             | 1        | wl_total       | 白名单总条数                                                 |
| READ_ROOT_WL_ALL | 响应      | 4             | 1        | item_count     | 当前分片内白名单条数                                         |
| 白名单 item      | 请求/响应 | var           | 6        | mac            | 子设备 MAC                                                   |
| 白名单 item      | 请求/响应 | var           | 1        | name_len       | 子设备 name 长度                                             |
| 白名单 item      | 请求/响应 | var           | N        | name           | 子设备 name，例如 DTU_N01                                    |
| READ_WL_NODE_CFG | 请求      | 0             | 6        | mac            | 目标白名单 node 的 MAC                                       |
| READ_WL_NODE_CFG | 响应      | 0             | 1        | status         | 状态码                                                       |
| READ_WL_NODE_CFG | 响应      | 1             | 1        | baud_level     | 该 node 子配置中的串口波特率挡位                            |
| READ_WL_NODE_CFG | 响应      | 2             | 1        | parity         | 该 node 子配置中的串口校验位                                |
| READ_WL_NODE_CFG | 响应      | 3             | 1        | stop_bits      | 该 node 子配置中的停止位                                    |
| READ_WL_NODE_CFG | 响应      | 4             | 1        | data_bits      | 该 node 子配置中的数据位                                    |
| READ_WL_NODE_CFG | 响应      | 5             | 1        | item_count     | 该 node 子配置中的 Modbus 预设项数量                        |
| READ_WL_NODE_CFG | 响应      | 6+2n          | 1        | addr           | 第 n 项 Modbus 地址                                          |
| READ_WL_NODE_CFG | 响应      | 7+2n          | 1        | dev_type       | 第 n 项设备类型                                              |
| GET_MODE_STATUS  | 响应      | 0             | 1        | status         | 状态码                                                       |
| GET_MODE_STATUS  | 响应      | 1             | 1        | current_mode   | 当前模式，CONFIG 或 RUN；具体数值以代码定义为准              |
| GET_MODE_STATUS  | 响应      | 2             | 1        | role           | 当前角色                                                     |
| GET_MODE_STATUS  | 响应      | 3             | 1        | baud_level     | 当前/保存的波特率挡位                                        |
| GET_MODE_STATUS  | 响应      | 4             | 1        | parity         | 校验位                                                       |
| GET_MODE_STATUS  | 响应      | 5             | 1        | stop_bits      | 停止位                                                       |
| GET_MODE_STATUS  | 响应      | 6             | 1        | data_bits      | 数据位                                                       |
| GET_MODE_STATUS  | 响应      | 7             | 1        | mode_source    | 模式来源，当前主要由 GPIO13 拨码决定；具体数值以代码定义为准 |
| GET_MODE_STATUS  | 响应      | 8             | 1        | rx_profile     | 接收配置/通道相关状态；具体数值以代码定义为准                |
| GET_MODE_STATUS  | 响应      | 9             | 1        | reboot_pending | 是否有待重启生效项；具体数值以代码定义为准                   |
| SET_WL_NODE_CFG  | 请求      | 0             | 6        | mac            | 目标白名单 node 的 MAC                                       |
| SET_WL_NODE_CFG  | 请求      | 6             | 1        | baud_level     | 该 node 子配置的波特率挡位                                  |
| SET_WL_NODE_CFG  | 请求      | 7             | 1        | parity         | 该 node 子配置的校验位                                      |
| SET_WL_NODE_CFG  | 请求      | 8             | 1        | stop_bits      | 该 node 子配置的停止位                                      |
| SET_WL_NODE_CFG  | 请求      | 9             | 1        | data_bits      | 该 node 子配置的数据位                                      |
| SET_WL_NODE_CFG  | 请求      | 10            | 1        | item_count     | 要写入的 Modbus 预设项数量                                  |
| SET_WL_NODE_CFG  | 请求      | 11+2n         | 1        | addr           | 第 n 项 Modbus 地址                                          |
| SET_WL_NODE_CFG  | 请求      | 12+2n         | 1        | dev_type       | 第 n 项设备类型                                              |

# 8. ROOT / NODE 读写顺序

| **角色** | **读取顺序**                                                                               | **写入顺序**                                                                             |
|----------|--------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| ROOT     | 1.READ_DEV_INFO；2.READ_UART_CFG；3.READ_MODBUS_CFG；4.READ_ROOT_WL_ALL；5.READ_ROOT_POWER；6.READ_WL_NODE_CFG | 1.SET_ROLE；2.SET_UART_CFG；3.SET_MODBUS_CFG；4.SET_ROOT_POWER；5.白名单增删改；6.SET_WL_NODE_CFG；7.COMMIT |
| NODE     | 1.READ_DEV_INFO；2.READ_UART_CFG；3.READ_MODBUS_CFG                                        | 1.SET_ROLE；2.SET_UART_CFG；3.SET_MODBUS_CFG；4.COMMIT                                   |

# 9. 配置生效规则

| **配置项**       | **写入后是否立即改变当前串口/运行状态** | **是否需要 COMMIT** | **是否需要 REBOOT**         | **说明**                                                      |
|------------------|-----------------------------------------|---------------------|-----------------------------|---------------------------------------------------------------|
| SET_ROLE         | RAM 中角色可立即变化                    | 需要                | 建议重启验证                | 读 READ_DEV_INFO 可确认角色变化；不 COMMIT 则重启后可能丢失。 |
| SET_UART_CFG     | 当前串口不立即重配                      | 需要                | 需要                        | COMMIT + REBOOT 后才按保存值重新初始化串口。                  |
| SET_MODBUS_CFG   | 配置表可立即读回                        | 需要                | 建议重启验证                | 用于设备轮询预设。                                            |
| SET_ROOT_POWER   | ROOT 下可立即读回                       | 需要                | 建议重启验证                | NODE 下访问返回 ROLE_MISMATCH。                               |
| ADD/DEL/CLEAR_WL | ROOT 下可立即读回                       | 需要                | 建议重启验证                | NODE 下访问返回 ROLE_MISMATCH。                               |
| SET_WL_NODE_CFG  | ROOT 下可立即读回                       | 需要                | 建议重启验证                | 只修改指定白名单 node 的 uart_cfg 与 modbus 预设。            |
| FACTORY_RESET    | 恢复默认配置                            | 内部处理            | 通常会结合重启/重新读取验证 | 恢复角色、串口、Modbus、功率、白名单默认值。                  |

# 10. 白名单 node 子配置规则

| **项目** | **规则** |
|----------|----------|
| 子配置归属 | 只有 `ROOT` 角色维护的白名单项才带 node 子配置。 |
| 包含内容 | 每个白名单 node 额外带：`uart_cfg + modbus_count + modbus[]`。 |
| 不包含内容 | node 不单独保存 `power`。 |
| power 规则 | node 统一跟随 `ROOT` 当前 `power`；初始默认值仍由系统默认 `5` 起步。 |
| 新增白名单项默认值 | 执行 `ADD_WL_ITEM` 新建成员时，默认继承当前 `ROOT runtime` 的 `uart_cfg` 和 `modbus` 预设。 |
| 单独修改方式 | 后续通过 `SET_WL_NODE_CFG` 单独改该 node 的 `uart_cfg / modbus`。 |
| 持久化方式 | 和其他配置一样，`COMMIT` 后写入 NV，重启后恢复。 |

# 11. 固定示例报文说明

| **测试项**            | **发送 Hex**                                                      | **BODY 含义**                                        | **备注**                     |
|-----------------------|-------------------------------------------------------------------|------------------------------------------------------|------------------------------|
| 读取设备信息          | AA 55 01 01 00 00 50 18                                           | 空 BODY                                              | 返回 role/mac/name。         |
| 读取串口配置          | AA 55 02 02 00 00 A0 5C                                           | 空 BODY                                              | 默认应返回 07 00 01 08。     |
| 读取白名单 node 子配置 | AA 55 07 07 06 00 A1 A2 A3 A4 A5 A6 80 F3                         | MAC=A1:A2:A3:A4:A5:A6                                | 返回该 node 的 uart/modbus。 |
| 设置为 ROOT           | AA 55 10 06 01 00 01 75 4B                                        | role=0x01                                            | 响应 status=00 表示成功。    |
| 设置为 NODE           | AA 55 10 06 01 00 00 B4 8B                                        | role=0x00                                            | 响应 status=00 表示成功。    |
| 设置串口 115200 8N1   | AA 55 11 07 04 00 07 00 01 08 F6 AD                               | baud_level=07，parity=00，stop_bits=01，data_bits=08 | 保存后重启才按新参数初始化。 |
| 设置 Modbus 预设 2 项 | AA 55 12 08 05 00 02 01 02 05 03 10 5B                            | item_count=2；addr1=1,type1=2；addr2=5,type2=3       | 站号1单相电表，站号5温湿度。 |
| 新增白名单 DTU_N01    | AA 55 14 0A 0E 00 A1 A2 A3 A4 A5 A6 07 44 54 55 5F 4E 30 31 07 7B | MAC=A1:A2:A3:A4:A5:A6；name_len=7；name=DTU_N01      | ROOT 专属。                  |
| 设置白名单 node 子配置 | AA 55 17 08 0F 00 A1 A2 A3 A4 A5 A6 07 00 01 08 02 01 05 02 05 91 F7 | 目标 MAC + uart(115200 8N1) + 2 项 modbus          | ROOT 专属。                  |

整理备注：GET_MODE_STATUS
为完整测试流程中当前代码已实现指令，原配置协议文档未单独列出，因此本说明按当前完整测试流程补充。
