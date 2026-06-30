# 标签孤儿修复 + 历史标签右键删除 + 选中态样式 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复历史会话「孤儿标签」被静默覆盖的 bug,给历史数据标签加右键删除,并让「记录标签」面板用实心/描边区分选中态。

**Architecture:** 三层最小改动。①C++ 模型层新增 `TagListModel::exportSelectedTagsMerged`,在 toggle 时合并「词表勾选」与「不在词表的孤儿标签」;②`AppController::removeSessionTag` 按下标删除某会话标签并写回;③QML:`TagChip` 加 `selected` 属性渲染描边,`RecordingSessionsPanel` 加右键删除菜单。

**Tech Stack:** C++17 / Qt5 (QtQuick.Controls 2.15) / ROS 2 Humble / colcon / ament_add_gtest。

**约定路径:**
- 工作空间根 `WS = /home/nros/Documents/Woosh/ros2_recorder_ws`
- 包根(git 仓库根)`PKG = $WS/src/data_recorder`
- 所有 `git`/相对路径命令在 `PKG` 下执行;`colcon`/测试二进制在 `WS` 下执行。

**构建单包:**
```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
```
**跑指定 gtest(构建后,二进制直跑,快):**
```bash
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_ui_models" --gtest_filter='<pattern>'
```

---

## Task 1: `TagListModel::exportSelectedTagsMerged`(纯模型层,TDD)

**Files:**
- Modify: `PKG/include/data_recorder/ui_models.hpp`(在 `exportSelectedTags()` 声明后加新方法,约第 124 行)
- Modify: `PKG/src/ui_models.cpp`(在 `exportSelectedTags()` 实现后加,约第 476 行)
- Test: `PKG/test/test_ui_models.cpp`(加在 `TEST(TagListModel, SetSelectedTagsMarksByName)` 之后)

- [ ] **Step 1: 写失败测试**

加在 `test/test_ui_models.cpp` 中 `TEST(TagListModel, SetSelectedTagsMarksByName)` 那个用例的右花括号之后:

```cpp
TEST(TagListModel, ExportSelectedTagsMergedPreservesOrphans)
{
  data_recorder::TagListModel model;
  // 词表已把「左手」改名为「合格零件」(颜色不变);「左手」成了孤儿。
  model.set_tags({{"合格零件", "#1763c9"}, {"成功", "#2f9e44"}});
  model.select(0);  // 勾选「合格零件」

  const std::vector<data_recorder::TagRecord> existing = {{"左手", "#1763c9"}};
  const auto merged = model.exportSelectedTagsMerged(existing);

  // 选中的词表标签在前,孤儿「左手」被保留在后。
  ASSERT_EQ(merged.size(), 2u);
  EXPECT_EQ(merged[0].name, "合格零件");
  EXPECT_EQ(merged[1].name, "左手");
  EXPECT_EQ(merged[1].color, "#1763c9");
}

TEST(TagListModel, ExportSelectedTagsMergedDoesNotDuplicateVocabularyTags)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});
  model.select(0);  // 「成功」选中

  // existing 含一个词表内标签(成功)+一个孤儿(左手)。
  const std::vector<data_recorder::TagRecord> existing = {
    {"成功", "#2f9e44"}, {"左手", "#1763c9"}};
  const auto merged = model.exportSelectedTagsMerged(existing);

  // 「成功」只出现一次(由勾选给出),孤儿「左手」保留。
  ASSERT_EQ(merged.size(), 2u);
  EXPECT_EQ(merged[0].name, "成功");
  EXPECT_EQ(merged[1].name, "左手");
}

TEST(TagListModel, ExportSelectedTagsMergedEmptyExistingEqualsSelected)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});
  model.select(1);
  const auto merged = model.exportSelectedTagsMerged({});
  ASSERT_EQ(merged.size(), 1u);
  EXPECT_EQ(merged[0].name, "失败");
}
```

