#include "app_init.h"
#include "soc_osal.h"

#include "dtu_service.h"

#define DTU_INIT_TASK_STACK_SIZE 0x2000
#define DTU_INIT_TASK_PRIO       24

/* 启动 DTU 服务初始化任务，避免在应用入口直接做较重初始化。 */
static void dtu_init_task(void)
{
    /* 真正的协议处理在服务内部，这里只负责启动 DTU 配置服务。 */
    osal_printk("[DTU LOG] DtuInitTask begin\r\n");
    if (dtu_service_init() != ERRCODE_SUCC) {
        osal_printk("DTU sample init failed\r\n");
        return;
    }
    osal_printk("[DTU LOG] DtuInitTask end\r\n");
}

/* 注册 DTU 示例入口，由系统线程在合适时机拉起初始化任务。 */
static void dtu_main_entry(void)
{
    osal_task *task = NULL;

    /* 通过应用任务启动，确保系统起来后再初始化串口和协议服务。 */
    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)dtu_init_task, 0, "DtuInitTask", DTU_INIT_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, DTU_INIT_TASK_PRIO);
    }
    osal_kthread_unlock();
}

app_run(dtu_main_entry);
