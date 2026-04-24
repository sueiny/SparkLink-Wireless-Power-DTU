# src/application/samples/peripheral/uart/CMakeLists.txt

set(SOURCES "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/uart_demo.c"   # 把你的 demo 代码拉进编译清单
    PARENT_SCOPE)

set(PUBLIC_HEADER "${PUBLIC_HEADER}" PARENT_SCOPE)