- [ ] **Step 2: 构建并确认编译失败(红)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
```
Expected: 编译失败,报 `exportSelectedTagsMerged` 未声明/未定义(no member named 'exportSelectedTagsMerged')。

- [ ] **Step 3: 声明方法**

在 `include/data_recorder/ui_models.hpp` 第 124 行 `std::vector<TagRecord> exportSelectedTags() const;` 之后加一行:

```cpp
  // 合并:当前选中的词表标签 + existing 中「名字不在词表」的孤儿标签
  // (改名/删词条后遗留的历史标签不被覆盖抹掉)。
  std::vector<TagRecord> exportSelectedTagsMerged(const std::vector<TagRecord> & existing) const;
```

- [ ] **Step 4: 实现方法**

在 `src/ui_models.cpp` 的 `exportSelectedTags()` 实现(以 `return out;` + `}` 结尾,约第 476 行)之后加:

```cpp
std::vector<TagRecord> TagListModel::exportSelectedTagsMerged(
  const std::vector<TagRecord> & existing) const
{
  std::vector<TagRecord> out = exportSelectedTags();
  for (const auto & t : existing) {
    // 词表内标签由勾选状态决定(已在 out 里);仅追加「不在词表」的孤儿标签。
    bool in_vocabulary = false;
    for (const auto & e : tags_) {
      if (e.name == t.name) { in_vocabulary = true; break; }
    }
    if (!in_vocabulary) { out.push_back(t); }
  }
  return out;
}
```

- [ ] **Step 5: 构建并跑测试(绿)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_ui_models" --gtest_filter='TagListModel.ExportSelectedTagsMerged*'
```
Expected: 3 个用例 PASS。

- [ ] **Step 6: 提交**

