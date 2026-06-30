# 数据面板标签功能完善设计（黑块修复 + 多标签编辑）

日期：2026-06-30
状态：设计已确认，待写实现计划

## Context（背景与问题）

「数据」面板有两个标签相关问题：

1. **渲染 bug —— 黑块**：会话列表里每个无标签会话的右侧画出一个黑色圆角块（见用户截图，顶部「在线数据」之外每个历史会话行都有）。
2. **缺功能 —— 无法编辑标签**：底部「记录标签」面板的标签当前只在*录制前预选、停录时写入*；无法为**正在录制**的会话实时体现，也无法为**已有历史会话**增删标签。

涉及组件：
- [RecordingSessionsPanel.qml](../../../qml/components/RecordingSessionsPanel.qml)：「数据」面板 = 顶部「在线数据」行 + 历史会话 `ListView`，每行右下角一个 `TagChip`。
- [RecordingTagsPanel.qml](../../../qml/components/RecordingTagsPanel.qml)：底部「记录标签」面板，`Repeater` 渲染可勾选的标签按钮。
- [TagChip.qml](../../../qml/components/TagChip.qml)：标签胶囊（圆角矩形 + 文字/纯点）。
- [TagListModel](../../../src/ui_models.cpp) / [RecordingSessionModel](../../../src/ui_models.cpp)：标签选择模型 / 会话列表模型。
- [AppController](../../../src/app_controller.cpp)：录制/历史状态机；持有 `session_manager_`、`scanned_sessions_`。
- [SessionManager](../../../src/session_manager.cpp)：`write_session_yaml` / `scan` 读写 `session.yaml`。

## 根因（黑块）

会话无标签时，[ui_models.cpp `setSessions`](../../../src/ui_models.cpp) 不填 `tag_name`/`tag_color`，二者保持空 `QString`。[RecordingSessionsPanel.qml](../../../qml/components/RecordingSessionsPanel.qml) 把空的 `model.tagColor` 绑到 `TagChip.chipColor`（`color` 类型）——QML 中空/非法颜色字符串退化为**黑色 `#000000`**，且 `tagName` 为空使 `dotOnly=false`，于是画出一个无字的黑色圆角块。截图里每个会话都没标签，所以全是黑块。

## Goals / Non-Goals

**Goals**
- 消除黑块：无标签的会话行不画任何 chip。
- 一个会话可贴**多个**标签。
- 底部「记录标签」面板**跟随当前选中的数据源**编辑标签（状态机见下）。
- 录制中勾选的标签实时显示在「在线数据」行；停录后随会话进入历史列表，并**清空「在线数据」行**。
- 历史会话改标签**即时同步**写回 `session.yaml`。

**Non-Goals**
- 不改 `TagChip.qml`（无标签不画 chip 即根因修复，无需在 chip 内做空色兜底——YAGNI）。
- 不新增/删除标签*定义*（标签集合仍来自 config）；本特性只增删某会话**已贴**的标签。
- 录制中不即时写盘（停录时一次性写，与现有机制一致）。
- 不引入异步写盘（`session.yaml` 极小，GUI 线程同步写）。

## 状态机（底部「记录标签」面板的作用对象）

| 状态 | 底部面板 | 「在线数据」行 chip | 写盘 |
|---|---|---|---|
| 在线 / 未录制 | 置灰、不可点 | 空 | — |
| **录制中** | 可多选勾选，UI 即时高亮 | **实时显示当前勾选的标签** | 不写（仅内存） |
| 停止录制（瞬间） | 清空勾选 | **清空 chip** | 同步写 `session.yaml`；新会话进历史列表（带标签） |
| 选中历史会话 | 显示该会话已有标签，点击切换 | 空 | **即时**同步写回该会话 `session.yaml` 并刷新该行 |

「在线数据」行的 chip 数据源 = `TagListModel` 当前勾选集合（实时），**不是**某个 `SessionRecord`。停录 = 清空勾选集合 → 该行回到无 chip。

## 设计

### 1. 黑块修复（渲染）
- [RecordingSessionsPanel.qml](../../../qml/components/RecordingSessionsPanel.qml) 历史会话行：把单个 `TagChip` 改为 `Repeater` 渲染会话的**标签数组**；数组为空则不渲染任何 chip（黑块消失）。
- 「在线数据」行：新增一处 `Repeater` 渲染 `TagListModel` 当前勾选集合（录制中实时显示；非录制态为空）。

