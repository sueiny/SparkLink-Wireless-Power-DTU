#include "dtu_service_inner.h"

#include "securec.h"

/* 协议装配器职责：  核心:拆解加校验
 * 1. 纯粹处理字节流到帧结构体的转换
 * 2. 提供 CRC16 与响应帧打包能力
 * 3. 不依赖 mode / storage / transport 业务逻辑
 */

/* 以小端格式向缓冲区追加 16 位整数。 */
static void dtu_protocol_append_u16_le(uint8_t *buf, uint16_t *offset, uint16_t value)
{
    buf[*offset] = (uint8_t)(value & 0xFF);
    buf[*offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    *offset = (uint16_t)(*offset + 2);
}

/* 重置协议解析状态机，准备接收下一帧。 */
void dtu_protocol_parser_init(dtu_protocol_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    parser->state = DTU_PARSE_SOF0;
    parser->cmd = 0;
    parser->seq = 0;
    parser->len = 0;
    parser->body_pos = 0;
    parser->crc_recv = 0;
    parser->skip_left = 0;
}

/* 计算 DTU 协议使用的 Modbus CRC16。 */
uint16_t dtu_protocol_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = ((crc & 0x0001U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* 在收到完整帧后，填充标准帧结构体。 */
static void dtu_protocol_fill_frame(const dtu_protocol_parser_t *parser, dtu_frame_t *frame)
{
    frame->cmd = parser->cmd;
    frame->seq = parser->seq;
    frame->len = parser->len;
    if (parser->len > 0) {
        (void)memcpy_s(frame->body, sizeof(frame->body), parser->body, parser->len);
    }
}

/* 单字节推进状态机，输出完整帧或明确协议错误。 */
dtu_protocol_status_t dtu_protocol_feed_byte(dtu_protocol_parser_t *parser, uint8_t byte, dtu_frame_t *frame)
{
    if (parser == NULL || frame == NULL) {
        return DTU_PROTOCOL_STATUS_INVALID_SOF;
    }

    switch (parser->state) {
        case DTU_PARSE_SOF0:
            if (byte == DTU_CFG_SOF0) {
                parser->state = DTU_PARSE_SOF1;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_SOF1:
            if (byte == DTU_CFG_SOF1) {
                parser->state = DTU_PARSE_CMD;
                return DTU_PROTOCOL_STATUS_INCOMPLETE;
            }
            parser->state = (byte == DTU_CFG_SOF0) ? DTU_PARSE_SOF1 : DTU_PARSE_SOF0;
            return DTU_PROTOCOL_STATUS_INVALID_SOF;
        case DTU_PARSE_CMD:
            parser->cmd = byte;
            parser->state = DTU_PARSE_SEQ;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_SEQ:
            parser->seq = byte;
            parser->state = DTU_PARSE_LEN0;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_LEN0:
            parser->len = byte;
            parser->state = DTU_PARSE_LEN1;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_LEN1:
            parser->len |= (uint16_t)(byte << 8);
            parser->body_pos = 0;
            if (parser->len > DTU_CFG_MAX_FRAME_BODY) {
                parser->skip_left = (uint16_t)(parser->len + 2);
                parser->state = DTU_PARSE_SKIP;
            } else if (parser->len == 0) {
                parser->state = DTU_PARSE_CRC0;
            } else {
                parser->state = DTU_PARSE_BODY;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_BODY:
            parser->body[parser->body_pos++] = byte;
            if (parser->body_pos >= parser->len) {
                parser->state = DTU_PARSE_CRC0;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_CRC0:
            parser->crc_recv = byte;
            parser->state = DTU_PARSE_CRC1;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_CRC1: {
            uint8_t crc_input[4 + DTU_CFG_MAX_FRAME_BODY];
            uint16_t crc_len = 0;
            uint16_t calc_crc;
            uint16_t recv_crc;

            recv_crc = (uint16_t)(parser->crc_recv | (uint16_t)(byte << 8));
            crc_input[crc_len++] = parser->cmd;
            crc_input[crc_len++] = parser->seq;
            crc_input[crc_len++] = (uint8_t)(parser->len & 0xFF);
            crc_input[crc_len++] = (uint8_t)((parser->len >> 8) & 0xFF);
            if (parser->len > 0) {
                (void)memcpy_s(&crc_input[crc_len], sizeof(crc_input) - crc_len, parser->body, parser->len);
                crc_len = (uint16_t)(crc_len + parser->len);
            }

            dtu_protocol_fill_frame(parser, frame);
            calc_crc = dtu_protocol_crc16(crc_input, crc_len);
            dtu_protocol_parser_init(parser);
            return (calc_crc == recv_crc) ? DTU_PROTOCOL_STATUS_OK : DTU_PROTOCOL_STATUS_CRC_ERR;
        }
        case DTU_PARSE_SKIP:
            if (parser->skip_left > 0) {
                parser->skip_left--;
            }
            if (parser->skip_left == 0) {
                frame->cmd = parser->cmd;
                frame->seq = parser->seq;
                frame->len = 0;
                dtu_protocol_parser_init(parser);
                return DTU_PROTOCOL_STATUS_LEN_ERR;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        default:
            dtu_protocol_parser_init(parser);
            return DTU_PROTOCOL_STATUS_INVALID_SOF;
    }
}

/* 打包完整响应帧，供路由层交给具体 transport 发送。 */
errcode_t dtu_protocol_pack_response(uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len,
    uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint16_t offset = 0;
    uint16_t crc;

    if (out == NULL || out_len == NULL || body_len > DTU_CFG_MAX_FRAME_BODY) {
        return ERRCODE_FAIL;
    }
    if (body_len > 0 && body == NULL) {
        return ERRCODE_FAIL;
    }
    if (out_size < (uint16_t)(body_len + 8)) {
        return ERRCODE_FAIL;
    }

    out[offset++] = DTU_CFG_SOF0;
    out[offset++] = DTU_CFG_SOF1;
    out[offset++] = (uint8_t)(cmd | 0x80U);
    out[offset++] = seq;
    dtu_protocol_append_u16_le(out, &offset, body_len);
    if (body_len > 0 && body != NULL) {
        if (memcpy_s(&out[offset], out_size - offset, body, body_len) != EOK) {
            return ERRCODE_FAIL;
        }
        offset = (uint16_t)(offset + body_len);
    }

    crc = dtu_protocol_crc16(&out[2], (uint16_t)(4 + body_len));
    dtu_protocol_append_u16_le(out, &offset, crc);
    *out_len = offset;
    return ERRCODE_SUCC;
}