```bash
cd "$PKG" && git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(tags): TagListModel::exportSelectedTagsMerged preserves orphan tags

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `toggleTag` 保留孤儿标签(集成测试 + 接线)

依赖 Task 1。`toggleTag` 改用 `exportSelectedTagsMerged(previous_tags)`,使历史会话上点击词表 chip 不再抹掉孤儿标签。

**Files:**
- Modify: `PKG/src/app_controller.cpp`(`toggleTag`,约第 552-563 行)
- Test: `PKG/test/test_ui_models.cpp`(加在 `TEST(AppControllerTags, HistorySessionTagToggleWritesYamlAndUpdatesRow)` 之后)

- [ ] **Step 1: 写失败测试(复现 bug)**

加在 `test/test_ui_models.cpp` 中 `TEST(AppControllerTags, HistorySessionTagToggleWritesYamlAndUpdatesRow)` 用例右花括号之后:

```cpp
TEST(AppControllerTags, HistoryToggleKeepsOrphanTagNotInVocabulary)
{
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "dr_appctrl_orphan_tag";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "2026-06-30_10-00-00";
  fs::create_directories(dir);

  // 会话已存「左手」标签(将不在新词表里 → 孤儿)。
  data_recorder::SessionManager mgr;
  data_recorder::SessionRecord rec;
  rec.session_id = "2026-06-30_10-00-00";
  rec.directory = dir.string();
  rec.duration_seconds = 5.0;
  rec.tags = {{"左手", "#1763c9"}};
  mgr.write_session_yaml(rec);

  data_recorder::ConfigData config;
  config.output_dir = tmp.string();
  // 词表里没有「左手」,只有「合格零件」(同色)与「成功」。
  config.tags = {{"合格零件", "#1763c9"}, {"成功", "#2f9e44"}};
  data_recorder::AppController controller(config, nullptr, nullptr, &mgr);
  ASSERT_GT(controller.recordingSessionModel()->rowCount(), 0);
  controller.selectHistorySession(0);

  // 点「合格零件」(行0)。修复前会把「左手」整体覆盖掉。
  controller.toggleTag(0);

  // 模型行:应是 [合格零件, 左手](孤儿被保留)。
  const auto tags_role = data_recorder::RecordingSessionModel::TagsRole;
  const auto list = controller.recordingSessionModel()
    ->data(controller.recordingSessionModel()->index(0, 0), tags_role).toList();
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(list[0].toMap().value("name").toString().toStdString(), "合格零件");
  EXPECT_EQ(list[1].toMap().value("name").toString().toStdString(), "左手");

  // 落盘验证:重扫磁盘,两个标签都在。
  auto rescanned = mgr.scan(tmp.string());
  ASSERT_EQ(rescanned.size(), 1u);
  ASSERT_EQ(rescanned.front().tags.size(), 2u);
  EXPECT_EQ(rescanned.front().tags[0].name, "合格零件");
  EXPECT_EQ(rescanned.front().tags[1].name, "左手");

  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 构建并跑测试,确认失败(红)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_ui_models" --gtest_filter='AppControllerTags.HistoryToggleKeepsOrphanTagNotInVocabulary'
```
Expected: FAIL —— `list.size()` 为 1(只剩「合格零件」),孤儿「左手」被抹掉。

- [ ] **Step 3: 修改 `toggleTag`**

在 `src/app_controller.cpp` 的 `toggleTag` 里,把这段(约第 552-555 行):

```cpp
  const auto tags = tag_model_.exportSelectedTags();
  auto & record = scanned_sessions_[static_cast<std::size_t>(selected_session_row_)];
  const auto previous_tags = record.tags;
  record.tags = tags;
```

替换为:

```cpp
  auto & record = scanned_sessions_[static_cast<std::size_t>(selected_session_row_)];
  const auto previous_tags = record.tags;
  // 词表勾选 + 保留不在词表里的孤儿标签(改名/删词条后遗留的历史标签不被覆盖抹掉)。
  const auto tags = tag_model_.exportSelectedTagsMerged(previous_tags);
  record.tags = tags;
```

(后续 `record.tags = previous_tags;` 回滚与 `updateSessionTags(selected_session_row_, tags)` 不变 —— 此时 `tags` 已是合并结果。)

- [ ] **Step 4: 构建并跑测试(绿)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_ui_models" --gtest_filter='AppControllerTags.*'
```
Expected: 全部 PASS(含原有 `HistorySessionTagToggleWritesYamlAndUpdatesRow` 不回归)。

- [ ] **Step 5: 提交**

```bash
cd "$PKG" && git add src/app_controller.cpp test/test_ui_models.cpp
git commit -m "fix(tags): toggleTag preserves orphan tags on history sessions

改名/删词条后遗留的历史标签(名字不在当前词表)不再被点击其它 chip 时静默覆盖。

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `AppController::removeSessionTag`(集成测试 + 实现)

**Files:**
- Modify: `PKG/include/data_recorder/app_controller.hpp`(在 `toggleTag` 声明后,约第 96 行)
- Modify: `PKG/src/app_controller.cpp`(在 `toggleTag` 实现后、`eventFilter` 之前,约第 565 行)
- Test: `PKG/test/test_ui_models.cpp`(加在 Task 2 新增用例之后)

- [ ] **Step 1: 写失败测试**

加在 `test/test_ui_models.cpp` 中 Task 2 的 `HistoryToggleKeepsOrphanTagNotInVocabulary` 用例之后:

```cpp
TEST(AppControllerTags, RemoveSessionTagDeletesByIndexAndWritesYaml)
{
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "dr_appctrl_remove_tag";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "2026-06-30_11-00-00";
  fs::create_directories(dir);

  data_recorder::SessionManager mgr;
  data_recorder::SessionRecord rec;
  rec.session_id = "2026-06-30_11-00-00";
  rec.directory = dir.string();
  rec.duration_seconds = 5.0;
  rec.tags = {{"成功", "#2f9e44"}, {"左手", "#1763c9"}};  // 词表内 + 孤儿
  mgr.write_session_yaml(rec);

  data_recorder::ConfigData config;
  config.output_dir = tmp.string();
  config.tags = {{"成功", "#2f9e44"}};
  data_recorder::AppController controller(config, nullptr, nullptr, &mgr);
  ASSERT_GT(controller.recordingSessionModel()->rowCount(), 0);

  // 删除下标 1(「左手」孤儿)——无需先选中该行。
  controller.removeSessionTag(0, 1);

  const auto tags_role = data_recorder::RecordingSessionModel::TagsRole;
  const auto list = controller.recordingSessionModel()
    ->data(controller.recordingSessionModel()->index(0, 0), tags_role).toList();
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].toMap().value("name").toString().toStdString(), "成功");

  // 落盘:重扫只剩「成功」。
  auto rescanned = mgr.scan(tmp.string());
  ASSERT_EQ(rescanned.size(), 1u);
  ASSERT_EQ(rescanned.front().tags.size(), 1u);
  EXPECT_EQ(rescanned.front().tags.front().name, "成功");

  fs::remove_all(tmp);
}

