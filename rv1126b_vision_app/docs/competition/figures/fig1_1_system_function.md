# 图1-1 系统功能示意图

![图1-1 系统功能示意图](fig1_1_system_function.png)

图注：
本图展示系统从视觉输入、RV1126B 端侧处理、功能判断到多端反馈的整体功能关系。

文件说明：
- SVG 矢量图：`fig1_1_system_function.svg`
- PNG 位图：`fig1_1_system_function.png`
- 图中上位机仅作为视频查看和 MQTT 订阅端，不作为 AI 推理节点。

Mermaid 备份：
```mermaid
flowchart LR
    A["输入层<br/>摄像头画面 / 用户手势 / 人体姿态 / 桌面杯瓶"] --> B["端侧处理层<br/>RV1126B 端侧 AI / 图像预处理 / 多模型协同 / 状态机融合"]
    B --> C["功能层<br/>手势启停 / 坐姿提醒 / 饮水判断 / 定时饮水提醒"]
    C --> D["反馈层<br/>ST7789 本地屏幕 / HTTP-FLV 与 RTSP relay / MQTT 状态同步"]

```
