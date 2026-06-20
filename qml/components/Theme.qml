pragma Singleton
import QtQuick 2.15

QtObject {
    // —— 表面 / 背景 ——
    readonly property color surface: "#ffffff"      // 面板/内容背景
    readonly property color surfaceAlt: "#f1f5f9"   // 次级背景（信息列、空轨道）
    readonly property color windowBg: "#e9edf3"     // 窗口底色
    readonly property color rowSelected: "#e8f1ff"  // 选中行

    // —— 线条 / 边框 ——
    readonly property color border: "#cbd5e1"       // 面板/单元边框
    readonly property color gridLine: "#e2e8f0"     // 图表网格线 / 行分隔
    readonly property color tickStrong: "#94a3b8"   // 强调刻度

    // —— 文字 ——
    readonly property color textPrimary: "#111827"
    readonly property color textSecondary: "#475569"
    readonly property color textMuted: "#64748b"

    // —— 强调 / 语义 ——
    readonly property color accent: "#2563eb"       // 主强调（选中框、拖拽高亮、默认序列色）
    readonly property color accentSoft: "#dbeafe"   // 弱强调填充
    readonly property color danger: "#dc2626"       // 播放头 / 停止 / 错误

    // —— 相机瓦片暗底 ——
    readonly property color cameraTileBg: "#162033"
}