TEST(AppControllerTags, RemoveSessionTagIgnoresOutOfRangeIndices)
{
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "dr_appctrl_remove_tag_oob";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "2026-06-30_12-00-00";
  fs::create_directories(dir);

  data_recorder::SessionManager mgr;
  data_recorder::SessionRecord rec;
  rec.session_id = "2026-06-30_12-00-00";
  rec.directory = dir.string();
  rec.tags = {{"成功", "#2f9e44"}};
  mgr.write_session_yaml(rec);

  data_recorder::ConfigData config;
  config.output_dir = tmp.string();
  config.tags = {{"成功", "#2f9e44"}};
  data_recorder::AppController controller(config, nullptr, nullptr, &mgr);
  ASSERT_GT(controller.recordingSessionModel()->rowCount(), 0);

  controller.removeSessionTag(0, 5);    // tag_index 越界
  controller.removeSessionTag(0, -1);   // 负 tag_index
  controller.removeSessionTag(9, 0);    // session_row 越界

  const auto tags_role = data_recorder::RecordingSessionModel::TagsRole;
  const auto list = controller.recordingSessionModel()
    ->data(controller.recordingSessionModel()->index(0, 0), tags_role).toList();
  EXPECT_EQ(list.size(), 1);  // 不变

  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 构建并确认编译失败(红)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
```
Expected: 编译失败,`removeSessionTag` 未声明(no member named 'removeSessionTag')。

- [ ] **Step 3: 声明方法**

在 `include/data_recorder/app_controller.hpp` 第 96 行 `Q_INVOKABLE void toggleTag(int tag_row);` 之后加:

```cpp
  // 历史数据面板:右键删除某历史会话的指定标签(按下标定位)。写回 session.yaml 并刷新行;
  // 若该行正是当前载入的历史会话,同步左侧「记录标签」面板勾选高亮。录制中为 no-op。
  Q_INVOKABLE void removeSessionTag(int session_row, int tag_index);
```

- [ ] **Step 4: 实现方法**

在 `src/app_controller.cpp` 的 `toggleTag` 实现末尾右花括号(约第 564 行)之后、`bool AppController::eventFilter(...)` 之前,加:

```cpp
void AppController::removeSessionTag(int session_row, int tag_index)
{
  // 录制中历史行禁用;越界守卫。
  if (recording_) { return; }
  if (session_row < 0 || session_row >= static_cast<int>(scanned_sessions_.size())) { return; }
  auto & record = scanned_sessions_[static_cast<std::size_t>(session_row)];
  if (tag_index < 0 || tag_index >= static_cast<int>(record.tags.size())) { return; }

  const auto previous_tags = record.tags;
  record.tags.erase(record.tags.begin() + tag_index);
  // 先落盘,仅写成功才更新内存/模型行(写盘失败则回滚,保持与磁盘一致)。
  const bool written = session_manager_ && session_manager_->write_session_yaml(record);
  if (!written) {
    record.tags = previous_tags;
    return;
  }
  recording_session_model_.updateSessionTags(session_row, record.tags);
  // 若删除的正是当前载入的历史会话,刷新左侧「记录标签」面板勾选高亮。
  if (history_mode_ && session_row == selected_session_row_) {
    tag_model_.setSelectedTags(record.tags);
  }
}
```

- [ ] **Step 5: 构建并跑测试(绿)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_ui_models" --gtest_filter='AppControllerTags.RemoveSessionTag*'
```
Expected: 2 个用例 PASS。

- [ ] **Step 6: 提交**

```bash
cd "$PKG" && git add include/data_recorder/app_controller.hpp src/app_controller.cpp test/test_ui_models.cpp
git commit -m "feat(tags): AppController::removeSessionTag deletes a history session tag by index

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `TagChip` 选中态样式(实心 / 描边)

`TagChip` 加 `selected` 属性(默认 `true`,保持现有所有用法实心)。`selected` → 实色填充;`!selected` → 透明底 + 彩色描边,文字用 `chipColor`。

**Files:**
- Modify: `PKG/qml/components/TagChip.qml`

- [ ] **Step 1: 加 `selected` 属性与描边渲染**

在 `qml/components/TagChip.qml` 中:

(a) 在属性区(`property int maxTextWidth: 72` 之后)加:
```qml
    property bool selected: true
```

(b) 把 `color: chipColor`(约第 20 行)替换为:
```qml
    color: root.selected ? root.chipColor : "transparent"
    border.width: root.selected ? 0 : 1
    border.color: root.chipColor
```

(c) 把 `Label` 的 `color:`(约第 28 行)
```qml
        color: root.luminance > 0.56 ? Theme.textPrimary : Theme.surface
```
替换为:
```qml
        color: root.selected ? (root.luminance > 0.56 ? Theme.textPrimary : Theme.surface)
                             : root.chipColor
```

- [ ] **Step 2: 构建 + QML 冒烟测试(确认无语法/加载错误)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_qml_smoke" --gtest_filter='QmlSmokeTest.LoadsMainWindowAndInteractiveControls'
```
Expected: PASS(Main.qml 含 TagChip 能正常加载)。

- [ ] **Step 3: 提交**

```bash
cd "$PKG" && git add qml/components/TagChip.qml
git commit -m "feat(ui): TagChip selected property renders solid (selected) vs outlined (unselected)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `RecordingTagsPanel` 传 `selected`

让「记录标签」面板按 `isSelected` 显示实心/描边。

**Files:**
- Modify: `PKG/qml/components/RecordingTagsPanel.qml`(contentItem 的 TagChip,约第 49-53 行)

- [ ] **Step 1: 传 `selected: model.isSelected`**

把 `qml/components/RecordingTagsPanel.qml` 的:
```qml
                    contentItem: TagChip {
                        label: model.name
                        chipColor: model.color
                        maxTextWidth: 72
                    }
```
替换为:
```qml
                    contentItem: TagChip {
                        label: model.name
                        chipColor: model.color
                        selected: model.isSelected
                        maxTextWidth: 72
                    }
```

- [ ] **Step 2: 构建 + 冒烟**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_qml_smoke" --gtest_filter='QmlSmokeTest.*'
```
Expected: 全部 PASS。

- [ ] **Step 3: 提交**

```bash
cd "$PKG" && git add qml/components/RecordingTagsPanel.qml
git commit -m "feat(ui): RecordingTagsPanel shows selected/unselected tag chips distinctly

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `RecordingSessionsPanel` 历史标签右键删除菜单

历史行的每个标签 chip 加右键 `MouseArea` → 弹 `Menu`「删除」→ 调 `removeSessionTag(sessionIndex, tagIndex)`。照搬 `EventTrackRow.qml` 的删除 UX(即时,无确认)。

**Files:**
- Modify: `PKG/qml/components/RecordingSessionsPanel.qml`(历史行 delegate,约第 100-178 行)

- [ ] **Step 1: 给行 delegate 加 `sessionIndex` 别名**

在 `qml/components/RecordingSessionsPanel.qml` 历史行 delegate(`ListView` 的 `delegate: Rectangle`)里,`readonly property var rowTags: model.tags`(约第 109 行)之后加一行:
```qml
                readonly property int sessionIndex: index
```

- [ ] **Step 2: 给标签 Repeater 的 chip 加右键 MouseArea**

把(约第 150-157 行):
```qml
                        Repeater {
                            model: rowTags
                            delegate: TagChip {
                                label: modelData.name
                                chipColor: modelData.color
                                maxTextWidth: 54
                            }
                        }
```
替换为:
```qml
                        Repeater {
                            model: rowTags
                            delegate: TagChip {
                                label: modelData.name
                                chipColor: modelData.color
                                maxTextWidth: 54

                                // 右键删除该历史标签;只接收右键,左键透传给行选中 MouseArea。
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    z: 2
                                    enabled: !(root.controller && root.controller.recording)
                                    onPressed: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            tagContextMenu.tagIndex = index
                                            tagContextMenu.popup()
                                            mouse.accepted = true
                                        }
                                    }
                                }
                            }
                        }
