/**
 * @file main.c
 * @brief DTU（数据传输单元）应用程序主入口文件
 * @details 本文件是DTU应用程序的入口点，负责初始化系统资源并创建核心任务。
 *          DTU主要用于将GNSS模块的数据通过L610 4G模块上传至云平台，
 *          实现远程数据传输和设备管理功能。
 * @version 1.0
 * @date 2026-05-20
 */

#include "soc_osal.h"
/**
 * @brief 应用程序初始化头文件，提供app_run等入口函数
 */
#include "app_init.h"
/**
 * @brief 通用定义头文件，包含常用宏和类型定义
 */
#include "common_def.h"
/**
 * @brief 操作系统抽象层调试接口，提供打印函数如osal_printk
 */
#include "osal_debug.h"

/**
 * @brief 云平台公共资源模块，提供消息队列、事件、缓冲区管理等功能
 */
#include "cloud_common.h"
/**
 * @brief 通用UART驱动模块，负责GNSS和L610串口的初始化与数据收发
 */
#include "common_uart.h"



#define INIT_TASK_PRIO                     24  // 初始化任务优先级，数值越小优先级越低
#define INIT_TASK_STACK_SIZE               0x1024  // 初始化任务栈大小，单位字节（4KB）



#define GNSS_TASK_STACK_SIZE 0x1024  // GNSS任务栈大小，单位字节（4KB）
#define GNSS_TASK_PRIO 26  // GNSS任务优先级，高于初始化任务以确保实时性







/**
 * @brief 初始化任务函数
 * @details 该任务负责系统资源的初始化，包括：
 *          1. 初始化云平台公共资源（消息队列、事件、缓冲区等）
 *          2. 初始化UART串口（GNSS和L610）
 *          3. 创建并启动DTU主任务
 * @note 该任务在系统启动时执行一次，完成后可考虑删除自身以释放资源
 */
void init_task(void)
{
    
    osal_printk("Init task start\r\n");  // 打印初始化任务启动信息
    cloud_common_init();  // 初始化云平台公共资源：创建消息队列、事件组、互斥锁和临时缓冲区
    uart_init();  // 初始化UART串口：配置GNSS和L610的引脚、波特率，并注册接收回调
     
    osal_task *task_gnss = osal_kthread_create((osal_kthread_handler)dtu_task,0,"dtu_task",GNSS_TASK_STACK_SIZE);  // 创建DTU任务线程
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
