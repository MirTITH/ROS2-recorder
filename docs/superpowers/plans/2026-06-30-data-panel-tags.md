# 数据面板标签功能完善 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复无标签会话的黑块，并让「记录标签」面板跟随选中数据源做多标签编辑（录制中实时显示在「在线数据」行、停录随会话写入并清空；历史会话即时同步写回 session.yaml）。

**Architecture:** 自底向上：先把 `TagListModel` 从单选改成多选（统一选中状态），再让 `RecordingSessionModel` 暴露多标签数组角色 + `updateSessionTags`，然后在 `AppController` 加 `toggleTag` 按状态分派（录制中=改内存勾选；历史=写回 session.yaml 并刷新行）并在停录后清空勾选，最后接 QML（底部面板经 controller、置灰；会话行/在线行渲染多 chip）。C++ 内部 `SessionRecord` 不变；历史写回用既有 `SessionManager::write_session_yaml`（无损）。

**Tech Stack:** C++17 / Qt 6（QAbstractListModel, QVariantList）/ ament_cmake / gtest（含真实 QML 引擎冒烟）。

**约定（命令里使用）：** `WS=/home/nros/Documents/Woosh/ros2_recorder_ws`；环境 `source ~/.local/ros2_rc && rr`；包 `$WS/src/data_recorder`；测试二进制 `$WS/build/data_recorder/`。分支 `feature/data-panel-tags`（已建，勿新建）。
构建（含测试）：
```
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
```
注意：colcon+ccache 可能返回过期测试二进制；构建后用 `cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target <name>` 强制重建目标。QML 文件运行时从源码目录加载（`DATA_RECORDER_QML_DIR` 指向源 `qml/`），改 .qml 无需重装，但改测试 .cpp 需重编。clangd 可能有过期缓存误报——以实际编译为准。

---

## File Structure
- **Modify** `include/data_recorder/ui_models.hpp` — TagListModel（多选成员 + 新方法）、RecordingSessionModel（多标签行 + TagsRole + updateSessionTags）。
- **Modify** `src/ui_models.cpp` — 上述两模型实现。
- **Modify** `include/data_recorder/app_controller.hpp` / `src/app_controller.cpp` — `toggleTag` 分派、历史写回、停录清空。
- **Modify** `qml/components/RecordingTagsPanel.qml` — 经 controller.toggleTag、置灰、加 controller 属性。
- **Modify** `qml/components/RecordingSessionsPanel.qml` — 历史行多 chip + 「在线数据」行勾选 chip。
- **Modify** `qml/Main.qml` — 给 RecordingTagsPanel 传 `controller`。
- **Modify** 测试：`test/test_ui_models.cpp`、`test/test_session_manager.cpp`、`test/test_qml_structure.cpp`、`test/test_qml_smoke.cpp`。

执行顺序：Task 1（TagListModel 多选）→ Task 2（RecordingSessionModel 多标签）→ Task 3（AppController 分派+写回）→ Task 4（QML）→ Task 5（全量验证）。

---

## Task 1: TagListModel 单选 → 多选

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`, `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: 改/加失败测试**

