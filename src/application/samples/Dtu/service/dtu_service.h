#ifndef DTU_SERVICE_H
#define DTU_SERVICE_H

#include "errcode.h"

/* 对外只暴露 DTU 服务初始化入口，内部协议和模式细节全部收敛在私有实现中。 */
/* 在专用配置串口上初始化 DTU 本地配置服务。 */
errcode_t dtu_service_init(void);

#endif
