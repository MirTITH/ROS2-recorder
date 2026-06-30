# Disk Space StatusBar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 StatusBar.qml 右下角显示真实磁盘剩余/总容量（格式 `磁盘 X.X / Y GB`），剩余 < 10 GB 时变红，每 30 秒刷新。

**Architecture:** AppController 内新增 `QTimer`（30s）驱动 `refreshDiskSpace()`，用 `QStorageInfo` 查询 `output_directory_` 所在挂载点，结果暴露为两个 Q_PROPERTY（`diskSpaceText` / `diskSpaceLow`）供 QML 绑定。

**Tech Stack:** Qt6::Core (`QStorageInfo`, `QTimer`), QML property binding

---

## 改动文件一览

| 文件 | 类型 |
|------|------|
| `include/data_recorder/app_controller.hpp` | 修改 |
| `src/data_recorder/src/app_controller.cpp` | 修改 |
| `qml/components/StatusBar.qml` | 修改 |

---

### Task 1: AppController 头文件 — 声明新 Property、信号、方法、成员

**Files:**
- Modify: `src/data_recorder/include/data_recorder/app_controller.hpp`

- [ ] **Step 1: 添加 `#include <QTimer>`**

  在 `app_controller.hpp` 的 includes 区（当前 `#include <QThread>` 所在行之后）加入：

  ```cpp
  #include <QTimer>
  ```

  修改后 includes 区应为：
  ```cpp
  #include <QObject>
  #include <QString>
  #include <QThread>
  #include <QTimer>
  #include <QVariantList>
  ```

- [ ] **Step 2: 新增两个 Q_PROPERTY**

  在现有最后一个 Q_PROPERTY（`RecordingSessionModel * recordingSessionModel READ recordingSessionModel CONSTANT`）之后，紧接着添加：

  ```cpp
  Q_PROPERTY(QString diskSpaceText READ diskSpaceText NOTIFY diskSpaceTextChanged)
  Q_PROPERTY(bool diskSpaceLow READ diskSpaceLow NOTIFY diskSpaceLowChanged)
  ```

- [ ] **Step 3: 新增两个公共读取方法声明**

  在 public 区现有方法列表（`int visibleCameraCount() const;` 之后）添加：

  ```cpp
  QString diskSpaceText() const;
  bool diskSpaceLow() const;
  ```

- [ ] **Step 4: 新增两个信号声明**

  在 signals 区（`void visibleCameraCountChanged();` 之后）添加：

  ```cpp
  void diskSpaceTextChanged();
  void diskSpaceLowChanged();
  ```

- [ ] **Step 5: 新增私有方法声明**

  在 private 区（`void refreshStatusText();` 所在行之后）添加：

  ```cpp
  void refreshDiskSpace();
  ```

- [ ] **Step 6: 新增三个私有成员变量**

  在 private 成员区末尾（`RecordingSessionModel recording_session_model_;` 之后）添加：

  ```cpp
  QTimer disk_timer_;
  QString disk_space_text_{"磁盘 -- / --"};
  bool disk_space_low_{false};
  ```

---

### Task 2: AppController 实现 — 添加 include、常量、getter、refreshDiskSpace、timer 初始化

**Files:**
- Modify: `src/data_recorder/src/app_controller.cpp`

- [ ] **Step 1: 添加两个 include**

  在 `app_controller.cpp` 顶部 includes 区（当前 `#include <QKeyEvent>` 和 `#include <QStringList>` 所在行之后）加入：

  ```cpp
  #include <QStorageInfo>
  #include <QTimer>
  ```

  修改后 Qt includes 区应为：
  ```cpp
  #include <QKeyEvent>
  #include <QStorageInfo>
  #include <QStringList>
  #include <QTimer>
  ```

- [ ] **Step 2: 在 anonymous namespace 添加磁盘阈值常量**

  在已有的 anonymous namespace 中（`kDefaultTimelineSpanSeconds` 之后）添加：

  ```cpp
  constexpr qint64 kDiskWarnBytes = 10LL * 1'000'000'000;  // 10 GB
  ```

  修改后 anonymous namespace 应为：
  ```cpp
  namespace
  {
  constexpr double kDefaultTimelineSpanSeconds = 60.0;
  constexpr qint64 kDiskWarnBytes = 10LL * 1'000'000'000;  // 10 GB
  }  // namespace
  ```

- [ ] **Step 3: 在构造函数末尾添加 timer 初始化**

  在构造函数中 `refreshSessions();  // 启动扫描` 这一行之后、`}` 之前，添加：

  ```cpp
  connect(&disk_timer_, &QTimer::timeout, this, &AppController::refreshDiskSpace);
  disk_timer_.setInterval(30'000);
  disk_timer_.start();
  refreshDiskSpace();
  ```

- [ ] **Step 4: 添加两个 getter 实现**

  在现有 `bool AppController::recording() const` 实现之后（约第 127 行之后），添加：

  ```cpp
  QString AppController::diskSpaceText() const
  {
    return disk_space_text_;
  }

  bool AppController::diskSpaceLow() const
  {
    return disk_space_low_;
  }
  ```

- [ ] **Step 5: 添加 `refreshDiskSpace()` 实现**

  在 `void AppController::refreshSessions()` 实现之前添加：

  ```cpp
  void AppController::refreshDiskSpace()
  {
    QStorageInfo info(output_directory_);
    if (info.bytesTotal() <= 0) {
      return;
    }

    const double avail_gb = info.bytesAvailable() / 1'000'000'000.0;
    const double total_gb = info.bytesTotal() / 1'000'000'000.0;
    const QString text =
      QString("磁盘 %1 / %2 GB").arg(avail_gb, 0, 'f', 1).arg(total_gb, 0, 'f', 1);
    const bool low = info.bytesAvailable() < kDiskWarnBytes;

    if (text != disk_space_text_) {
      disk_space_text_ = text;
      emit diskSpaceTextChanged();
    }
    if (low != disk_space_low_) {
      disk_space_low_ = low;
      emit diskSpaceLowChanged();
    }
  }
  ```

---

### Task 3: StatusBar.qml — 替换占位符 Label

**Files:**
- Modify: `src/data_recorder/qml/components/StatusBar.qml`

- [ ] **Step 1: 替换磁盘占位符 Label**

  将现有第 36–40 行的 Label：

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

---

### Task 4: 构建并验证

**Files:** 无代码改动，仅验证

- [ ] **Step 1: 构建**

  ```bash
  source ~/.local/ros2_rc && rr && colcon build \
    --symlink-install \
    --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
    --continue-on-error \
    --mixin release compile-commands ccache \
    --packages-select data_recorder
  ```

  预期：构建成功，无编译警告或错误。

- [ ] **Step 2: 启动应用并目视验证**

  ```bash
  source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
    --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
  ```

  验证项：
  - 状态栏右下角显示 `磁盘 X.X / Y GB`（非 `磁盘 --`）
  - 磁盘充足时文字为灰色（`Theme.textMuted`）
  - 无崩溃，无 QML 报错
