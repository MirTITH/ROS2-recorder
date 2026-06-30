# 标签 bug 修复 + 历史标签右键删除 + 选中态样式

日期: 2026-06-30
范围: `data_recorder` 包

## 背景与问题

用户给某条数据打了 `左手` 标签 → 存入该会话 `session.yaml` 的 `tags:`。随后在
`example_config.yaml` 把词表里那一项改名为 `合格零件`(颜色不变)。结果点击
`合格零件` 时,原来的 `左手` 标签被静默覆盖/抹掉。

### 根因(已用代码与真实数据验证)

标签全程按 `name` 匹配,**没有任何按 `color` 匹配的代码**——用户"按颜色匹配"的猜测
与实现不符。真实机制是「孤儿标签 + 整体替换」:

1. 载入历史会话时 `TagListModel::setSelectedTags`([ui_models.cpp:488-499](../../../src/ui_models.cpp))
   按 `name` 把已存标签匹配到词表行。`左手` 已不在词表 → 成了"孤儿",无任何词表行
   与之对应 → 面板里没有 chip 高亮。
2. 点击 chip 时 `AppController::toggleTag`([app_controller.cpp:533-564](../../../src/app_controller.cpp))
   执行 `record.tags = exportSelectedTags()` ——**整体替换**,而 `exportSelectedTags`
   只能返回"当前词表里被选中的行"。孤儿 `左手` 没有对应行 → 被排除 → 落盘时丢失。

颜色相同纯属巧合:无论孤儿标签是什么颜色,只要其 `name` 不在当前词表,点任意 chip
都会把它抹掉。

真实数据已确认:`recordings/` 下确有一个会话存着 `左手`,而当前词表是 `合格零件`。

## 设计

三项改动,集中在标签这一片区域。

### 1) Bug 修复:保留孤儿标签(用户确认语义)

期望行为:在历史会话上 toggle 词表 chip 时,**不在当前词表里的已存标签原样保留**,
只有词表内标签随勾选增删。配合下面的右键删除,用户可显式清理孤儿标签。

实现:
- 新增 `TagListModel::exportSelectedTagsMerged(const std::vector<TagRecord> & existing) const`:
  返回 `exportSelectedTags()`,再追加 `existing` 中**名字不在词表**的标签(孤儿)。
  词表内标签由勾选状态决定,不会重复。
- `AppController::toggleTag` 历史分支改为
  `record.tags = tag_model_.exportSelectedTagsMerged(previous_tags)`,
  落盘成功后用合并结果刷新数据行模型。

效果:点 `合格零件` → `[合格零件, 左手]`,`左手` 保留。

### 2) 新功能:历史数据标签右键删除

- 新增 `Q_INVOKABLE void AppController::removeSessionTag(int session_row, int tag_index)`:
  - 录制中直接返回(历史行此时禁用)。
  - 边界校验 `session_row` / `tag_index`。
  - 从 `scanned_sessions_[session_row].tags` 删除该项 → `write_session_yaml`
    (失败回滚,保持内存与磁盘一致)→ `updateSessionTags` 刷新数据行 →
    若该行正是当前载入的历史会话,`tag_model_.setSelectedTags(...)` 同步左侧 chip 高亮。
  - 删除按 `tag_index` 定位:数据行 chip 由 `model.tags`(与 `record.tags` 同序)渲染,
    Repeater 的 `index` 即标签下标,可精确定位(含同名/同色重复项)。
- QML([RecordingSessionsPanel.qml](../../../qml/components/RecordingSessionsPanel.qml)):
  历史行标签 `Repeater` 的每个 `TagChip` 加右键 `MouseArea` → 弹 `Menu`「删除」→
  调 `removeSessionTag(sessionIndex, index)`。录制中禁用。
  **完全照搬 [EventTrackRow.qml:319-330](../../../qml/components/EventTrackRow.qml) 现有删除 UX:
  即时删除,无确认弹窗。**

### 3) 增强:记录标签面板的选中态样式(选中实心 / 未选描边)

现状:`RecordingTagsPanel` 的 chip 设了 `checked: model.isSelected` 但**未绑定任何视觉**,
选中/未选长得一样。改为:
- `TagChip` 新增 `property bool selected: true`(默认 `true`,保持现有所有用法为实心):
  - `selected` → 实色填充 `chipColor`,文字按亮度选 `Theme.textPrimary`/`Theme.surface`(现状)。
  - `!selected` → 透明底 + `chipColor` 描边(空心药丸),文字用 `chipColor`;dot-only 时为空心圆点。
- `RecordingTagsPanel` 的 `TagChip` 传 `selected: model.isSelected`。
- 其余 `TagChip` 用法(在线行、历史数据行)不传 `selected` → 默认实心,行为不变。

## 测试

- 单元测试(`test/test_ui_models.cpp`)`TagListModel::exportSelectedTagsMerged`——复现 bug 的
  失败→通过测试:
  - 词表 `[合格零件]`,`existing=[左手]`,选中 `合格零件` → 合并结果含 `左手`(孤儿保留)。
  - 词表内标签不因 `existing` 重复出现。
  - `existing` 为空 / 无孤儿时,结果等于 `exportSelectedTags()`。
- 现有 `setSelectedTags` / `exportSelectedTags` 测试不改,保持绿。
- `removeSessionTag` 与 QML 接线:无 AppController 单测桩,依赖编译通过 +
  headless QML smoke/structure 测试(QML 能加载)+ 必要的 headless 运行验证。

## 影响文件(8)

| 文件 | 改动 |
|---|---|
| `include/data_recorder/ui_models.hpp` | 声明 `exportSelectedTagsMerged` |
| `src/ui_models.cpp` | 实现 `exportSelectedTagsMerged` |
| `include/data_recorder/app_controller.hpp` | 声明 `removeSessionTag` |
| `src/app_controller.cpp` | `toggleTag` 保留孤儿 + `removeSessionTag` |
| `qml/components/TagChip.qml` | `selected` 属性 + 实心/描边渲染 |
| `qml/components/RecordingTagsPanel.qml` | 传 `selected: model.isSelected` |
| `qml/components/RecordingSessionsPanel.qml` | 历史标签右键删除菜单 |
| `test/test_ui_models.cpp` | `exportSelectedTagsMerged` 单元测试 |

## 非目标(YAGNI)

- 不引入标签稳定 ID / 不按颜色匹配身份。
- 菜单不加「删除全部标签」,不加确认弹窗(与现有删除一致)。
- 不改 `example_config.yaml`(用户的工作改动,保持不动)。