在 `test/test_ui_models.cpp` 中，把现有 `TEST(TagListModel, SelectsOneTag)` 整体替换为多选语义，并在其后新增两个测试：
```cpp
TEST(TagListModel, SelectTogglesAndSupportsMultiple)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}, {"力控", "#7c4dff"}});

  model.select(0);
  model.select(2);  // 多选：0 与 2 同时选中
  EXPECT_TRUE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_FALSE(model.data(model.index(1, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TagListModel::IsSelectedRole).toBool());

  model.select(0);  // 再点 0：取消
  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
}

TEST(TagListModel, ExportsAllSelectedTags)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}, {"力控", "#7c4dff"}});
  model.select(0);
  model.select(2);

  const auto tags = model.exportSelectedTags();
  ASSERT_EQ(tags.size(), 2u);
  // 按行序导出：成功(0) 在前，力控(2) 在后
  EXPECT_EQ(tags[0].name, "成功");
  EXPECT_EQ(tags[1].name, "力控");
}

TEST(TagListModel, SetSelectedTagsMarksByName)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}, {"力控", "#7c4dff"}});
  model.setSelectedTags({{"失败", "#e03131"}, {"力控", "#7c4dff"}});

  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(1, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  // 导出顺序按行序
  const auto tags = model.exportSelectedTags();
  ASSERT_EQ(tags.size(), 2u);
  EXPECT_EQ(tags[0].name, "失败");
  EXPECT_EQ(tags[1].name, "力控");
}
```
保留现有 `TEST(TagListModel, StartsWithNoSelection)`、`ExportsSelectedTag`、`ClearSelectionEmptiesExport` 不动（它们在多选语义下仍成立：单选一个 = 集合一个元素）。

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='TagListModel.*'
```
Expected: FAIL —`select` 当前是单选互斥，`SelectTogglesAndSupportsMultiple`/`ExportsAllSelectedTags`/`SetSelectedTagsMarksByName` 不满足。

- [ ] **Step 3: 改 TagListModel 头（ui_models.hpp）**

把 `TagListModel` 的私有成员：
```cpp
private:
  std::vector<TagEntry> tags_;
  int selected_row_{-1};
  std::vector<std::string> session_selected_names_;
```
替换为：
```cpp
private:
  std::vector<TagEntry> tags_;
  std::set<int> selected_rows_;  // 多选：选中行下标集合（统一录制预选与历史回显）
```
并在文件顶部确保 `#include <set>` 存在（若缺则加，靠近其它标准库 include）。
`clearSelection()` 的注释（`// startSession 时清空（select(-1) 当前是 no-op，故需独立方法）`）可更新为 `// 清空多选集合`。其余公有签名（`select`/`exportSelectedTags`/`setSelectedTags`/`clearSelection`/`set_tags`）不变。

- [ ] **Step 4: 改 TagListModel 实现（ui_models.cpp）**

(a) `data(...)` 的 `IsSelectedRole` 分支（约 415-418 行）改为：
```cpp
    case IsSelectedRole:
      return selected_rows_.count(index.row()) != 0;
```
(b) `select(int row)`（约 433-454 行）整体替换为 toggle：
```cpp
void TagListModel::select(int row)
{
  if (!valid_row(row, static_cast<int>(tags_.size()))) {
    return;
  }
  if (selected_rows_.count(row) != 0) {
    selected_rows_.erase(row);
  } else {
    selected_rows_.insert(row);
  }
  const auto idx = index(row, 0);
  emit dataChanged(idx, idx, {IsSelectedRole});
}
```
(c) `set_tags(...)`（约 456-463 行）里把 `selected_row_ = -1; session_selected_names_.clear();` 替换为 `selected_rows_.clear();`。
(d) `exportSelectedTags()`（约 465-473 行）整体替换为按行序导出集合：
```cpp
std::vector<TagRecord> TagListModel::exportSelectedTags() const
{
  std::vector<TagRecord> out;
  for (int row : selected_rows_) {  // std::set<int> 升序迭代 = 行序
    if (valid_row(row, static_cast<int>(tags_.size()))) {
      out.push_back({tags_[static_cast<std::size_t>(row)].name,
        tags_[static_cast<std::size_t>(row)].color});
    }
  }
  return out;
}
```
(e) `clearSelection()`（约 475-491 行）整体替换为：
```cpp
void TagListModel::clearSelection()
{
  if (selected_rows_.empty()) { return; }
  selected_rows_.clear();
  if (!tags_.empty()) {
    emit dataChanged(index(0, 0), index(static_cast<int>(tags_.size()) - 1, 0),
      {IsSelectedRole});
  }
}
```
(f) `setSelectedTags(...)`（约 493-504 行）整体替换为按 name 命中填集合：
```cpp
void TagListModel::setSelectedTags(const std::vector<TagRecord> & tags)
{
  selected_rows_.clear();
  for (const auto & t : tags) {
    for (std::size_t i = 0; i < tags_.size(); ++i) {
      if (tags_[i].name == t.name) { selected_rows_.insert(static_cast<int>(i)); break; }
    }
  }
  if (!tags_.empty()) {
    emit dataChanged(index(0, 0), index(static_cast<int>(tags_.size()) - 1, 0),
      {IsSelectedRole});
  }
}
```

