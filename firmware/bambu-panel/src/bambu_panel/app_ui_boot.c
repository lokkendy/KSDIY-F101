/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY Action6 无线屏（F101S3 移植） - UI 引导
 *
 * 与 ESP 版 app_main 的对应关系：
 *
 *   ESP 版                                 F101 版（本文件）
 *   ------------------------------------   ------------------------------------
 *   ksdiy_lvgl_port_init()（BSP 一并做）   lv_init + sunxifb disp + indev
 *   ksdiy_lvgl_lock/unlock                 port/ksdiy_lvgl_port.c
 *   App_Init()                             App_Init()（原样调用）
 *
 * 时序（顺序有依赖）：
 *   1. 先建锁（后面所有 UI 操作都靠它）
 *   2. lv_init → 显示端口 → 输入端口 → 旋转
 *   3. 起 LVGL 任务（此后 lv_timer_handler 常驻）
 *   4. 在锁内跑 App_Init（建外壳/页面，与原工程完全一致）
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ksdiy_power_key.h"

#include "lvgl.h"
#include "lv_sunxi_port_disp.h"
#ifdef LV_USE_SUNXI_INDEV
#include "lv_sunxi_port_indev.h"
#endif

#include "port/ksdiy_lvgl_port.h"
#include "port/nvs_port.h"
#include "HAL.h"
#include "App.h"

/* 屏幕方向：480x800 竖屏面板，本应用按横屏 800x480 设计 UI。
 * 旋转由 sunxifb 的 G2D 硬件旋转实现（日志里的
 * "Turn on 2d hardware acceleration rotate"）。 */
#include "ksdiy_rotation.h"

/* UI 引导任务栈：lv_init + sunxifb 端口 + disp_ioctl + hal_malloc 的调用链很深，
 * 原厂 CONFIG_INIT_THREAD_STACK_SIZE=1024 完全不够（实测会冲毁寄存器，
 * 表现为 EXC_STORE_ACCESS 写代码段 / EXC_INST_ACCESS 跳数据区）。
 * 这里起独立任务，不占用 init-thread 的栈。
 *
 * 注意：xTaskCreate() 的第 3 个参数是**字数（StackType_t 个数）**，
 * 不是字节数 —— tasks.c 里会做 `usStackDepth * sizeof(StackType_t)`。
 * 因此这里写 16384 表示 16384 字 = 64KB（32 位栈宽）。
 * RAM 有 16MB，这点开销可忽略。 */
#define PORT_UI_BOOT_STACK_WORDS   (16384)
/* UI 引导任务优先级。
 *
 * ⚠ 必须**低于** disp2 任务（优先级 15）！
 *   lv_free_rtos_disp2_init() 会调 disp_init_wait() 等 g_disp_event，
 *   而该事件由 disp2 任务设置。若 ui_boot 优先级 >= disp2，
 *   ui_boot 会一直抢占 disp2，事件永远等不到 → disp_init_wait() 空转
 *   约 2 秒后「静默超时返回」→ LVGL 用未初始化的 disp 结构继续初始化 → 崩溃。
 *   （原厂 HAL_THREAD_PRIORITY_APP = 16，正好踩这个坑，故不能照抄）
 *
 * 顺带：这个任务只做一次性初始化，跑完就 vTaskDelete，优先级不影响运行期性能。
 */
#define PORT_UI_BOOT_PRIO   5

static void app_ui_boot_task(void *arg)
{
    (void)arg;

    printf("\n[app] === KSDIY Bambu Panel (F101S3) ===\n");
    printf("[app] build %s %s\n", __DATE__, __TIME__);
    printf("[app] ui boot task stack %u words (%u bytes)\n",
           (unsigned)PORT_UI_BOOT_STACK_WORDS,
           (unsigned)(PORT_UI_BOOT_STACK_WORDS * sizeof(StackType_t)));

    /* 1. 锁必须最先建：后面 lv_init 之后任何跨线程操作都要它 */
    port_lvgl_lock_init();

    /* 1.5 显示端口初始化。
     *
     * 这里**不再**做固定延时等待：真正确保 disp2 就绪的是
     * lv_sunxifb_port_disp_init() → sunxifb_init() → lv_free_rtos_disp2_init()
     * → disp_init_wait()，它内部用 hal_ev_wait_all 等
     * EVT_BIT_DISPLAY_INIT | EVT_BIT_DISPLAY_WORK 两个事件位
     * （已加 10s 超时 + 诊断打印，不会再永久阻塞）。
     *
     * 前提条件是 ui_boot 的优先级必须**低于** disp2 任务（15），
     * 否则事件永远等不到 —— 见 PORT_UI_BOOT_PRIO 的说明。
     */
    printf("[app] starting LVGL\n");

    /* 2. LVGL 核心 + 显示/触摸端口 */
    lv_init();
    lv_sunxifb_port_disp_init();
#ifdef LV_USE_SUNXI_INDEV
    /* 触摸是可选的：无触摸板时（CONFIG_LVGL9_USE_SUNXI_INDEV=n）
     * 不注册输入设备，UI 用按键自检模式操作。 */
    lv_sunxi_port_indev_init();
#else
    printf("[app] no touch input (LVGL9_USE_SUNXI_INDEV off)\n");
#endif

    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        printf("[app] FATAL: lv display not created\n");
        vTaskDelete(NULL);
        return;
    }
    lv_display_set_rotation(disp, (lv_display_rotation_t)ksdiy_rotation_get());
    printf("[rotation] LVGL + touch orientation=%u degrees\n",ksdiy_rotation_get()*90);

    /* 3. 通知 HAL 显示已就绪（HAL_Display_Init 里的检查） */
    hal_display_mark_ready();

    /* 4. 起 LVGL 任务：之后 lv_timer_handler 常驻，
     *    页面里的 lv_timer / 动画 / 绘制都在这个任务里跑 */
    port_lvgl_start_task(5);

    /* 5. 配置存储（NVS 兼容层）先载入，settings_init 才有数据可读 */
    nvs_port_init();

    /* 6. 应用装配（原工程逻辑，零改动） */
    App_Init();
    ksdiy_power_key_start();

    printf("[app] === boot done ===\n");

    /* 引导完成，本任务退出（UI 已由 port_lvgl_start_task 起的任务接管） */
    vTaskDelete(NULL);
}

void app_ui_boot(void)
{
    BaseType_t ok = xTaskCreate(app_ui_boot_task, "ui_boot",
                                PORT_UI_BOOT_STACK_WORDS,
                                NULL, PORT_UI_BOOT_PRIO, NULL);
    if (ok != pdPASS) {
        printf("[app] FATAL: create ui_boot task failed\n");
    }
}