```

- [ ] **Step 3: 在行 delegate 内加删除菜单**

在同一行 delegate(`Rectangle`)内部、底部分隔线 `Rectangle { ... height: 1; color: Theme.gridLine }`(约第 171-177 行)之后、该 `Rectangle` 的闭合 `}` 之前,加:
```qml
                Menu {
                    id: tagContextMenu

                    property int tagIndex: -1

                    MenuItem {
                        text: "删除"
                        onTriggered: {
                            if (root.controller && root.controller.removeSessionTag
                                && tagContextMenu.tagIndex >= 0) {
                                root.controller.removeSessionTag(sessionIndex, tagContextMenu.tagIndex)
                            }
                        }
                    }
                }
```

- [ ] **Step 4: 构建 + 冒烟(确认加载无误)**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
QT_QPA_PLATFORM=offscreen "$WS/build/data_recorder/test_qml_smoke" --gtest_filter='QmlSmokeTest.DataSourceRowsSwitchBetweenOnlineAndHistoryState'
```
Expected: PASS(含历史行的面板能加载且历史/在线切换正常)。

注:若右键无反应,多半是行选中 MouseArea 盖住了 chip —— 本步已给 chip 的 MouseArea 设 `z: 2` 提升层级;`EventTrackRow.qml` 用同样的右键 MouseArea 模式,可参照。