- [ ] **Step 5: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='TagListModel.*'
```
Expected: 全部 PASS。

- [ ] **Step 6: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(tags): TagListModel multi-select (unify recording preselect + history echo)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: RecordingSessionModel 多标签角色 + updateSessionTags

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`, `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: 改/加失败测试**

在 `test/test_ui_models.cpp`，把 `TEST(RecordingSessionModel, ExposesFolderDurationSizeAndTagRoles)` 末尾两行：
```cpp
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagColorRole).toString().isEmpty());
```
替换为 TagsRole 数组断言：
```cpp
  const auto tags = model.data(row, data_recorder::RecordingSessionModel::TagsRole).toList();
  ASSERT_EQ(tags.size(), 1);
  EXPECT_EQ(tags[0].toMap().value("name").toString().toStdString(), "成功");
  EXPECT_EQ(tags[0].toMap().value("color").toString().toStdString(), "#2f9e44");
```
并在该测试之后新增两个：
```cpp
TEST(RecordingSessionModel, UntaggedSessionHasEmptyTagsList)
{
  data_recorder::RecordingSessionModel model;
  data_recorder::SessionRecord r;
  r.session_id = "2026-06-30_00-00-00";
  r.directory = "/tmp/x/2026-06-30_00-00-00";
  r.duration_seconds = 10.0;
  r.size_bytes = 1024;
  // 无 tags
  model.setSessions({r});

  const auto row = model.index(0, 0);
  EXPECT_TRUE(model.data(row, data_recorder::RecordingSessionModel::TagsRole).toList().isEmpty());
}

TEST(RecordingSessionModel, UpdateSessionTagsReplacesRowTags)
{
  data_recorder::RecordingSessionModel model;
  data_recorder::SessionRecord r;
  r.session_id = "2026-06-30_00-00-01";
  r.directory = "/tmp/x/2026-06-30_00-00-01";
  r.duration_seconds = 10.0;
  r.size_bytes = 1024;
  model.setSessions({r});

  model.updateSessionTags(0, {{"成功", "#2f9e44"}, {"力控", "#7c4dff"}});
  const auto tags = model.data(model.index(0, 0),
    data_recorder::RecordingSessionModel::TagsRole).toList();
  ASSERT_EQ(tags.size(), 2);
  EXPECT_EQ(tags[0].toMap().value("name").toString().toStdString(), "成功");
  EXPECT_EQ(tags[1].toMap().value("name").toString().toStdString(), "力控");
}
```

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='RecordingSessionModel.*'
```
Expected: FAIL — `TagsRole` 与 `updateSessionTags` 未定义/编译错误。

- [ ] **Step 3: 改 RecordingSessionModel 头（ui_models.hpp）**

(a) `Roles` 枚举里把 `TagNameRole, TagColorRole,` 替换为 `TagsRole,`。
(b) 公有区加方法（在 `setSessions` 声明之后）：
```cpp
  void setSessions(const std::vector<SessionRecord> & sessions);
  // 替换某行的全部标签并发 dataChanged（历史会话即时改标签后刷新该行）。
  void updateSessionTags(int row, const std::vector<TagRecord> & tags);
```
(c) `RecordingSessionRow` 结构里把：
```cpp
    QString tag_name;
    QString tag_color;
```
替换为：
```cpp
    QVariantList tags;  // [{name, color}]；无标签则空
```
确保头部已 `#include <QVariantList>`（TopicListModel 已用，应已在；若缺则加）。`<vector>` 已在。

- [ ] **Step 4: 改 RecordingSessionModel 实现（ui_models.cpp）**

