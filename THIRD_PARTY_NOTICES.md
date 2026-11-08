# 第三方来源与许可

本项目保留各文件和组件自带的版权及许可。根目录MIT不覆盖独立第三方材料原有条款。

| 材料 | 来源与许可 |
|---|---|
| 固件基础 | [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，MIT；保留Shenzhen Xinzhi Future Technology和Project Contributors版权声明 |
| 固件SDK | [ESP-IDF](https://github.com/espressif/esp-idf)，各SDK/组件原有许可；通过本机工具链使用，不将SDK全量上传 |
| 界面 | [LVGL](https://github.com/lvgl/lvgl)，MIT，通过组件管理器获取 |
| 本地硬件组件 | `common_components`、`firmware/components`中保留各自LICENSE、license.txt或文件版权头；含乐鑫组件、Wi-Fi连接组件、FatFs等 |
| 固定中文字体 | [Noto Sans CJK](https://github.com/notofonts/noto-cjk)，SIL Open Font License 1.1；原字体/许可和子集生成字集位于assets/fonts |
| 动态字体、图标和小智资源 | 固件组件依赖提供，按对应上游包许可使用；不将组件管理器缓存当作本项目原创内容 |
| 简体规范化 | [opencc-python-reimplemented](https://github.com/yichen0831/opencc-python)，0.1.7，Apache-2.0；原许可保留在host/codex_sync/vendor |
| 本地ASR | [faster-whisper](https://github.com/SYSTRAN/faster-whisper)，通过Python包安装，使用其原有许可；模型权重单独获取，仓库不附带 |
| 木鱼短音效 | [hollandm / Woodblock-med.wav](https://freesound.org/people/hollandm/sounds/692819/)，CC0；使用公开HQ预览降调/裁切至120ms，保留来源说明 |

本项目没有纳入之前调研的HachimoDock、Codex Pet或游戏项目的代码/私有美术资源。后续引入时应新增对应来源和许可记录。