- [ ] **Step 5: 提交**

```bash
cd "$PKG" && git add qml/components/RecordingSessionsPanel.qml
git commit -m "feat(ui): right-click context menu to delete a tag on history data rows

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: 全量构建 + 全测试 + headless 验证

**Files:** 无(仅验证)

- [ ] **Step 1: 全量构建**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon build --symlink-install --packages-select data_recorder --base-paths "$WS" --mixin release compile-commands ccache
```
Expected: 构建成功,无错误。

- [ ] **Step 2: 跑该包全部测试**

```bash
cd "$WS" && source ~/.local/ros2_rc && rr && colcon test --packages-select data_recorder --base-paths "$WS"
colcon test-result --verbose --test-result-base "$WS/build/data_recorder"
```
Expected: 全部测试通过(尤其 `test_ui_models`、`test_qml_smoke`、`test_qml_structure`)。

- [ ] **Step 3: headless 启动健全性检查**

```bash
cd "$WS" && source ~/.local/ros2_rc && rs && QT_QPA_PLATFORM=offscreen ros2 run data_recorder data_recorder --ros-args -p config_file:="$PKG/config/example_config.yaml" &
sleep 6 && kill %1 2>/dev/null
```
Expected: 进程能起、能退,日志无 QML 报错(无 "TypeError"、无 "is not a function"、无 `removeSessionTag` 相关警告)。

- [ ] **Step 4: 最终汇总(不提交,留待人工 GUI 验收)**

输出改动总结。提醒人工验收(GUI 合成点击不可靠,见项目记忆):
  1. 载入带孤儿标签(如「左手」)的历史会话,点其它 chip → 孤儿标签仍在(数据行同时显示两个 chip)。
  2. 右键历史数据行的某标签 → 弹「删除」→ 该标签消失且 `session.yaml` 同步更新。
  3. 「记录标签」面板:当前数据已有的标签 = 实心,未有的 = 描边空心。

---

## 自检(写计划者已核对)

- **Spec 覆盖:** ①保留孤儿=Task 1+2;②右键删除=Task 3+6;③选中态样式=Task 4+5;测试=各 Task 内 + Task 7。全覆盖。
- **占位符:** 无 TBD/TODO,每步含实际代码/命令/期望输出。
- **类型/签名一致:** `exportSelectedTagsMerged(const std::vector<TagRecord>&)`、`removeSessionTag(int,int)`、QML `selected`/`sessionIndex`/`tagContextMenu.tagIndex` 在定义与使用处一致。
- **非目标:** 不改 `example_config.yaml`;无确认弹窗;无「删除全部」;不引入标签 ID/不按颜色匹配身份。
