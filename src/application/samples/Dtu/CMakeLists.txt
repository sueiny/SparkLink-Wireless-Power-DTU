#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/service/dtu_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/dtu_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/flow/dtu_flow.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/protocol/dtu_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/storage/dtu_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_sle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/mode/dtu_mode_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/service/mode/dtu_mode_run.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dtu_main.c
)

set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/service
    ${CMAKE_CURRENT_SOURCE_DIR}/service/flow
    ${CMAKE_CURRENT_SOURCE_DIR}/service/mode
    ${CMAKE_CURRENT_SOURCE_DIR}/service/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/service/storage
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
