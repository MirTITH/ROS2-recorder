# Data Recorder Event Marker Tracks Design

Date: 2026-06-18

## Goal

Move event markers from the standalone event marker panel into the Timeline as their own compact
track group. Each event definition becomes one timeline row. The left information pane shows the
event name, completed instance count, and add action. The right timeline area shows point and range
event instances aligned to the same ruler, viewport, playhead, zoom, and horizontal range bar used
by topic tracks.

This spec is UI-only. It adds in-memory marker instances for the prototype, but it does not add
recording backend persistence, rosbag metadata writing, or YAML hot reload behavior.

## Current Problems

The current `EventMarkersPanel` is a separate horizontal button strip above the Timeline. It only
selects an event marker definition. It does not show where events occurred, does not support
multiple instances, and does not use the Timeline viewport.

That separation makes event annotations feel detached from the time-based data they describe. It
also duplicates timeline-like controls: users add events with one UI region, but must inspect time
in another.

## Chosen Approach

Use event marker tracks as an independent row group inside `TimelinePanel`.

The Timeline will contain two vertical groups:

- Event marker tracks at the top.
- Topic tracks below the event tracks.

Both groups share the same time ruler, curve area, visible time window, playhead, row scrolling, and
bottom range bar. Event tracks are not represented as fake topics. They use their own model roles and
QML row components so event-specific state does not leak into `TopicListModel`.

The standalone `EventMarkersPanel` is removed from the main layout.

## Timeline Layout

The Timeline keeps its current split layout:

- Left: `TimelineInfoPane`.
- Right: ruler, timeline content area, and range bar.

Within the scrollable row area, event rows appear before topic rows.

Event rows:

- Height is one compact row: `32` px.
- A subtle group divider separates the event rows from topic rows.
- Row height is fixed for all event tracks.
- Event rows scroll vertically together with topic rows.

The time ruler remains above the right-side timeline content only. It does not extend over the left
information pane.

## Event Track Information Row

Each event information row contains:

- A small color swatch using the event marker color.
- The event name and completed count, left-aligned.
- A compact action button, right-aligned.

Display text format:

- Point event: `拿起水杯（共 0 个）`
- Range event: `倒水（共 1 个）`

Action button text:

- Point event: `添加 (1)`
- Range event without pending start: `添加起点 (2)`
- Range event with pending start: `设置终点 (2)`

Counts include completed instances only:

- A point event counts immediately after it is added.
- A range event counts only after both start and end are set.
- A pending range start does not increase the count.

## Event Track Timeline Row

The right-side event row renders marker instances instead of numeric curves.

Point event instances:

- Render as an `8` px diamond centered on the event time.
- Use the event color.
- Keep a minimum `16` px hit target so the marker can be selected and dragged when zoomed out.

Range event instances:

- Render as a horizontal segment from start time to end time.
- Use the event color with a subtle border.
- Preserve a minimum visible height inside the one-row track.
- Render visible left and right resize handles.
- Each edge handle uses a `3` px visual width and at least a `10` px horizontal hit target.

Pending range start:

- Render as a temporary, semi-transparent segment from the pending start to the current playhead
  time.
- If the playhead is before the pending start, render the temporary segment between the two times
  and keep the start visually identifiable.
- The pending segment uses the event color at lower opacity.

## Add And Shortcut Behavior

The row action button and keyboard shortcut perform the same action at the current playhead time.

Point event:

- Click `添加 (shortcut)` or press the shortcut.
- Add a point instance at `playheadSeconds`.
- Increment the completed count.

Range event:

- First click or shortcut press stores a pending start at `playheadSeconds`.
- The button changes to `设置终点 (shortcut)`.
- Second click or shortcut press completes the range with the current `playheadSeconds`.
- If the end time is earlier than the start time, store the range with normalized `start <= end`.
- Clear pending state and increment the completed count.

The previous behavior where shortcuts only select an event marker definition is removed.

## Editing Interactions

Event instances are editable directly in the timeline content area.

Point drag:

- Dragging a point moves it horizontally.
- The new time is computed with the shared `TimelineViewport.timeAtX()` conversion.
- The time is clamped to the total timeline range.

Range drag:

