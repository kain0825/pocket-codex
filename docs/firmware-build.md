# 固件构建与恢复

## 构建

使用 ESP-IDF **5.5.4**，在已经执行其 `export.ps1`（Windows）或 `export.sh` 的终端中操作。

先执行仓库的 `scripts/setup.py` 生成自己的 `firmware/main/pocket_pairing.local.h`，然后：

```text
cd firmware
idf.py set-target esp32p4
idf.py build
```

首轮依赖由 ESP-IDF Component Manager 依据清单/锁文件获取，`managed_components` 不提交。项目包含当前需要的本地组件，包括带exFAT支持的FatFs；各组件许可保留。

## 安装和恢复边界

本项目当前提供源码预览，不提供供所有人直接刷写的预配对镜像。构建通过也不代表所有硬件修订均已验收。

对于空白/已备份设备，ESP-IDF生成的全量刷写参数只适用于该次构建的分区布局。**对于已有固件的板子，先备份并核对分区表、当前运行槽和芯片修订。** 不要把本仓库示例当成任意设备的通用刷写命令。

开发来源的原设备曾运行于 `ota_1`、偏移 `0x410000`；这是历史验证条件，不能照抄到别人的板子。本仓库没有默认执行的刷机脚本，也不自动写入C6、分区表、NVS或TF卡。

Wi-Fi OTA有已知看门狗问题；公开版不开放OTA命令入口。保留USB恢复和每次构建的应用、ELF/map、配置与哈希，先完成独立硬件验收再考虑扩大部署。

## 字体

两个固定字体子集已包含在源码中，普通构建无需重生成。来源是Noto Sans CJK，原字体及OFL许可在 `assets/fonts`。修改字集后可用npm安装的 `lv_font_conv` 和 `scripts/regenerate_fonts.py --converter <lv_font_conv.js>` 重生成。