(a) `setSessions(...)`（约 922-925 行）把：
```cpp
    if (!s.tags.empty()) {
      row.tag_name = QString::fromStdString(s.tags.front().name);
      row.tag_color = QString::fromStdString(s.tags.front().color);
    }
```
替换为构建 tags 数组：
```cpp
    for (const auto & t : s.tags) {
      QVariantMap m;
      m.insert("name", QString::fromStdString(t.name));
      m.insert("color", QString::fromStdString(t.color));
      row.tags.push_back(m);
    }
```
(b) `data(...)` 把：
```cpp
    case TagNameRole:
      return session.tag_name;
    case TagColorRole:
      return session.tag_color;
```
替换为：
```cpp
    case TagsRole:
      return session.tags;
```
(c) `roleNames()` 把：
```cpp
    {TagNameRole, "tagName"},
    {TagColorRole, "tagColor"},
```
替换为：
```cpp
    {TagsRole, "tags"},
```
(d) 在 `setSessions` 实现之后新增 `updateSessionTags`：
```cpp
void RecordingSessionModel::updateSessionTags(int row, const std::vector<TagRecord> & tags)
{
  if (!valid_row(row, static_cast<int>(sessions_.size()))) { return; }
  QVariantList list;
  for (const auto & t : tags) {
    QVariantMap m;
    m.insert("name", QString::fromStdString(t.name));
    m.insert("color", QString::fromStdString(t.color));
    list.push_back(m);
  }
  sessions_[static_cast<std::size_t>(row)].tags = std::move(list);
  const auto idx = index(row, 0);
  emit dataChanged(idx, idx, {TagsRole});
}
```
确认 `ui_models.cpp` 顶部已 include `<QVariantMap>`（TopicListModel updateSeries 已用，应已在）。

- [ ] **Step 5: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='RecordingSessionModel.*'
```
Expected: 全部 PASS。

> 注意：`test/test_qml_smoke.cpp` 若引用了 `tagName/tagColor` 角色名（通过 QML），会在 Task 4 一并处理；此时只跑 test_ui_models。

- [ ] **Step 6: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(tags): RecordingSessionModel exposes tags[] role + updateSessionTags

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: AppController.toggleTag 分派 + 历史写回 + 停录清空

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`, `src/app_controller.cpp`
- Test: `test/test_ui_models.cpp`（AppController 用例）, `test/test_session_manager.cpp`（无损往返）

- [ ] **Step 1: 写失败测试 —— 历史写回（test_ui_models.cpp）**

`test_ui_models.cpp` 已 `#include "data_recorder/app_controller.hpp"`、`session_manager.hpp`（确认；若历史 AppController 测试已存在则必已 include）。在 AppController 相关测试区追加一个用例，验证选中历史会话点标签会改模型行并落盘。该测试需要一个带真实 session.yaml 的目录 + 注入了 SessionManager 的 AppController。仿现有 history 测试的 fixture 构造方式（用 `make_config_fixture()` 或等价；查看本文件中已有的 `AppControllerHistory*` 测试如何构造 controller 与注入 session，照搬其 SessionManager/scan 注入路径）：
```cpp
TEST(AppControllerTags, HistorySessionTagToggleWritesYamlAndUpdatesRow)
{
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "dr_appctrl_tag_rw";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "2026-06-30_09-00-00";
  fs::create_directories(dir);

  // 先写一个无标签会话（带 topics/annotation 以验证无损）。
  data_recorder::SessionManager mgr;
  data_recorder::SessionRecord rec;
  rec.session_id = "2026-06-30_09-00-00";
  rec.directory = dir.string();
  rec.duration_seconds = 12.0;
  rec.topics = {{"/joint_states", "rosbag"}};
  rec.annotations = {{"碰撞", "c", "point", "#e03131", 3.0, 0.0}};
  mgr.write_session_yaml(rec);

  data_recorder::ConfigData config;
  config.output_dir = tmp.string();
  config.tags = {{"成功", "#2f9e44"}, {"力控", "#7c4dff"}};
  data_recorder::AppController controller(config, nullptr, nullptr, &mgr);
  // refreshSessions 在构造时扫描 output_dir，应已发现该会话；选中它。
  ASSERT_GT(controller.recordingSessionModel()->rowCount(), 0);
  controller.selectHistorySession(0);

  // 点「成功」(行0)：写回 + 刷新行。
  controller.toggleTag(0);

  // 模型行已更新。
  const auto tags_role = data_recorder::RecordingSessionModel::TagsRole;
  const auto list = controller.recordingSessionModel()
    ->data(controller.recordingSessionModel()->index(0, 0), tags_role).toList();
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].toMap().value("name").toString().toStdString(), "成功");

  // 落盘且无损：重扫，tags 在、annotation 仍在。
  auto rescanned = mgr.scan(tmp.string());
  ASSERT_EQ(rescanned.size(), 1u);
  ASSERT_EQ(rescanned.front().tags.size(), 1u);
  EXPECT_EQ(rescanned.front().tags.front().name, "成功");
  ASSERT_EQ(rescanned.front().annotations.size(), 1u);
  EXPECT_EQ(rescanned.front().annotations.front().name, "碰撞");

  fs::remove_all(tmp);
}
```
确认 `test_ui_models.cpp` 顶部含 `#include <filesystem>` 与 `#include "data_recorder/session_manager.hpp"`（若缺则加）。
> 实现者注意：若现有 AppController 历史测试用的是不同的 SessionManager 注入/扫描方式（例如直接 `setSessions` 而非真实 scan），请改用真实 `SessionManager` + 真实目录（如上），因为本特性要验证*落盘*。

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='AppControllerTags.*'
```
Expected: FAIL — `toggleTag` 未声明（编译错误）。

- [ ] **Step 3: 加 toggleTag 声明（app_controller.hpp）**

在 `setSeriesVisible(...)` 声明之后、`bool eventFilter(...)` 之前，加：
```cpp
  // 底部「记录标签」面板点击入口：按当前状态分派——录制中切换内存勾选（停录时随会话写入）；
  // 选中历史会话时即时切换并同步写回该会话 session.yaml + 刷新行；未录制且非历史为 no-op。
  Q_INVOKABLE void toggleTag(int tag_row);
