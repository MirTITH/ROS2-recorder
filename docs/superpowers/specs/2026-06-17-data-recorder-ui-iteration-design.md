# Data Recorder UI Iteration Design

Date: 2026-06-17

## Goal

This iteration improves the existing Qt/QML UI prototype so it feels closer to a practical data
recording tool. The scope is visual layout and basic interaction only. It does not add real ROS
subscriptions, rosbag/video recording, metadata parsing, output directory scanning, or annotation
persistence.

The output directory structure described in the feedback document is reference material for UI
shape, especially the recording session list. This iteration continues to use simulated session
data.

## Inputs

- Human feedback:
  `docs/superpowers/feedbacks/2026-06-16-data-recorder-ui-prototype-feedback.md`
- Existing UI prototype:
  `qml/Main.qml` and `qml/components/*.qml`
- Example config:
  `config/example_config.yaml`
- Timeline visual reference:
  `docs/reference/Premiere Pro Timeline.png`

## Scope

In scope:

- Rework panel spacing, title bars, and splitter handles.
- Remove the standalone Topic List panel and move topic information into the Timeline.
- Replace the camera preview strip with an auto-fit camera grid.
- Add UI-only camera preview ordering by drag and drop.
- Add UI-only camera visibility toggles from the Timeline information pane.
- Improve recording session and tag chip styling.
- Add Space key handling for Record/Stop.
- Rework the Timeline into an information pane plus curve area.
- Add live playhead dragging behavior in the ruler and curve area.
- Add UI-only scroll and zoom behavior in the Timeline.
- Add visual distinction and keyboard triggering for event marker buttons.
- Update docs and install rules for the moved `docs/` and `config/` directories.

Out of scope:

- Real backend recording.
- ROS topic subscriptions.
- Runtime topic discovery.
- Real image rendering.
- Reading session folders or metadata from disk.
- Writing annotation files.
- Full point/range marker creation and deletion state machine.
- Persisting camera order, layout sizes, zoom level, or hidden camera state.
- Browser-based visual companion or web UI.

## Main Layout

The main window keeps the current overall structure but becomes denser and more tool-like:

- `AppHeader` remains at the top and owns app name, config path, status, and Record/Stop.
- The central workspace is a vertical `SplitView`.
- The upper split item is the Camera Preview Area. It is hidden when there are no visible camera
  topics.
- The lower split item is a horizontal workspace split.
- The left workspace column contains Recording Sessions and Recording Tags.
- The right workspace column contains Event Markers and Timeline.
- The standalone Topic List panel is removed.

All major panels use a common panel chrome:

- Title bar height is 20 px at 1x DPI.
- Panel padding is reduced.
- Splitter handles render as 1 px gray lines.
- Hovered splitter handles become blue, visually thicker, and use the appropriate resize cursor.
- Splitter positions do not persist across launches.

## Camera Preview Area

The Camera Preview Area uses an auto-fit grid with no scroll bars.

Behavior:

- All visible camera previews must fit inside the available panel area.
- Preview content must not be cropped. If a preview aspect ratio differs from the cell ratio, the UI
  may show letterboxing.
- With one camera, the preview fills the camera area.
- With two or three cameras, the grid prefers a single row when the area is wide enough.
- With four cameras, the grid prefers a 2x2 layout.
- More cameras increase rows and columns as needed.
- If no camera topic is visible, the Camera Preview Area collapses.

Preview card:

- The bottom overlay is removed.
- The title bar is compact.
- The left side shows the topic name.
- The right side shows a simulated resolution, such as `1280x720`.
- FPS and backend information are moved to the Timeline information pane.
- Gaps between previews are small, about 4 px.

Ordering:

- Users can drag previews to reorder them in the current UI.
- The order is UI-only and is not written to config or disk.
- Restarting the app restores the config order.

## Recording Sessions And Tags

Recording Sessions continue to use simulated data, shaped like real session folders.

Each session row:

- The primary title is the folder name, such as `2026-05-31_07-46-20` or a user-edited name.
- Long folder names are elided.
- Tooltip shows full folder name, full duration, and disk usage.
- Disk usage is not visible in the row body.
- The second line shows short duration at left, such as `24s` or `154m35s`.
- The second line shows the session tag chip at right.
- Rows are separated by 1 px horizontal dividers.
- Rows do not have individual card borders or extra spacing.

Tag chips:

- A shared `TagChip` QML component is used by Recording Sessions and Recording Tags.
- Chips have no border.
- Chips use rounded colored backgrounds.
- Text color is automatically chosen as black or white for contrast.
- Long labels collapse to a small colored dot with a tooltip.
- Recording Tags remain UI-only selection controls and do not write annotations.

## Timeline

The Timeline replaces the old Topic List plus chart-only track list.

Structure:

- The Timeline has a left information pane and a right curve area.
- The information pane width is adjustable with a splitter.
- The information pane has a minimum width so topic names and controls remain usable.
- The right curve area receives the remaining width.
- Each configured topic appears as one row.
- Rows in the information pane and curve area are vertically synchronized.

Header:

- The upper-left Timeline header belongs to the information pane.
- It shows the current playhead time, such as `00:00:24.120`.
- It may show compact mode state such as `录制中`, `查看`, or `实时`.
- The time ruler exists only above the curve area.
- The ruler does not extend over the information pane.
- The status bar does not show playhead time or zoom ratio.

Topic rows:

