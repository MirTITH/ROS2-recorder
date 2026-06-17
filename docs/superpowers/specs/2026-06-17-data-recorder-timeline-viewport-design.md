# Data Recorder Timeline Viewport Design

Date: 2026-06-17

## Goal

Refactor the Timeline UI around a single viewport state object so the time ruler, curve rows,
playhead, mouse wheel behavior, and bottom range bar all use the same time-window math.

This spec is UI-only. It does not add real ROS topic subscriptions, real recorded data loading,
annotation persistence, or backend recording behavior.

## Problems To Solve

The current Timeline implementation keeps viewport math spread across `TimelinePanel.qml`,
`TimelineCurveRow.qml`, and `TimelineRangeBar.qml`. That causes inconsistent behavior:

- Shift + mouse wheel in the curve area moves the playhead, but it should pan the visible time
  window and ruler.
- When the playhead time is outside the visible window, the red playhead line is clamped to the
  curve area's edge. It should disappear.
- Curve drawing touches the top and bottom row edges. It needs a small vertical plotting padding.
- The bottom range bar is hard to drag. The thumb can jitter because drag deltas are computed from a
  coordinate system that moves with the thumb.
- Ruler ticks are too sparse for review work.

## Scope

In scope:

- Add a `TimelineViewport.qml` state/helper object.
- Move visible-window clamping, time-to-x conversion, x-to-time conversion, panning, zooming, and
  tick generation into `TimelineViewport`.
- Update `TimelinePanel.qml` to consume the viewport object instead of duplicating viewport math.
- Update `TimelineRangeBar.qml` drag math to use stable track coordinates.
- Update `TimelineCurveRow.qml` to add vertical plot padding.
- Hide playhead lines when the playhead is outside the visible time window.
- Make the ruler denser with major and minor ticks.
- Add structure and behavior tests for the new viewport contract.

Out of scope:

- Real numeric topic extraction from ROS messages.
- Real-time streaming performance work.
- Persistent zoom, pan, or range-bar positions.
- Marker creation/deletion changes.
- Redesigning the overall Timeline panel layout.

## TimelineViewport Component

Add `qml/components/TimelineViewport.qml` as a non-visual `QtObject`.

Primary properties:

- `totalDurationSeconds`: full timeline duration.
- `visibleStartSeconds`: start of the visible time window.
- `visibleDurationSeconds`: length of the visible time window.
- `visibleEndSeconds`: derived visible end time.
- `minimumVisibleDurationSeconds`: lower zoom bound, fixed at `0.05` seconds for this iteration.

Core functions:

- `setWindow(startSeconds, durationSeconds)`: clamps and applies the visible window.
- `panBySeconds(deltaSeconds)`: shifts the visible window without changing duration.
- `panByWheel(deltaY)`: pans the visible window from wheel movement.
- `zoomAt(anchorX, widthValue, deltaY)`: zooms around the time under the mouse.
- `timeAtX(xPosition, widthValue)`: converts a curve/ruler x coordinate to seconds.
- `xAtTime(seconds, widthValue)`: converts seconds to x coordinate in the visible window.
- `isTimeVisible(seconds)`: returns whether a time is within the current visible window.
- `majorTickInterval(widthValue)`: chooses dense but readable labeled tick spacing.
- `minorTickInterval(widthValue)`: chooses unlabeled sub-tick spacing.
- `tickTimes(widthValue, interval)`: returns tick times inside the visible window.

The component is the only place that clamps the visible window to total duration.

## Wheel And Seek Behavior

Curve area mouse behavior:

- Left press and drag still seeks the playhead using `viewport.timeAtX()`.
- Plain wheel zooms the visible window using `viewport.zoomAt()`.
- Shift + wheel pans the visible window using `viewport.panByWheel()`.
- Shift + wheel does not call `setPlayheadSeconds()` and does not move the playhead.

Direction:

- Wheel up / away from the user pans toward earlier time.
- Wheel down / toward the user pans toward later time.

## Playhead Rendering

Both ruler and curve playhead lines use the same rule:

- If `viewport.isTimeVisible(playheadSeconds)` is false, the playhead line is hidden.
- If visible, x is computed by `viewport.xAtTime(playheadSeconds, width)`.
- The playhead is not clamped to the left or right edge.

This prevents a misleading edge-pinned red line when the current playhead is outside the displayed
time window.

## Curve Row Drawing

`TimelineCurveRow.qml` remains a Canvas-based prototype renderer.

Changes:

- Add `plotTopPadding` and `plotBottomPadding`, both `4` px for this iteration.
- Draw horizontal grid lines inside the padded plot area.
- Map numeric values into the padded plot area instead of the full row height.
- Keep x coordinates flush with the ruler: no left or right curve padding.
- Keep row dividers outside the plotting math.

The renderer may continue using all points to compute `minY` and `maxY` for now. Per-visible-window
Y autoscaling is not part of this change.

## Ruler Tick Density

The ruler uses major and minor ticks.

Major ticks:

- Have labels.
- Use human-readable intervals from a fixed set such as 1 ms, 2 ms, 5 ms, 10 ms, 20 ms, 50 ms,
  100 ms, 200 ms, 500 ms, 1 s, 2 s, 5 s, 10 s, 30 s, 1 min, 2 min, 5 min.
- Target `64` px between major ticks.
- Preserve label readability by using major ticks for labels only.

Minor ticks:

- Do not have labels.
- Use one fifth of the major interval when that keeps minor ticks at least `10` px apart; otherwise
  use one half of the major interval.
- Are shorter and lighter than major ticks.
- Increase perceived density without forcing text overlap.

Label formatting stays scale-aware:

- Sub-second views show milliseconds.
- Second-level views show seconds.
- Longer views show minutes and seconds.

## TimelineRangeBar

`TimelineRangeBar.qml` still represents the visible window within the total duration and still
allows panning and resizing the visible window.

Drag math changes:

- On press, map the mouse position into the stable `track` coordinate system.
- Store `pressTrackX`, `pressStart`, and `pressDuration`.
- On drag, map the current mouse position into `track` again and compute
  `deltaSeconds = ((currentTrackX - pressTrackX) / track.width) * totalDurationSeconds`.
- Do this for thumb drag, left resize handle, and right resize handle.

Reason:

- The current local `mouse.x` belongs to the moving thumb or handle, so the coordinate frame changes
  during a drag.
- Using stable track coordinates removes jitter and makes thumb movement match mouse movement.

## Testing

Add or update QML structure tests and QML smoke tests for the viewport interaction contract.

Expected coverage:

- `TimelineViewport.qml` exists.
- `TimelinePanel.qml` instantiates `TimelineViewport`.
- Shift + wheel in `TimelinePanel.qml` calls viewport panning, not playhead nudging.
- Playhead rendering includes an `isTimeVisible` visibility condition.
- `TimelineCurveRow.qml` defines vertical plot padding and uses it in y-coordinate mapping.
- `TimelineRangeBar.qml` uses `mapToItem(track, ...)` for drag and resize calculations.
- Ruler code includes major and minor tick generation.

The existing package verification remains:

- `qmllint -I qml/components qml/Main.qml qml/components/*.qml`
- `colcon build --packages-select data_recorder`
- `colcon test --packages-select data_recorder`

## Acceptance Criteria

- Shift + wheel over the curve area pans the visible time window and ruler.
- Shift + wheel does not change the playhead time.
- The playhead line disappears when the playhead is outside the visible window.
- Numeric curves have visible top and bottom breathing room while remaining horizontally aligned
  with the ruler.
- The range bar thumb and resize handles drag smoothly without jitter.
- The range bar movement matches mouse movement proportionally.
- The ruler appears denser than before, using labeled major ticks and unlabeled minor ticks.
