# 磁盘剩余空间统计 — 设计文档

## 需求

在 UI 右下角状态栏（`StatusBar.qml`）实现磁盘剩余空间实时显示，替换现有占位符 `"磁盘 --"`。

## 决策摘要

| 项目 | 决策 |
|------|------|
| 显示格式 | `磁盘 X.X / Y GB`（剩余 / 总容量，保留一位小数） |
| 警告策略 | 两级：正常灰色，剩余 < 10 GB 变红 |
| 刷新频率 | 每 30 秒 |
| 实现方案 | AppController 内 QTimer + Q_PROPERTY（方案 A） |

## 架构

### C++ — `AppController`

**新增成员变量（`app_controller.hpp` private 区）：**

```cpp
QTimer disk_timer_;
QString disk_space_text_{"磁盘 -- / --"};
bool disk_space_low_{false};
```

**新增 Q_PROPERTY（`app_controller.hpp`）：**

```cpp
Q_PROPERTY(QString diskSpaceText READ diskSpaceText NOTIFY diskSpaceTextChanged)
Q_PROPERTY(bool diskSpaceLow READ diskSpaceLow NOTIFY diskSpaceLowChanged)
```

**新增信号：**

```cpp
void diskSpaceTextChanged();
void diskSpaceLowChanged();
```

**新增公共读取方法：**

```cpp
QString diskSpaceText() const;
bool diskSpaceLow() const;
```

**新增私有方法：**

```cpp
void refreshDiskSpace();
```

**实现逻辑（`app_controller.cpp`）：**

`refreshDiskSpace()` 内部：
1. 用 `QStorageInfo(output_directory_)` 查询挂载点
2. 读取 `bytesAvailable()`（剩余）和 `bytesTotal()`（总量）
3. 若 `bytesTotal() <= 0`，将 `disk_space_text_` 保持为 `"磁盘 -- / --"`，`disk_space_low_` 保持为 false，不更新不发信号
4. 转换为 GB（除以 `1'000'000'000.0`），各保留一位小数
5. 格式化：`QString("磁盘 %1 / %2 GB").arg(avail_gb, 0, 'f', 1).arg(total_gb, 0, 'f', 1)`
6. 若文本或 low 标志发生变化，更新成员并 emit 对应信号

构造函数中：
```cpp
disk_timer_.setInterval(30'000);
connect(&disk_timer_, &QTimer::timeout, this, &AppController::refreshDiskSpace);
disk_timer_.start();
refreshDiskSpace();  // 立即执行一次，避免冷启动 30 秒空白
```

**阈值常量（文件顶部 anonymous namespace）：**

```cpp
constexpr qint64 kDiskWarnBytes = 10LL * 1'000'000'000;  // 10 GB
```

`disk_space_low_` 在 `refreshDiskSpace()` 中设为 `bytesAvailable() < kDiskWarnBytes`。

### QML — `StatusBar.qml`

将现有第 36–40 行：

```qml
Label {
    text: "磁盘 --"
    color: Theme.textMuted
    font.pixelSize: 11
}
```

替换为：

```qml
Label {
    text: controller ? controller.diskSpaceText : "磁盘 -- / --"
    color: (controller && controller.diskSpaceLow) ? Theme.danger : Theme.textMuted
    font.pixelSize: 11
}
```

## 数据流

```
QTimer (30s)
  → AppController::refreshDiskSpace()
    → QStorageInfo(output_directory_)
    → 格式化字符串 + 判断阈值
    → emit diskSpaceTextChanged() / diskSpaceLowChanged()
      → StatusBar.qml Label.text / Label.color 自动更新
```

## 错误处理

- `QStorageInfo::bytesTotal() <= 0`（路径不存在、权限问题）：保持显示 `"磁盘 -- / --"`，不崩溃
- `output_directory_` 为 CONSTANT，无需处理路径变化

## 测试

- 不新增专用测试文件：`refreshDiskSpace()` 依赖真实文件系统，mock 价值低；阈值逻辑无分支复杂度
- 现有 `test_qml_smoke` 覆盖编译正确性
- 如需将来可测试，可将格式化提取为纯函数，但当前不做

## 改动文件

| 文件 | 改动类型 |
|------|----------|
| `include/data_recorder/app_controller.hpp` | 新增 2 个 Q_PROPERTY、2 个信号、2 个读取方法、1 个私有方法、3 个成员变量 |
| `src/data_recorder/src/app_controller.cpp` | 新增 `refreshDiskSpace()` 实现、构造函数初始化 timer |
| `qml/components/StatusBar.qml` | 替换占位符 Label |