### 2. `TagListModel` 单选 → 多选
文件：[ui_models.cpp](../../../src/ui_models.cpp) / [ui_models.hpp](../../../include/data_recorder/ui_models.hpp)
- 用**选中集合**替代 `selected_row_`（单 int）与 `session_selected_names_`（两套字段）——统一为一个状态，消除「录制预选 vs 历史显示」的割裂。集合用按行下标的 `std::set<int>`；载入历史标签时按 tag name 匹配填入下标。
- `Q_INVOKABLE void select(int row)`：改为 **toggle**（在集合则移出，否则加入），发 `dataChanged(IsSelectedRole)`。
- `IsSelectedRole`：据集合判定。
- `exportSelectedTags()`：返回集合内所有 tag（保持返回 `std::vector<TagRecord>`，调用点不变）。
- `setSelectedTags(tags)`：按 name 匹配填集合（载入历史会话标签时用）。
- `clearSelection()`：清空集合。
- 新增供「在线数据」行显示用的只读访问：暴露当前勾选 tag 列表给 QML（如新增 `Q_PROPERTY`/`Q_INVOKABLE` 返回 `QVariantList` of `{name,color}`，或由 QML 遍历模型按 `isSelected` 过滤——实现时择简）。

### 3. `RecordingSessionModel` 单标签 → 多标签
文件：[ui_models.cpp](../../../src/ui_models.cpp) / [ui_models.hpp](../../../include/data_recorder/ui_models.hpp)
- 行结构存全部标签（`std::vector<TagRecord>`）而非单个 `tag_name/tag_color`。
- 新增角色 `TagsRole`（`"tags"`）返回 `QVariantList` of `{name,color}`；移除或保留旧 `TagNameRole/TagColorRole`（YAGNI：直接用新角色，删旧的，同步改其唯一消费者 `RecordingSessionsPanel.qml`）。
- 新增 `Q_INVOKABLE`（或方法）`updateSessionTags(int row, const std::vector<TagRecord> &)`：更新该行标签并发 `dataChanged`。

### 4. `AppController` 状态机与写回
文件：[app_controller.cpp](../../../src/app_controller.cpp) / [app_controller.hpp](../../../include/data_recorder/app_controller.hpp)
- 新增 `Q_INVOKABLE void toggleTag(int tagRow)`：底部面板点击的统一入口，按当前状态分派：
  - **未录制且非历史**：no-op（面板在 QML 侧已置灰，双保险）。
  - **录制中**：`tag_model_.select(tagRow)`（toggle 内存勾选）。UI 经 `IsSelectedRole` + 「在线数据」行 `Repeater` 即时反映。停录时既有 `exportSelectedTags()` 自动带多标签（[app_controller.cpp:256](../../../src/app_controller.cpp)），无需改调用点。
  - **选中历史会话**：toggle `tag_model_` 勾选 → 同步：
    1. 由 `exportSelectedTags()` 取最新集合，写入 `scanned_sessions_[selected_session_row_].tags`；
    2. `session_manager_->write_session_yaml(scanned_sessions_[row])`（**无损**：该 record 含 topics/annotations/recorded_at，已由 `scan()` 完整载入，验证见 [session_manager.cpp](../../../src/session_manager.cpp)）；
    3. `recording_session_model_.updateSessionTags(row, tags)` 刷新该行 chip。
- 录制开始（`toggleRecording` start 分支）已 `tag_model_.clearSelection()`（[app_controller.cpp:241](../../../src/app_controller.cpp)）——保证「在线数据」行从空开始。
- 录制停止：在既有停录逻辑后 `tag_model_.clearSelection()`，清空「在线数据」行 chip。
- 选中历史会话已 `tag_model_.setSelectedTags(session.tags)`（[app_controller.cpp:379](../../../src/app_controller.cpp)）——面板正确回显该会话标签。
- 暴露一个 QML 可读的"标签是否可编辑"状态（`recording || historyMode`），供底部面板置灰/启用绑定（可复用现有 `recording`/`historyMode` 属性，QML 侧组合）。

### 5. 底部面板 QML
文件：[RecordingTagsPanel.qml](../../../qml/components/RecordingTagsPanel.qml)
- 点击调 `controller.toggleTag(index)`（替代直接 `model.select(index)`，让 C++ 按状态分派写盘）。
- 整个 Flow 置灰/禁用：`enabled: controller && (controller.recording || controller.historyMode)`；禁用时降透明度。

## 数据流

