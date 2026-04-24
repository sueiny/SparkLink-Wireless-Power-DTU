#include "soc_osal.h"
#include "app_init.h"
#include "common_def.h"
#include "osal_debug.h"

#include "cloud_common.h"
#include "common_uart.h"



#define INIT_TASK_PRIO                     24
#define INIT_TASK_STACK_SIZE               0x1024



#define GNSS_TASK_STACK_SIZE 0x1024
#define GNSS_TASK_PRIO 26







void init_task(void)
{
    
    osal_printk("Init task start\r\n");
    cloud_common_init();//初始化云平台公共资源，如消息队列、事件等
    uart_init();
     
    osal_task *task_gnss = osal_kthread_create((osal_kthread_handler)dtu_task,0,"dtu_task",GNSS_TASK_STACK_SIZE);
    if(task_gnss)  osal_kthread_set_priority(task_gnss, GNSS_TASK_PRIO);
    
}


static void main_entry(void)
{

    osal_kthread_lock();

    osal_task *task_init = osal_kthread_create((osal_kthread_handler)init_task,0,"init_task",INIT_TASK_STACK_SIZE);
    if(task_init)  osal_kthread_set_priority(task_init,24);

    osal_kthread_unlock();
}

app_run(main_entry);
