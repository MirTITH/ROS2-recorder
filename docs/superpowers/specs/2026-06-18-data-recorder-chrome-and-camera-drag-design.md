# Data Recorder Chrome And Camera Drag Design

Date: 2026-06-18

## Goal

Simplify the main window chrome and improve camera preview drag feedback:

- Remove the top `AppHeader` row.
- Move the recording control and recording status into the bottom status bar, left-aligned.
- Remove the saved output directory text from the status bar.
- During camera preview drag, show the insertion result by shifting other camera tiles out of the
  target slot before the user releases the mouse.

This spec is UI-only. It does not change backend recording behavior, config loading, topic models,
or persistence of camera ordering.

## Current Problems

The current main window has both a top `AppHeader` and a bottom `StatusBar`. The header contains the
record button, status text, app name, ROS badge, and config path. The status bar contains the saved
output directory and a disk placeholder. This makes the top chrome heavier than the rest of the
application and duplicates the idea of a status surface.

The current camera drag interaction hides the dragged tile and shows a blue placeholder rectangle,
but non-dragged camera tiles keep using their original indexes. The target rectangle can appear over
an existing layout slot without making neighboring tiles move, so the user cannot preview the final
inserted order.

## Scope

In scope:

- Remove `AppHeader` usage from `Main.qml`.
- Delete `qml/components/AppHeader.qml`.
- Move the `recordButton` into `StatusBar.qml`.
- Show controller status text in `StatusBar.qml`, immediately to the right of the record button.
- Keep status bar content left-aligned.
- Remove the saved output directory label from `StatusBar.qml`.
- Keep the lightweight disk placeholder on the right side of the status bar.
- Make camera drag layout use a temporary preview order while dragging.
- Keep `visualOrder` unchanged until mouse release commits the drag.
- Keep the floating dragged tile following the mouse.

Out of scope:

- Changing `AppController` recording state behavior.
- Changing keyboard shortcuts.
- Persisting camera preview order across application restarts.
- Adding animations.
- Redesigning the whole status bar or adding new disk metrics.
- Changing camera tile visuals beyond the drag insertion preview.

## Status Bar Layout

`StatusBar.qml` becomes the only application-level chrome strip.

Left side:

- A `Button` with `objectName: "recordButton"`.
- Button text remains `"录制"` when idle and `"停止"` when recording.
- Button enabled state remains tied to whether `controller` exists.
- Button click still calls `controller.toggleRecording()`.
- A status label shows `controller.statusText`, defaulting to `"就绪"` when the controller is missing
  or the text is empty.
- Status label color remains green when idle and red when recording.

Right side:

- Keep `"磁盘 --"` as a compact placeholder.

Removed from UI:

- The saved output directory text.
- The top app name and ROS badge.
- The header config path text.

`StatusBar.qml` uses an `implicitHeight` of `32` px so the record button fits without recreating a
header-like strip.

## Main Window Layout

`Main.qml` removes the `AppHeader { ... }` child from the root `ColumnLayout`.

The main vertical `SplitView` moves directly below the top of the window, and the `StatusBar` remains
the final child at the bottom. The removed header height becomes usable content area.

The application window title can remain `"DataRecorder"` so the app identity is still visible in the
native window frame.

## Camera Drag Preview Reordering

`CameraGridPanel.qml` will separate three concepts:

- `visualOrder`: the committed camera source order.
- `cameraProxy`: the model of currently visible camera objects.
- drag preview order: a transient list used only while dragging.

During a drag:

1. Exclude the dragged camera from the normal tile layout.
2. Insert a placeholder item at the current `dropInsertIndex`.
3. Lay out all non-dragged tiles and the placeholder using this transient preview sequence.
4. Draw the placeholder in the insertion slot.
5. Draw the dragged camera as the floating preview under the mouse.

This makes the tile at the insertion slot and any following tiles shift away before release.

When the drag is released, `commitDropInsertIndex()` applies the same target index to `visualOrder`,
calls `rebuildVisibleCameras()`, and clears drag state. If the drag is canceled, the transient preview
state is discarded and `visualOrder` is unchanged.

## Camera Layout Helpers

The current `layoutForIndex(index)` maps a committed camera index to a geometry. This change adds
helpers that can compute geometry for the transient drag sequence.

Expected helper responsibilities:

- Build a preview sequence from `cameraProxy`, `dragSourceKey`, and `dropInsertIndex`.
- Represent the placeholder with a sentinel key that cannot collide with a real camera source key.
- Compute layout by preview sequence index while dragging.
- Compute layout by committed index when not dragging.
- Keep the existing row-centering behavior for incomplete rows.

The floating preview uses the dragged tile's last committed size. It must not cause non-dragged tiles
to disappear.

## Testing

Add or update QML structure tests for:

- `Main.qml` no longer instantiates `AppHeader`.
- `qml/components/AppHeader.qml` is removed from the source tree.
- `StatusBar.qml` contains `objectName: "recordButton"`.
- `StatusBar.qml` calls `root.controller.toggleRecording()`.
- `StatusBar.qml` contains `statusText`.
- `StatusBar.qml` no longer contains `"保存目录"` or `outputDirectory`.
- `CameraGridPanel.qml` contains explicit transient preview-order helpers.
- Drag placeholder layout is based on the preview sequence instead of the committed `cameraProxy`
  index alone.

Keep existing smoke tests that locate `recordButton` and toggle recording. They should continue to
pass after the button moves from `AppHeader` to `StatusBar`.

Expected verification:

- `qmllint -I qml/components qml/Main.qml qml/components/*.qml`
- `colcon build --packages-select data_recorder`
- `colcon test --packages-select data_recorder`

## Acceptance Criteria

- The top header row is gone.
- The bottom status bar starts with the record button and status text.
- The saved output directory is no longer shown in the status bar.
- The existing record button smoke test still finds `recordButton`.
- Dragging a camera preview shifts other camera tiles to show the insertion result before release.
- Dropping the dragged preview commits the same order shown during drag.
- Canceling a drag leaves the committed camera order unchanged.