- Dragging the body of a completed range moves both start and end by the same delta.
- Duration is preserved.
- The range is clamped to the total timeline range.

Range resize:

- Dragging the left edge changes only the start time.
- Dragging the right edge changes only the end time.
- If a resize crosses the opposite edge, normalize the stored range so `start <= end`.
- Resize uses the same time conversion and clamping as point drag.

Context menu:

- Right-clicking a point or range opens a small context menu.
- The initial menu contains `删除`.
- Choosing `删除` removes that instance and updates the completed count.

## Data Model

Extend event marker state beyond definitions.

Each event marker row should expose:

- `shortcut`
- `name`
- `kind` (`point` or `range`)
- `color`
- `count`
- `actionText`
- `hasPendingRangeStart`
- `pendingStartSeconds`
- `instances`

Each instance should expose enough data for QML rendering:

- Stable instance id.
- Kind.
- Start time.
- End time for ranges.
- Color.

Required model/controller operations:

- Add point at time.
- Toggle range start/end at time.
- Move point instance.
- Move range instance.
- Resize range start.
- Resize range end.
- Delete instance.
- Trigger shortcut at playhead time.

Implementation may extend `EventMarkerModel` directly for the prototype. It should not overload
`TopicListModel` with event-specific roles.

## Component Boundaries

Expected QML component split:

- `TimelinePanel.qml`: owns viewport, ruler, playhead, range bar, and combined row layout.
- `EventTrackInfoRow.qml`: renders the left event information row and action button.
- `EventTrackRow.qml`: renders point/range instances and handles drag/resize/context menu.
- Existing `TimelineInfoRow.qml` and `TimelineCurveRow.qml`: continue handling topic tracks.

This keeps event editing isolated from numeric curve rendering.

## Visual Style

Event tracks should feel related to topic tracks, but distinct:

- Use the same row grid and divider language as topic tracks.
- Use a lighter event-row background or a subtle left color swatch to signal annotation rows.
- Keep buttons compact and right-aligned.
- Avoid large cards, rounded panels, or a second toolbar-like event marker strip.
- Use event color for the marker glyphs, not for the whole row background.

The result should read like a video editor timeline annotation lane, not a separate form.

## Out Of Scope

- Persisting event marker instances to disk.
- Writing marker metadata into rosbag or video outputs.
- Importing marker instances from prior recordings.
- Undo/redo.
- Multi-select or bulk delete.
- Keyboard nudging of selected markers.
- Snapping markers to samples or ticks.
- Editing event names, colors, shortcuts, or kinds in the UI.
- Showing real recorded event data from backend storage.

## Testing

Add or update tests for the UI structure and event marker model behavior.

Expected coverage:

- `Main.qml` no longer instantiates `EventMarkersPanel`.
- `TimelinePanel.qml` accepts both `topicModel` and `eventMarkerModel`.
- Event marker rows are rendered before topic rows.
- Event info rows expose action text for point and range markers.
- Range action text changes from `添加起点` to `设置终点` while a pending start exists.
- Shortcut triggering adds a marker at the current playhead time instead of only selecting a marker.
- Completed counts update after point add, range completion, and deletion.
- Point drag, range body drag, and range edge resize operations exist in the QML/C++ contract.
- Right-click context menu includes `删除`.

Expected verification:

- `qmllint -I qml/components qml/Main.qml qml/components/*.qml`
- `colcon build --packages-select data_recorder`
- `colcon test --packages-select data_recorder`

## Acceptance Criteria

- The standalone event marker panel is gone.
- Each configured event marker appears as one compact event track in the Timeline.
- Event tracks appear above topic tracks.
- Event row text is left-aligned and action buttons are right-aligned.
- Point events can be added at the playhead with either the row button or shortcut.
- Range events can add a start and then set an end with the same button or shortcut.
- Pending range rows visibly show their pending state and provisional segment.
- Completed point and range counts are displayed correctly.
- Point markers can be dragged horizontally.
- Range markers can be dragged as a whole.
- Range marker left and right edges can be resized independently.
- Point and range markers can be deleted from a right-click menu.
- Event markers share the existing ruler, zoom, pan, playhead visibility, and range bar behavior.