```

- [ ] **Step 4: 实现 toggleTag + 停录清空（app_controller.cpp）**

(a) 顶部确认已 `#include "data_recorder/session_manager.hpp"`（构造函数已用 SessionManager*，应已在；若仅前向声明则需为调用 `write_session_yaml` 加 include）。
(b) 在 `setSeriesVisible(...)` 的实现之后新增：
```cpp
void AppController::toggleTag(int tag_row)
{
  // 未录制且非历史：面板本应置灰，这里双保险忽略。
  if (!recording_ && !history_mode_) { return; }

  tag_model_.select(tag_row);  // toggle 内存勾选

  // 录制中：仅改内存，停录时随会话一次性写入（exportSelectedTags）。UI 经 IsSelectedRole 即时反映。
  if (recording_) { return; }

  // 选中历史会话：写回该会话 session.yaml + 刷新行。
  if (selected_session_row_ < 0 ||
    selected_session_row_ >= static_cast<int>(scanned_sessions_.size()))
  {
    return;
  }
  const auto tags = tag_model_.exportSelectedTags();
  auto & record = scanned_sessions_[static_cast<std::size_t>(selected_session_row_)];
  record.tags = tags;
  if (session_manager_) {
    session_manager_->write_session_yaml(record);  // 同步、无损（record 含 topics/annotations）
  }
  recording_session_model_.updateSessionTags(selected_session_row_, tags);
}
```
(c) 停录后清空「在线数据」行勾选：在 `toggleRecording()` 的停止分支里，`recording_ = false; refreshSessions();` 之后加 `tag_model_.clearSelection();`。具体定位——找到 stop 分支（`engine_->stop_session(...)` 之后），在 `refreshSessions();` 下一行加：
```cpp
    tag_model_.clearSelection();  // 清空「在线数据」行标签（已随会话写入历史）
```
（注意：start 分支已有 `tag_model_.clearSelection();`，无需重复；这里加的是 stop 分支。）

- [ ] **Step 5: 写无损往返补充测试（test_session_manager.cpp）**

