# Data Recorder UI Refinement Design

Date: 2026-06-17

## Goal

This spec refines the current Qt/QML UI prototype after visual inspection and user feedback. The
scope is still UI-only. It does not add real ROS subscriptions, real image rendering, recording
backends, annotation persistence, or output directory scanning.

The main goal is to make the interface behave more like a practical recording and review tool:
camera previews should resize predictably, timeline geometry should align precisely, panel resizing
should feel consistent, and visual chrome should follow one style system.

## Inputs

- Existing UI prototype: `qml/Main.qml` and `qml/components/*.qml`
- Existing terminology document: `docs/ui_terminology.md`
- Previous design spec:
  `docs/superpowers/specs/2026-06-17-data-recorder-ui-iteration-design.md`
- User feedback from visual inspection:
  - Camera preview aspect ratio is not preserved.
  - Camera grid leaves visually abrupt integer-cell gaps.
  - Camera drag reorder only moves by one slot and does not preview the drop result.
  - Curve rows still have large margins and do not align tightly with the ruler.
  - Time ruler ticks should adapt to zoom.
  - Curve area needs an adjustable horizontal range scrollbar.
  - Shift + wheel in the curve area should move the playhead horizontally.
  - Timeline information pane should remove raw `numeric`, `empty`, `camera` text.
  - Camera visibility control should be an SVG eye icon on the second line, right aligned.
  - All resizable panels should use the custom `ResizeHandle`.
  - Top-level panels should share one visual style; camera tiles should be square-cornered.

## Scope

In scope:

- Replace the camera preview grid layout with explicit QML positioning.
- Preserve camera preview aspect ratio with letterboxing or pillarboxing.
- Improve camera drag reorder with a floating dragged preview and drop placeholder.
- Remove curve-row margins so curve content aligns with the time ruler.
- Make time ruler tick spacing adapt to the current zoom.
- Add a UI-only horizontal range scrollbar at the bottom of the curve area.
- Add Shift + wheel playhead movement in the curve area.
- Remove track-kind text from the Timeline information pane.
- Move camera visibility control to the second row as a right-aligned SVG eye icon.
- Use the custom `ResizeHandle` for every `SplitView`.
- Normalize panel chrome and make camera preview tiles square-cornered.
- Update UI terminology only if new names are introduced.

Out of scope:

- Real ROS image display.
- Real topic data plotting.
- Backend recording.
- Persistent camera order.
- Persistent splitter positions.
- Persistent zoom or scroll range.
- Full marker creation/deletion behavior.
- Browser-based visual mockups.

## Visual Style Rules

The UI uses one clear distinction between panels, global chrome, controls, and content.

Top-level panels:

- Use `Panel.qml` as the common outer shell.
- Share the same background, border, title height, and outer corner radius.
- Own the only panel-level rounded corners.
- Do not nest panel-like cards inside panel bodies.

Panel internals:

- Use square-cornered internal rectangles.
- Internal headers, rulers, rows, and content areas should align flush with the panel body.
- Internal elements may use dividers and subtle background changes, but should not look like
  independent panels.

Global chrome:

- `AppHeader` and `StatusBar` remain square-cornered full-width application chrome.
- They are visually distinct from resizable panels.

Controls:

- Buttons, tag chips, and small interactive controls may keep their own control-specific shapes.
- Camera preview tiles are an exception: they become square-cornered content tiles to avoid mixing
  card styling with the camera grid.

## Splitter Handles

All `SplitView` instances use the shared `ResizeHandle` component.

Behavior:

- The handle has a larger invisible hit area than its visible line, so it is easy to grab.
- The normal visible line is 1 px gray.
- On hover, the visible line becomes blue and thicker.
- The cursor changes to the correct horizontal or vertical resize cursor.
- The handle style is identical in the main vertical split, lower horizontal split, left column
  vertical split, right column vertical split, and Timeline information-pane split.

Naming:

- The implementation should avoid ambiguous naming such as `vertical` if it is unclear whether it
  describes the line orientation or the resize direction.
- Preferred API names are `orientation`, `lineOrientation`, or explicit booleans such as
  `isVerticalLine`.

Constraints:

- Existing `SplitView.minimumWidth`, `SplitView.maximumWidth`, `SplitView.minimumHeight`, and
  `SplitView.maximumHeight` still apply.
- If dragging appears blocked because a pane has reached its minimum or maximum size, this should be
  a constraint result, not an inconsistent handle behavior.

## Camera Preview Area

The camera preview area uses a custom layout based on `Repeater` and explicit geometry instead of
`GridView`.

Reason:

- `GridView` works best when every row follows the same fixed cell grid.
- The desired behavior needs centered partial rows, aspect-aware sizing, whole-panel hit testing,
  drag placeholders, and insertion positions from `0..cameraCount`.
- A custom layout gives predictable geometry and avoids fighting `GridView` internals.

Layout:

- Enumerate candidate column counts from `1` to `cameraCount`.
- For each candidate, compute rows, candidate cell size, and preview drawing area.
- Score candidates by useful preview area, wasted space, and aspect-ratio distortion.
- Choose the best candidate for the current panel size.
- Each row is centered independently.
- A partially filled final row should be centered instead of leaving all empty cells on the right.
- Example: with three cameras in a two-column layout, the first row has two previews and the second
  row has one centered preview.

Aspect ratio:

- Parse camera resolution text such as `1920x1080` into an aspect ratio.
- The tile allocation may be wider or taller than the video aspect ratio.
- The simulated video content draws into the largest centered rectangle that preserves aspect ratio.
- The unused space becomes letterbox or pillarbox area.
- The title strip remains outside the aspect-preserved video content area.

Camera tile:

- The tile itself is square-cornered.
- The title strip is compact.
- The topic name is left aligned.
- The resolution text is right aligned.
- There is no bottom overlay.
- The tile background may remain dark, but it should not introduce rounded card styling.

Drag reorder:

- Dragging starts from a camera tile.
- The model order is not changed during the drag.
- The original tile is shown as a muted source placeholder.
- A floating preview follows the mouse.
- A drop placeholder rectangle shows where the tile will be inserted if released.
- Hit testing covers the entire camera preview area, including empty space.
- Insert positions range from `0` through `cameraCount`, so the first tile can be moved directly to
  the end.
- On release, the UI-only order is updated once.
- Restarting the application restores the config order.

## Timeline Information Pane

The Timeline information pane remains adjustable in width.

Header:

- The playhead time is displayed in the upper part of the information pane.
- The status bar does not display playhead time.
- The status bar does not display zoom ratio.

Topic row content:

- Remove raw track-kind text such as `numeric`, `empty`, and `camera`.
- Keep topic name, frequency, and backend information.
- Camera rows show the camera visibility control on the second line.
- The camera visibility control is right aligned.
- The control uses an SVG eye icon.
- The icon should expose a tooltip or accessible name indicating show/hide camera preview.

## Curve Area And Time Ruler

The time ruler belongs only to the curve area.

Alignment:

- The ruler must not extend over the Timeline information pane.
- Curve content starts at exactly the same left x-position as the ruler's zero-time mark.
- Curve content ends at exactly the same right x-position as the ruler's visible end.
- Remove curve-row top, bottom, left, and right chart margins.
- Row dividers may remain, but they must not shift the plotted curve geometry.

Adaptive ruler ticks:

- Tick spacing is derived from visible duration and available pixel width.
- The UI chooses a human-readable interval from a fixed set, such as 1 ms, 2 ms, 5 ms, 10 ms,
  20 ms, 50 ms, 100 ms, 200 ms, 500 ms, 1 s, 2 s, 5 s, 10 s, 30 s, 1 min, 2 min, 5 min.
- The target major tick spacing is roughly 80-140 px.
- Labels change format based on scale:
  - Sub-second views show milliseconds.
  - Second-level views show seconds.
  - Longer views show minutes and seconds.
- Tick generation is based on time values, not fixed pixel positions.

Horizontal range scrollbar:

- Add a bottom horizontal range scrollbar inside the curve area.
- It represents the visible time window within the full recording duration.
- Dragging the scrollbar thumb pans the visible time window.
- Resizing the thumb changes the visible duration, similar to timeline range controls in video
  editing software.
- The scrollbar is UI-only and does not persist across launches.

Mouse and wheel behavior:

- Pressing or dragging in the ruler or curve area moves the playhead to the corresponding time.
- Wheel in the curve area without modifiers zooms the visible time window.
- Shift + wheel in the curve area moves the playhead horizontally.
- Wheel in the Timeline information pane scrolls tracks vertically.
- Vertical scrolling remains synchronized between the information pane and curve rows.

Time precision:

- UI time mapping is continuous and should not depend on a minimum frame or tick unit.
- Internal time calculations should avoid exact equality checks for marker hit testing.
- Future marker hit testing should use a pixel tolerance converted into a time range.

## Testing And Verification

Automated checks:

- Add or update QML smoke/static tests to verify all `SplitView` declarations use `ResizeHandle`.
- Add or update tests for UI-facing model roles only if C++ model roles change.
- Run `qmllint` for `qml/Main.qml` and `qml/components/*.qml`.
- Run package tests with `colcon test --packages-select data_recorder`.
- Build the package with `colcon build --packages-select data_recorder`.

Manual visual verification:

- Launch the Qt app with the example config.
- Resize the main camera preview panel vertically.
- Resize the main workspace, left column, right column, and Timeline information pane.
- Confirm every splitter has the same hover/cursor behavior.
- Confirm camera previews preserve aspect ratio.
- Confirm one, two, and three visible cameras are centered without abrupt right-side empty cells.
- Drag the leftmost camera preview to the rightmost position and confirm the floating preview and
  drop placeholder follow the mouse.
- Confirm the time ruler starts above the curve area, not above the information pane.
- Confirm curves align with the ruler edges.
- Zoom the curve area and confirm ruler ticks change interval.
- Use Shift + wheel in the curve area and confirm the playhead moves horizontally.
- Confirm the Timeline information pane no longer shows raw `numeric`, `empty`, or `camera` text.
- Confirm camera visibility uses a right-aligned SVG eye icon on the second line.

## Non-Goals And Risks

Non-goals:

- This spec does not turn the UI into a complete video editor.
- This spec does not implement real marker storage or deletion.
- This spec does not implement real camera frames or ROS subscriptions.

Risks:

- The custom camera layout and drag interaction may become too large if implemented directly inside
  one QML file. If that happens, split geometry helpers or drag state into focused components.
- QML `Rectangle.clip` clips rectangular bounds, not true rounded masks. The style should avoid
  relying on child clipping to fake rounded corners.
- The horizontal range scrollbar is more complex than a normal `ScrollBar` because the thumb itself
  must be resizable. It may need a custom component.

