# 拓竹屏源码

这里保留 F101S3 拓竹屏应用模块，以及 4.3 英寸 V2 的构建配置参考。该模块覆盖设备发现、MQTT 状态、相机、存储、云监控及 LVGL 页面。

| 路径 | 内容 |
| --- | --- |
| [src/bambu_panel](src/bambu_panel) | 应用、页面、模型、协议和平台适配 |
| [config/defconfig](config/defconfig) | 4.3 英寸拓竹屏的配置，开启 CONFIG_APP_BAMBU_PANEL |
| [config/sys_config.fex](config/sys_config.fex) | 4.3 英寸板级配置，Wi‑Fi 由使用者配网 |
| [config/sys_partition_bambu_nor.fex](config/sys_partition_bambu_nor.fex) | SPI NOR 分区参考 |

集成到匹配的 KSDIY F101S3 SDK 时，应用目录对应 lichee/rtos/projects/f101s3/ksdiy_f101s3/src/bambu_panel。仍需原工程的板级和 HAL、LVGL 共享层、网络适配与资源，包括字体、提示音和由 bambu_photo.c 使用的机型图片头文件。配置用于对照原 SDK，保留已有板级设置与分区选择逻辑。源码是应用模块快照，固件目录里的历史 IMG 按各自版本使用。