在 `test/test_session_manager.cpp` 末尾追加一个"改 tags 后重写仍无损"的测试（锁定 write→改→write→scan 不丢 topics/annotations）：
```cpp
TEST(SessionManager, RewriteWithNewTagsPreservesOtherFields)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_retag";
  fs::remove_all(tmp);
  fs::create_directories(tmp / "2026-06-30_10-00-00");

  data_recorder::SessionManager mgr;
  auto record = make_record("2026-06-30_10-00-00");
  record.directory = (tmp / "2026-06-30_10-00-00").string();
  mgr.write_session_yaml(record);

  // 改 tags 后重写。
  record.tags = {{"成功", "#2f9e44"}, {"力控", "#7c4dff"}};
  mgr.write_session_yaml(record);

  auto sessions = mgr.scan(tmp.string());
  ASSERT_EQ(sessions.size(), 1u);
  const auto & s = sessions.front();
  ASSERT_EQ(s.tags.size(), 2u);
  EXPECT_EQ(s.tags[0].name, "成功");
  EXPECT_EQ(s.tags[1].name, "力控");
  // 其它字段无损
  ASSERT_EQ(s.annotations.size(), 4u);
  ASSERT_EQ(s.topics.size(), 2u);
  EXPECT_NEAR(s.duration_seconds, 42.512, 1e-6);

  fs::remove_all(tmp);
}
```

- [ ] **Step 6: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_ui_models test_session_manager
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='AppControllerTags.*'
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_session_manager
```
Expected: 全部 PASS。

- [ ] **Step 7: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/app_controller.hpp src/app_controller.cpp test/test_ui_models.cpp test/test_session_manager.cpp
git commit -m "feat(tags): AppController.toggleTag dispatch + history write-back + clear on stop

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: QML 接线（面板置灰/经 controller、多 chip、在线行勾选 chip）

**Files:**
- Modify: `qml/components/RecordingTagsPanel.qml`, `qml/components/RecordingSessionsPanel.qml`, `qml/Main.qml`
- Test: `test/test_qml_structure.cpp`, `test/test_qml_smoke.cpp`

- [ ] **Step 1: 结构测试（先红）—— test_qml_structure.cpp**

在 `test/test_qml_structure.cpp` 末尾新增结构断言（仿文件内既有 `expect_contains`/`qml_dir()` 风格）：
```cpp
TEST(QmlStructure, TagPanelEditsFollowSelectedSource)
{
  const std::string tags_panel = read_text(qml_dir() / "components" / "RecordingTagsPanel.qml");
  const std::string sessions_panel = read_text(qml_dir() / "components" / "RecordingSessionsPanel.qml");
  const std::string main_text = read_text(qml_dir() / "Main.qml");

  // 底部面板经 controller.toggleTag，且按录制/历史态置灰。
  expect_contains(tags_panel, "property var controller");
  expect_contains(tags_panel, "controller.toggleTag(index)");
  expect_contains(tags_panel, "controller.recording || root.controller.historyMode");
  // Main.qml 给底部面板传 controller。
  expect_contains(main_text, "RecordingTagsPanel {");
  expect_contains(main_text, "controller: appController");

  // 会话行用 Repeater 渲染 tags 数组（无标签不画 → 黑块消失）；不再用单 tagName/tagColor。
  expect_contains(sessions_panel, "model: modelData");  // 由 TagChip 绑定 modelData
  expect_contains(sessions_panel, "model.tags");
  expect_not_contains(sessions_panel, "model.tagName");
  expect_not_contains(sessions_panel, "model.tagColor");
  // 「在线数据」行渲染当前勾选标签（录制中实时）。
  expect_contains(sessions_panel, "onlineDataSourceRow");
}
```
> 实现者注意：上面 `expect_contains(sessions_panel, "model: modelData")` 是按"Repeater 用 `model: <tags 数组>`、delegate 内 `TagChip` 取 `modelData.name/color`"写法来断言；若你的实现里 Repeater 的 model 表达式写法不同，请让该断言匹配你实际写入的稳定字符串（核心是：存在 Repeater 遍历 tags、且 TagChip 由数组项取色）。务必保留 `expect_not_contains` 两条以确保旧单标签角色被移除。

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_qml_structure
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_structure --gtest_filter='QmlStructure.TagPanelEditsFollowSelectedSource'
```
Expected: FAIL — QML 尚未改。