- The information pane shows topic name, frequency, backend, and track type.
- Camera topic rows include an eye button for showing or hiding the corresponding camera preview.
- Numeric topic rows show one or more simulated curves in the curve area.
- Empty topic rows remain visible but do not draw curves.
- `/joint_states` is treated as numeric and displays multiple simulated curves.
- `/tf` and `/tf_static` are treated as empty and do not draw curves.
- Camera topic rows do not draw curves.

Curve area:

- Chart margins are minimized so curves align tightly with row bounds.
- Track rows share a common horizontal time scale.
- The playhead is a vertical line through the ruler and track area.
- The playhead is not drawn through the information pane.

Interactions:

- Pressing or dragging in the ruler or curve area moves the playhead continuously.
- The playhead updates while the mouse moves, not only on release.
- Wheel scrolling in the information pane scrolls tracks vertically.
- Wheel scrolling in the curve area zooms the horizontal time scale.
- Zoom is a UI-only state and is not displayed as a numeric status value.

## Recording And Review Modes

The UI distinguishes recording mode from review mode, without connecting to a real recorder.

Recording mode:

- The Record button switches to Stop.
- Space toggles Record/Stop when focus is not in a text input.
- A simulated live edge advances while recording.
- By default, the playhead follows the live edge.
- Dragging the playhead temporarily detaches it from the live edge.
- A compact `回到实时` affordance lets the user return to the live edge.

Review mode:

- When not recording, the playhead is controlled by the user.
- The playhead does not auto-advance.
- Session selection remains simulated in this iteration.

## Event Markers

This iteration changes event marker visuals and trigger plumbing only. It does not implement real
annotation creation, deletion, or persistence.

Visuals:

- Point marker buttons use a dot or short vertical-line visual cue.
- Range marker buttons use a short bar or paired-boundary visual cue.
- Buttons show shortcut and name, such as `1 拿起水杯`.
- Selected or recently triggered marker type has a clear highlight state.

Keyboard:

- Pressing an event shortcut triggers the same UI action as clicking the corresponding button.
- Letter shortcuts are case-insensitive.
- Main keyboard digits and numpad digits map to the same shortcut.
- Space is reserved for Record/Stop.
- Shortcuts do not trigger while focus is inside a text input.

Future marker logic constraint:

- Full marker creation/deletion must not rely on exact equality between playhead time and marker
  time.
- Internal marker time should use high precision, preferably integer nanoseconds.
- UI hit testing should use a pixel tolerance converted to a time range.
- Point deletion should find the nearest same-type marker within the tolerance window.
- Range deletion should hit-test whether the playhead is inside the interval or near a boundary.
- If a range endpoint is set before its start, the stored start and end must be swapped.

## Data And Model Boundaries

The C++ layer remains minimal but gains UI-facing state required by the new layout.

Topic model:

- The topic model feeds the Timeline and contains all configured topics.
- It exposes a track kind role: `camera`, `numeric`, or `empty`.
- It exposes camera visibility state for camera rows.
- It exposes simulated series only for numeric rows.
- It exposes multiple simulated series for `/joint_states`.
- It exposes no series for `/tf`, `/tf_static`, and camera rows.

Camera preview model:

- The Camera Preview Area derives its visible rows from camera topics whose visibility is enabled.
- UI-only drag ordering is handled in QML or a small UI proxy model.
- The ordering does not modify parsed config data.

Recording session model:

- Simulated session rows expose `folderName`, `shortDuration`, `fullDuration`, `sizeText`,
  `tagName`, and `tagColor`.
- The model does not scan the output directory in this iteration.

App controller:

- Exposes playhead time.
- Exposes simulated live edge time.
- Exposes recording/review mode state.
- Exposes whether the playhead is following the live edge.
- Handles Record/Stop toggling.
- Handles event marker shortcut selection as UI-only state.

## Documentation And Install Rules

The package has moved documentation and config into package-local directories:

- `docs/`
- `config/`

The implementation should:

- Install `docs/` into the package share directory.
- Install `config/` into the package share directory.
- Update README commands to use
  `/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml`
  for source-tree examples.
- Keep package documentation under `src/data_recorder/docs`.
- Update terminology with:
  - `TimelineInfoPane`
  - `CurveArea`
  - `CameraGrid`
  - `TagChip`
  - `LiveEdge`
  - `TrackKind`

## Testing And Verification

Automated C++ tests:

- All configured topics appear in the Timeline topic model.
- Topic classification returns camera, numeric, and empty rows correctly.
- `/joint_states` has multiple simulated series.
- `/tf` and `/tf_static` have no simulated series.
- Camera visibility toggles update the visible camera count.
- Recording toggle updates recording mode and live-edge-following state.
- Playhead dragging can detach from live edge and return to live edge.
- Event marker shortcut triggering updates selected marker UI state.

QML static checks:

- `qmllint` covers `qml/Main.qml` and all QML components.

Manual UI checks:

- Splitter handles are 1 px gray by default and turn blue with resize cursors on hover.
- Panel title bars are visually 20 px at 1x DPI.
- Camera preview grid has no scroll bars and does not crop preview content.
- Hiding all cameras collapses the Camera Preview Area.
- Dragging camera previews changes visible order for the current run.
- Recording session rows use compact dividers and show tag chips.
- Space toggles Record/Stop.
- Timeline ruler starts above the curve area only.
- Timeline information pane width is adjustable.
- Playhead follows mouse drag continuously in the ruler and curve area.
- Information pane wheel scrolls tracks vertically.
- Curve area wheel zooms horizontally.
- Event marker buttons show distinct point/range styles and respond to shortcuts.

Visual verification uses the actual Qt application window. Browser-based mockups are not used.
If needed, a temporary Qt/QML style preview target may be created during implementation, but it is
not part of the user-facing product.
