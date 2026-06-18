# Event Marker Track Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish event marker tracks with cursor feedback, duplicate prevention, track-level deletion, type-specific info indicators, and translucent range markers.

**Architecture:** Keep event marker state in `EventMarkerModel`; expose one new invokable for clearing a row and keep duplicate filtering inside existing creation methods. Keep visual behavior inside the existing `EventTrackInfoRow.qml` and `EventTrackRow.qml` components so topic tracks remain untouched.

**Tech Stack:** ROS 2 ament C++, Qt/QML 2.15, Qt Test/GTest structure checks, existing colcon build/test flow.

---

### Task 1: Model Duplicate Filtering And Clear-All

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [x] **Step 1: Write failing model tests**

Add tests that create a model with one point marker and one range marker, verify duplicate point/range creation does not increase `count`, and verify `deleteAllInstances(row)` clears instances plus pending range state.

- [x] **Step 2: Run model tests to verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure -R test_ui_models
```

Expected: fails because `deleteAllInstances` does not exist and duplicate creation is still allowed.

- [x] **Step 3: Implement minimal model changes**

Add `Q_INVOKABLE bool deleteAllInstances(int row);`. Add duplicate checks in `addPoint()` and the completed branch of `toggleRange()` after time normalization. If duplicate exists, emit only the role changes needed for pending range cleanup in the range case.

- [x] **Step 4: Run model tests to verify green**

Run the same `colcon test ... -R test_ui_models` command. Expected: all selected tests pass.

### Task 2: QML Structure And Interaction Polish

**Files:**
- Modify: `qml/components/EventTrackInfoRow.qml`
- Modify: `qml/components/EventTrackRow.qml`
- Test: `test/test_qml_structure.cpp`

- [x] **Step 1: Write failing QML structure tests**

Assert `EventTrackInfoRow.qml` has a `kind` property, point indicator circle, and range indicator bar. Assert `EventTrackRow.qml` has `PointingHandCursor` for point/body mouse areas, keeps `SizeHorCursor` for resize handles, gives `rangeBody` an opacity below `1`, adds a track context menu with `删除所有“`, and calls `deleteAllInstances`.

- [x] **Step 2: Run QML structure test to verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure -R test_qml_structure
```

Expected: fails because the new strings/structure are absent.

- [x] **Step 3: Implement QML polish**

Update `EventTrackInfoRow.qml` to accept `kind`, render a circular point indicator for point rows and the existing vertical bar for range rows. Pass `kind: model.kind` from `TimelinePanel.qml` if not already available in the row.

Update `EventTrackRow.qml`:
- set `cursorShape: Qt.PointingHandCursor` on point and range body mouse areas;
- preserve `Qt.SizeHorCursor` on edge handles;
- set range body opacity to a fixed translucent value;
- add a background mouse area/menu for right-clicking empty track space;
- add menu item text `删除所有“` + event name + `”`;
- call `markerModel.deleteAllInstances(rowIndex)`.

- [x] **Step 4: Run QML structure test to verify green**

Run the same `colcon test ... -R test_qml_structure` command. Expected: selected tests pass.

### Task 3: Lint, Build, Full Package Test, Commit

**Files:**
- Verify all modified files.

- [x] **Step 1: Run qmllint**

Run:

```bash
source ~/.local/ros2_rc && rr && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: no lint errors.

- [x] **Step 2: Build package**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: package build succeeds.

- [x] **Step 3: Run full package tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure
```

Expected: all package tests pass.

- [x] **Step 4: Commit implementation**

Run:

```bash
git status --short
git add include/data_recorder/ui_models.hpp src/ui_models.cpp qml/components/EventTrackInfoRow.qml qml/components/EventTrackRow.qml qml/components/TimelinePanel.qml test/test_ui_models.cpp test/test_qml_structure.cpp docs/superpowers/plans/2026-06-18-event-marker-track-polish.md
git commit -m "Polish event marker track interactions"
```

Expected: commit succeeds and working tree is clean.