- [ ] **Step 3: 改 RecordingTagsPanel.qml（经 controller + 置灰）**

整体替换 `qml/components/RecordingTagsPanel.qml` 为：
```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Panel {
    id: root

    property var model
    property var controller

    title: "记录标签"

    Flickable {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        contentWidth: width
        contentHeight: tagFlow.implicitHeight
        // 未录制且非历史时整组置灰不可编辑（标签跟随选中的数据源）。
        enabled: !!root.controller && (root.controller.recording || root.controller.historyMode)
        opacity: enabled ? 1.0 : 0.45

        Flow {
            id: tagFlow

            width: parent.width
            spacing: 4

            Repeater {
                model: root.model

                delegate: Button {
                    id: tagButton

                    text: model.name
                    implicitHeight: 18
                    padding: 0
                    leftPadding: 0
                    rightPadding: 0
                    checkable: true
                    checked: model.isSelected
                    Accessible.name: model.name

                    background: Rectangle {
                        color: "transparent"
                    }

                    contentItem: TagChip {
                        label: model.name
                        chipColor: model.color
                        maxTextWidth: 72
                    }

                    onClicked: {
                        if (root.controller && root.controller.toggleTag) {
                            root.controller.toggleTag(index)
                        }
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 4: 改 Main.qml（给底部面板传 controller）**

在 `qml/Main.qml` 的 `RecordingTagsPanel { ... }` 块里（约 76-79 行），`model: appController.tagModel` 旁补一行：
```qml
                        RecordingTagsPanel {
```
块内加：
```qml
                            controller: appController
```
（保持其它现有属性不动；最终该块同时有 `model: appController.tagModel` 与 `controller: appController`。）

- [ ] **Step 5: 改 RecordingSessionsPanel.qml（历史行多 chip + 在线行勾选 chip）**

(a) 历史会话 delegate 里，把单个 `TagChip`（约 138-142 行）：
```qml
                        TagChip {
                            label: model.tagName
                            chipColor: model.tagColor
                            maxTextWidth: 54
                        }
```
替换为遍历 tags 数组的 `Repeater`（无标签则不渲染任何 chip）：
```qml
                        Repeater {
                            model: model.tags
                            delegate: TagChip {
                                label: modelData.name
                                chipColor: modelData.color
                                maxTextWidth: 54
                            }
                        }
```
> 注意 delegate 里外层 `model` 是会话行的 model（有 `.tags`），内层 Repeater 的 `model: model.tags` + `modelData` 取数组项。若 QML 报 `model` 名称遮蔽，可在外层 ColumnLayout 前用 `readonly property var rowTags: model.tags` 并 `Repeater { model: rowTags }`——实现者择稳妥写法，但结构测试断言的 `model.tags` 字符串需对应保留（或同步调整 Step 1 的断言为你采用的稳定字符串）。

(b) 「在线数据」行（`onlineDataSourceRow` 内的 `RowLayout`，约 38-59 行）：在 `Label { text: "在线数据" ... }` 之后、RowLayout 结束前，加一个渲染当前勾选标签的 `Repeater`（录制中实时显示；非录制态 tagModel 无勾选 → 不渲染）：
```qml
                Repeater {
                    model: root.controller ? root.controller.tagModel : null
                    delegate: TagChip {
                        visible: model.isSelected
                        label: model.name
                        chipColor: model.color
                        maxTextWidth: 54
                    }
                }
```
> 说明：直接复用 tagModel，按 `isSelected` 过滤显示。录制中勾选 → chip 出现；停录 `clearSelection()` → 全部 `isSelected=false` → chip 消失。`root.controller.tagModel` 在本面板可用（RecordingSessionsPanel 已有 `controller` 属性，且 AppController 暴露 `topicModel` 同款 CONSTANT 属性——确认 `tagModel` 是 `Q_PROPERTY(... tagModel ... CONSTANT)`，已是）。

- [ ] **Step 6: 构建并运行结构 + 冒烟测试**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder --target test_qml_structure test_qml_smoke
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_structure
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_smoke
```
Expected: 两者全 PASS。若 `test_qml_smoke` 之前有用到 `tagName/tagColor` 角色的断言，改为 `tags`（实现者据失败信息定位并改对应断言；核心契约：会话行经 `tags` 数组）。

- [ ] **Step 7: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/RecordingTagsPanel.qml qml/components/RecordingSessionsPanel.qml qml/Main.qml test/test_qml_structure.cpp test/test_qml_smoke.cpp
git commit -m "feat(tags): tag panel follows selected source; sessions render multi-chip (fix black block)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: 全量构建、全测试与人工确认

**Files:** 无（验证任务）

- [ ] **Step 1: 全量构建 + 强制重建所有目标**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && cmake --build /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder
```
Expected: 构建成功，无错误。

- [ ] **Step 2: 全测试**

```bash
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen colcon test --packages-select data_recorder --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws
colcon test-result --all | tail -5
```
Expected: 全部 PASS，0 failures（含 test_ui_models、test_session_manager、test_qml_structure、test_qml_smoke）。

- [ ] **Step 3: 人工外观/交互确认（需人参与）**

启动应用，确认：
1. 历史会话行无标签时**不再有黑块**；有标签时显示一个/多个 chip。
2. 未录制时底部「记录标签」面板**置灰**不可点。
3. 开始录制 → 面板可点；点多个标签 → 「在线数据」行实时出现这些 chip。
4. 停止录制 → 「在线数据」行 chip 清空；新会话出现在历史列表并带这些标签。
5. 选中某历史会话 → 面板回显其标签；点标签增删 → 立即生效，且**重启应用后仍在**（已写盘）。

（GUI 点击自动化不可靠，故人工核对。）

---

## Self-Review

**Spec 覆盖：**
- 黑块修复（无标签不画 chip）→ Task 2（空 tags 数组）+ Task 4 Step 5（Repeater 遍历，空则不画）。✓
- 多标签 → Task 1（TagListModel 多选）+ Task 2（RecordingSessionModel tags[]）。✓
- 跟随选中源状态机（置灰/录制中/历史）→ Task 3（toggleTag 分派）+ Task 4（enabled 绑定）。✓
- 录制中实时显示在「在线数据」行 → Task 4 Step 5(b)。✓
- 停录随会话写入并清空在线行 → Task 3 Step 4(c)（停录 clearSelection）+ 既有 exportSelectedTags 写入路径。✓
- 历史即时同步写回 session.yaml + 刷新行 → Task 3 Step 4(b)。✓
- 无损往返 → Task 3 Step 1 + Step 5。✓
- 同步写盘 → Task 3 `write_session_yaml` 同步调用。✓
- 测试：多选/导出/setSelected、tags 角色/updateSessionTags、历史写回往返、QML 结构/冒烟、人工。✓

**占位符扫描：** 无 TBD；每个代码步骤含完整前后代码。两处给实现者的 QML 写法说明（Step 5 model 名称遮蔽、Step 1 断言字符串对应）是显式的"二选一并保持断言一致"指引，非占位。✓

**类型/命名一致性：** `TagListModel` 用 `selected_rows_`（`std::set<int>`），`select` toggle、`exportSelectedTags`/`setSelectedTags`/`clearSelection` 全部基于它。`RecordingSessionModel` 用 `TagsRole`/`"tags"`、行字段 `tags`（`QVariantList`）、`updateSessionTags(int, vector<TagRecord>)`——Task 2 定义、Task 3 调用一致。`AppController::toggleTag(int)` Task 3 定义、Task 4 QML 调用一致。旧 `TagNameRole/TagColorRole/tagName/tagColor` 在 Task 2 移除、Task 4 同步删除 QML 消费与结构断言（`expect_not_contains`）。✓

**中间态：** Task 2 之后、Task 4 之前，QML 仍引用旧 `tagName/tagColor` 会导致 `test_qml_smoke`/运行时取空——计划在 Task 2 Step 5 注明"smoke 留到 Task 4"，Task 4 修复 QML 与 smoke 断言。各 Task 的 test_ui_models / test_session_manager 自身保持绿。✓
