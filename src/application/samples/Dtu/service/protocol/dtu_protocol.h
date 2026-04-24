#ifndef DTU_PROTOCOL_H
#define DTU_PROTOCOL_H

/* protocol 头文件：
 * 1. 只描述协议帧、解析器和纯协议 API
 * 2. 不承载 mode / storage / transport 语义
 */

#include <stdint.h>

#include "dtu_build_config.h"
#include "errcode.h"

typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t body[DTU_CFG_MAX_FRAME_BODY];
} dtu_frame_t;

typedef enum {
    DTU_PROTOCOL_STATUS_INCOMPLETE = 0,
    DTU_PROTOCOL_STATUS_OK,
    DTU_PROTOCOL_STATUS_CRC_ERR,
    DTU_PROTOCOL_STATUS_LEN_ERR,
    DTU_PROTOCOL_STATUS_INVALID_SOF
} dtu_protocol_status_t;

typedef enum {
    DTU_PARSE_SOF0 = 0,
    DTU_PARSE_SOF1,
    DTU_PARSE_CMD,
    DTU_PARSE_SEQ,
    DTU_PARSE_LEN0,
    DTU_PARSE_LEN1,
    DTU_PARSE_BODY,
    DTU_PARSE_CRC0,
    DTU_PARSE_CRC1,
    DTU_PARSE_SKIP
} dtu_parse_state_t;

typedef struct {
    dtu_parse_state_t state;
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint16_t body_pos;
    uint16_t crc_recv;
    uint16_t skip_left;
    uint8_t body[DTU_CFG_MAX_FRAME_BODY];
} dtu_protocol_parser_t;

void dtu_protocol_parser_init(dtu_protocol_parser_t *parser);
dtu_protocol_status_t dtu_protocol_feed_byte(dtu_protocol_parser_t *parser, uint8_t byte, dtu_frame_t *frame);
uint16_t dtu_protocol_crc16(const uint8_t *data, uint16_t len);
errcode_t dtu_protocol_pack_response(uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len,
    uint8_t *out, uint16_t out_size, uint16_t *out_len);

#endif
