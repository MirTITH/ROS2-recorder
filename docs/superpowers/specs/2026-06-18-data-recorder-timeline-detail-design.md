# Data Recorder Timeline Detail Design

Date: 2026-06-18

## Goal

Refine the Timeline UI so high-zoom numeric review feels continuous and precise:

- Curves remain visually complete when the visible window cuts through sparse samples.
- Sparse samples show point markers.
- The time ruler uses dense ticks without repeated-looking labels.
- The bottom time range bar visually matches the editing-style scrollbar reference.

This spec is UI-only. It does not add real ROS subscriptions, backend recording behavior, marker
persistence, or a new charting library.

## Current Problems

`TimelineCurveRow.qml` currently skips any point outside the visible window before drawing. At high
zoom, the visible window often starts or ends between two samples. When that happens, the line
segment crossing the window boundary is dropped, so the curve can look broken or incomplete.

The current curve renderer also draws only strokes. When the average spacing between visible samples
is large, users cannot easily see where actual data samples are.

The ruler currently labels too often for high-zoom views. Several adjacent labels can appear to
communicate the same time because the label format changes with zoom level and may not include
milliseconds.

The bottom `TimelineRangeBar.qml` works functionally, but its visual style still looks like a generic
light scrollbar rather than the more explicit visible-window control shown in
`docs/reference/Premiere Pro Timeline.png`.

## Scope

In scope:

- Keep the existing Canvas-based numeric curve renderer.
- Add viewport-boundary line completion in `TimelineCurveRow.qml`.
- Draw sample point markers when visible samples are sparse.
- Change ruler label cadence to one label every 10 ticks.
- Format ruler labels as `minutes:seconds.milliseconds`.
- Restyle `TimelineRangeBar.qml` to resemble the Premiere timeline scrollbar reference while staying
  compatible with the existing light UI.
- Add or update tests for the new QML structure and formatting contracts.

Out of scope:

- Replacing Canvas with `ChartView` or another charting library.
- Backend-side decimation, interpolation, or numeric message extraction.
- Per-topic curve styling controls.
- Persistent timeline zoom or scroll state.
- Changing marker behavior or playback controls.

## Curve Boundary Completion

`TimelineCurveRow.qml` will keep using the series data supplied by the existing model. For each
series, the renderer will build a draw list for the current visible window:

1. Iterate through points in time order.
2. Keep valid numeric samples whose `x` value is inside `[visibleStartSeconds, visibleEndSeconds]`.
3. Track the nearest valid sample before the visible start.
4. Track the nearest valid sample after the visible end.
5. If the before sample and the first inside/right sample span `visibleStartSeconds`, add an
   interpolated boundary point at the left edge.
6. If the last inside/left sample and the after sample span `visibleEndSeconds`, add an interpolated
   boundary point at the right edge.

The interpolation is linear and is used only for drawing the clipped curve boundary. It does not
create or mutate data samples in the model.

If fewer than two drawable points remain after boundary completion, the renderer will not draw a
line segment. It may still draw a point marker for any real sample inside the visible window.

The curve remains horizontally flush with the ruler: x coordinates still map `visibleStartSeconds` to
`0` and `visibleEndSeconds` to the full curve width. Existing top and bottom plot padding stay in
place.

## Sparse Sample Markers

Point markers appear only when the visible data is sparse enough to benefit from them.

Rule:

- For each series, collect real samples inside the visible window.
- Compute average x spacing in pixels between adjacent visible samples.
- Draw point markers when the average spacing is at least `12` px.
- If exactly one real sample is visible, draw its marker.

Marker style:

- Radius: `2` px.
- Fill: white.
- Stroke: the series color.
- Stroke width: `1` px.

Boundary interpolation points are not marked as samples. Only real data samples receive markers.

## Ruler Ticks And Labels

The ruler should remain dense, but labels should be less noisy.

Tick behavior:

- Use a single dense tick sequence from `TimelineViewport.tickTimes(...)`.
- Draw a visible tick for every tick in the sequence.
- Draw a text label on every 10th tick by visible tick index.
- The labeled tick can be slightly taller or darker than unlabeled ticks.

Label format:

- Always use `minutes:seconds.milliseconds`.
- Seconds are zero-padded to two digits.
- Milliseconds are zero-padded to three digits.
- Minutes are total elapsed minutes and do not wrap at one hour.

Examples:

- `35.2` seconds -> `0:35.200`
- `9254.015` seconds -> `154:14.015`

This removes repeated-looking labels in high-zoom views and keeps the format stable across zoom
levels.

## Timeline Range Bar Style

`TimelineRangeBar.qml` remains the visible-window control for the full timeline duration. Its drag
and resize behavior stays based on stable track coordinates.

Visual direction:

- Use a low-profile full-width track near the bottom of the Timeline panel.
- Track color: light neutral gray, with a subtle border.
- Thumb color: blue-gray `#9aa8ba`, clearly darker than the track.
- Thumb spans the visible time window.
- Left and right resize handles are visually distinct from the thumb body.
- Handles use darker vertical grip blocks, each with a thin inner highlight line.
- Keep square or very small-radius geometry so the control fits the existing technical UI.

The style should communicate three separate actions:

- Drag the thumb body to pan the visible window.
- Drag the left handle to move the visible start.
- Drag the right handle to move the visible end.

## Testing

Add or update structure tests for:

- `TimelineCurveRow.qml` contains explicit boundary-completion logic.
- Sparse point marker behavior is represented by a marker-spacing threshold.
- Ruler label cadence uses a 10-tick stride.
- Ruler label formatting uses minutes, zero-padded seconds, and zero-padded milliseconds.
- `TimelineRangeBar.qml` includes distinct thumb body and resize handle styling.

Keep existing verification:

- `qmllint -I qml/components qml/Main.qml qml/components/*.qml`
- `colcon build --packages-select data_recorder`
- `colcon test --packages-select data_recorder`

## Acceptance Criteria

- At high zoom, a curve remains visually continuous when a line segment crosses the visible window
  boundary.
- Sparse visible samples are shown as point markers.
- Interpolated boundary points are not displayed as real sample markers.
- Ruler labels appear every 10 ticks, not every 5 ticks.
- Ruler labels use `minutes:seconds.milliseconds`, including long durations such as
  `154:14.015`.
- The bottom time range bar looks closer to the Premiere timeline reference and clearly exposes
  thumb dragging plus left/right resizing.
- Existing timeline panning, zooming, seeking, and playhead visibility behavior continue to work.