```
录制中点标签：
  QML toggleTag(i) → AppController(录制中) → tag_model_.select(i)
    → IsSelectedRole 变 → 底部高亮 + 「在线数据」行 Repeater 实时显示
  停录 → exportSelectedTags() 随 stop_session 写 session.yaml → 新会话进历史列表
       → clearSelection() → 「在线数据」行清空

历史会话点标签：
  QML toggleTag(i) → AppController(历史) → tag_model_.select(i)
    → scanned_sessions_[row].tags = exportSelectedTags()
    → session_manager_->write_session_yaml(record)   [同步、无损]
    → recording_session_model_.updateSessionTags(row, tags)  [刷新行 chip]
```

## 行为/边界核对
- 无标签会话：不画 chip（黑块消失）。
- 多标签会话行：多个 chip 横排（`TagChip` 已有 `dotOnly` 溢出退化为点，空间不足自动收缩）。
- 录制中切到看历史？录制中历史行禁用（既有 `enabled: !recording`），不会发生跨态。
- 历史写回失败（磁盘只读等）：`write_session_yaml` 失败应不崩 GUI；行为是内存已改但落盘失败——实现时至少不抛异常穿出 GUI 线程（记录或静默，沿用现有"静默跳过损坏 yaml"风格）。

## 测试与验证
- **C++ 单测**（[test_ui_models.cpp](../../../test/test_ui_models.cpp)）：
  - `TagListModel` 多选：toggle 同一行两次回到未选；多行可同时选；`exportSelectedTags` 返回全部；`setSelectedTags` 按 name 命中填集合；`clearSelection` 清空。
  - `RecordingSessionModel`：`TagsRole` 返回多标签数组；无标签行返回空数组；`updateSessionTags` 改后 `dataChanged` 且角色返回更新值。
- **历史写回往返测试**（新增，仿 [test_session_manager.cpp](../../../test/test_session_manager.cpp)）：构造带 topics/annotations 的 `SessionRecord` → `write_session_yaml` → 改 tags → 再 `write_session_yaml` → `scan` 重读 → tags 更新且 topics/annotations/recorded_at **无损保留**。
- **QML 结构**（[test_qml_structure.cpp](../../../test/test_qml_structure.cpp)）：会话行用 `Repeater`+`TagChip` 且 `tags` 角色；底部面板 `enabled` 绑定 `recording || historyMode`；「在线数据」行含勾选标签 `Repeater`。
- **QML 冒烟**（[test_qml_smoke.cpp](../../../test/test_qml_smoke.cpp)，真实引擎）：录制中点标签 → `tag_model_` 选中 + 「在线数据」行出现 chip；停录 → 行清空；选中历史会话点标签 → `RecordingSessionModel` 行标签更新（经 controller 写回路径）。
- **回归**：现有 184+ 测试绿（注意调整断言了旧 `tagName/tagColor` 单标签角色的测试）。
- **人工确认**：截图确认黑块消失、多 chip 显示、三态编辑（GUI 点击自动化不可靠，故人工核对外观）。

## 风险
- **契约面**：TagListModel 选择语义（单→多）被录制流程与历史回显共用，改动需同步两处调用点 + 删除旧 `selected_row_/session_selected_names_` 双字段——单测兜底。
- **旧角色清理**：移除 `TagNameRole/TagColorRole` 须同步其唯一 QML 消费者与相关测试，避免悬空引用。
- **同步写盘**：历史改标签在 GUI 线程同步写 `session.yaml`；文件极小可接受，但须保证失败不抛穿 GUI。

## 文件清单（预计触及）
- C++：[ui_models.cpp](../../../src/ui_models.cpp) / [ui_models.hpp](../../../include/data_recorder/ui_models.hpp)（TagListModel 多选 + RecordingSessionModel 多标签角色 + updateSessionTags）、[app_controller.cpp](../../../src/app_controller.cpp) / [app_controller.hpp](../../../include/data_recorder/app_controller.hpp)（toggleTag 分派 + 历史写回 + 停录清空）。
- QML：[RecordingTagsPanel.qml](../../../qml/components/RecordingTagsPanel.qml)（多选 + 置灰 + 经 controller）、[RecordingSessionsPanel.qml](../../../qml/components/RecordingSessionsPanel.qml)（历史行多 chip + 「在线数据」行勾选 chip）。
- 测试：[test_ui_models.cpp](../../../test/test_ui_models.cpp)、[test_qml_structure.cpp](../../../test/test_qml_structure.cpp)、[test_qml_smoke.cpp](../../../test/test_qml_smoke.cpp)、可能 [test_session_manager.cpp](../../../test/test_session_manager.cpp)（往返）。
