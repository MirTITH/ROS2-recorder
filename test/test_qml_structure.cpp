#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::filesystem::path qml_dir()
{
  return std::filesystem::path(DATA_RECORDER_QML_DIR);
}

std::string read_text(const std::filesystem::path & path)
{
  std::ifstream input(path);
  if (!input.is_open()) {
    ADD_FAILURE() << "Failed to open " << path;
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::size_t count_token(const std::string & text, const std::string & token)
{
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = text.find(token, position)) != std::string::npos) {
    ++count;
    position += token.size();
  }
  return count;
}

void expect_contains(const std::string & text, const std::string & token)
{
  EXPECT_NE(text.find(token), std::string::npos) << token;
}

void expect_not_contains(const std::string & text, const std::string & token)
{
  EXPECT_EQ(text.find(token), std::string::npos) << token;
}

std::string qml_block(const std::string & text, const std::string & block_start)
{
  const std::size_t start = text.find(block_start);
  if (start == std::string::npos) {
    ADD_FAILURE() << block_start;
    return {};
  }

  int brace_depth = 0;
  bool entered_block = false;
  for (std::size_t position = start; position < text.size(); ++position) {
    if (text[position] == '{') {
      ++brace_depth;
      entered_block = true;
    } else if (text[position] == '}') {
      --brace_depth;
      if (entered_block && brace_depth == 0) {
        return text.substr(start, position - start + 1);
      }
    }
  }

  ADD_FAILURE() << "Unclosed QML block: " << block_start;
  return {};
}

}  // namespace

TEST(QmlStructure, EverySplitViewUsesCustomResizeHandle)
{
  const std::filesystem::path main_path = qml_dir() / "Main.qml";
  const std::filesystem::path timeline_path = qml_dir() / "components" / "TimelinePanel.qml";

  const std::string main_text = read_text(main_path);
  const std::string timeline_text = read_text(timeline_path);

  EXPECT_EQ(count_token(main_text, "SplitView {"), count_token(main_text, "handle: ResizeHandle"));
  EXPECT_EQ(
    count_token(timeline_text, "SplitView {"), count_token(timeline_text, "handle: ResizeHandle"));
  expect_contains(read_text(qml_dir() / "components" / "ResizeHandle.qml"), "property int lineOrientation");
}

TEST(QmlStructure, PanelChromeIsSquareCornered)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "Panel.qml");

  EXPECT_EQ(count_token(panel_text, "radius:"), 1U);
  expect_contains(panel_text, "radius: 0");
}

TEST(QmlStructure, MainLayoutUsesFlushLeftWorkspace)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");

  expect_not_contains(main_text, "padding: 8");
  expect_not_contains(main_text, "leftPadding:");
  expect_not_contains(main_text, "rightPadding:");
  expect_not_contains(main_text, "topPadding:");
  expect_not_contains(main_text, "bottomPadding:");
}

TEST(QmlStructure, AppChromeUsesStatusBarForRecording)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string status_text = read_text(qml_dir() / "components" / "StatusBar.qml");

  expect_not_contains(main_text, "AppHeader");
  EXPECT_FALSE(std::filesystem::exists(qml_dir() / "components" / "AppHeader.qml"));
  expect_contains(status_text, "implicitHeight: 32");
  expect_contains(status_text, "readonly property bool isRecording");
  expect_contains(status_text, "readonly property string statusText");
  expect_contains(status_text, "text: root.statusText");
  expect_contains(status_text, "text: \"磁盘 --\"");
  expect_not_contains(status_text, "outputDirectory");
  expect_not_contains(status_text, "保存目录");
}

TEST(QmlStructure, EventMarkersRenderAsTimelineTracks)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string track_info_text = read_text(qml_dir() / "components" / "TrackInfoColumn.qml");
  const std::string lane_text = read_text(qml_dir() / "components" / "TrackLaneColumn.qml");

  expect_not_contains(main_text, "EventMarkersPanel");
  EXPECT_FALSE(std::filesystem::exists(qml_dir() / "components" / "EventMarkersPanel.qml"));
  expect_contains(main_text, "eventMarkerModel: appController.eventMarkerModel");
  expect_contains(panel_text, "property var eventMarkerModel");
  expect_contains(track_info_text, "model: root.eventMarkerModel");
  expect_contains(track_info_text, "EventTrackInfoRow {");
  expect_contains(track_info_text, "objectName: \"primaryActionButton\"");
  expect_contains(track_info_text, "togglePlayback()");
  expect_contains(track_info_text, "toggleRecording()");
  expect_contains(lane_text, "EventTrackRow {");

  // Within the right-hand lane column the event marker tracks render above the
  // topic tracks. Scope the ordering check to TrackLaneColumn.qml, which now
  // owns the lane Repeaters. The event repeater binds "model: eventMarkerModel"
  // and the topic repeater binds "model: root.model".
  const std::string lane_column = qml_block(lane_text, "Column {");
  const auto event_position = lane_column.find("model: eventMarkerModel");
  const auto topic_position = lane_column.find("model: root.model");
  ASSERT_NE(event_position, std::string::npos);
  ASSERT_NE(topic_position, std::string::npos);
  EXPECT_LT(event_position, topic_position);

  const std::filesystem::path info_row_path = qml_dir() / "components" / "EventTrackInfoRow.qml";
  const std::filesystem::path track_row_path = qml_dir() / "components" / "EventTrackRow.qml";
  EXPECT_TRUE(std::filesystem::exists(info_row_path));
  EXPECT_TRUE(std::filesystem::exists(track_row_path));

  const std::string info_text = read_text(info_row_path);
  expect_contains(info_text, "property string kind");
  expect_contains(info_text, "id: pointTypeIndicator");
  expect_contains(info_text, "id: rangeTypeIndicator");
  expect_contains(info_text, "visible: root.kind === \"point\"");
  expect_contains(info_text, "visible: root.kind === \"range\"");
  expect_contains(info_text, "radius: width / 2");
  expect_contains(info_text, "objectName: \"eventMarkerActionButton_\" + root.shortcut");
  expect_contains(info_text, "root.eventName + \"（共 \" + root.count + \" 个）\"");
  expect_contains(info_text, "signal actionRequested()");

  const std::string track_text = read_text(track_row_path);
  expect_contains(lane_text, "eventName: model.name");
  expect_contains(lane_text, "kind: model.kind");
  expect_contains(track_text, "property var viewport");
  expect_contains(track_text, "property var markerModel");
  expect_contains(track_text, "property string eventName");
  expect_contains(track_text, "id: pendingRangePreview");
  expect_contains(track_text, "rotation: 45");
  expect_contains(track_text, "id: leftResizeHandle");
  expect_contains(track_text, "id: rightResizeHandle");
  expect_contains(track_text, "id: leftRangeBorder");
  expect_contains(track_text, "id: rightRangeBorder");
  expect_contains(track_text, "opacity: 0.68");
  EXPECT_GE(count_token(track_text, "cursorShape: Qt.PointingHandCursor"), 2U);
  EXPECT_GE(count_token(track_text, "cursorShape: Qt.SizeHorCursor"), 2U);
  expect_contains(track_text, "function startDrag(mode, instanceId, startSeconds, endSeconds, rootX)");
  expect_contains(track_text, "function updateDrag(rootX)");
  expect_contains(track_text, "function finishDrag()");
  expect_contains(track_text, "activeDragCurrentX");
  expect_contains(track_text, "root.markerModel.movePoint");
  expect_contains(track_text, "root.markerModel.moveRange");
  expect_contains(track_text, "root.markerModel.deleteInstance");
  expect_contains(track_text, "function requestDelete(instanceId, localX, localY)");
  expect_contains(track_text, "deleteMenu.popup(root, localX, localY)");
  expect_contains(track_text, "function requestDeleteAll(localX, localY)");
  expect_contains(track_text, "deleteAllMenu.popup(root, localX, localY)");
  expect_contains(track_text, "MenuItem");
  expect_contains(track_text, "text: \"删除\"");
  expect_contains(track_text, "text: \"删除所有“\" + root.eventName + \"”\"");
  expect_contains(track_text, "root.markerModel.deleteAllInstances(root.rowIndex)");
  expect_not_contains(track_text, "width: 3\n                    height: 14\n                    color: \"#ffffff\"");
  expect_contains(track_info_text, "height: eventInfoRepeater.count > 0 ? 1 : 0");
  expect_contains(lane_text, "height: eventLaneRepeater.count > 0 ? 1 : 0");
}

TEST(QmlStructure, RecordingTagsPanelCanCollapseToTitleBar)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");

  expect_contains(main_text, "RecordingTagsPanel {");
  expect_contains(main_text, "SplitView.minimumHeight: 20");
}

TEST(QmlStructure, RecordingSessionsPanelActsAsDataSourceSelector)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string sessions_block = qml_block(main_text, "RecordingSessionsPanel {");
  const std::string panel_text = read_text(qml_dir() / "components" / "RecordingSessionsPanel.qml");

  expect_contains(sessions_block, "SplitView.minimumHeight: 120");
  expect_contains(sessions_block, "controller: appController");
  expect_contains(panel_text, "title: \"数据\"");
  expect_contains(panel_text, "property var controller");
  expect_contains(panel_text, "objectName: \"onlineDataSourceRow\"");
  expect_contains(panel_text, "objectName: \"historyDataSourceRow_\" + index");
  expect_contains(panel_text, "text: \"在线数据\"");
  expect_contains(panel_text, "root.controller.selectOnlineData()");
  expect_contains(panel_text, "root.controller.selectHistorySession(index)");
  expect_contains(panel_text, "root.controller.selectedSessionRow === index");
  expect_contains(panel_text, "root.controller.historyMode");
  expect_contains(panel_text, "height: 32");
  expect_contains(panel_text, "color: selected ? Theme.rowSelected");
  expect_not_contains(panel_text, "当前 ROS topics");
}

TEST(QmlStructure, CameraPreviewTileIsSquareCornered)
{
  const std::string tile_text = read_text(qml_dir() / "components" / "CameraPreviewTile.qml");

  expect_not_contains(tile_text, "radius:");
}

TEST(QmlStructure, CameraGridUsesExplicitLayoutAndDragPreview)
{
  // The camera grid is split into three files: CameraGridPanel.qml (the view that
  // binds the C++ cameraGridModel and renders the cells / placeholder / floating
  // preview), CameraGridLayout.qml (pure geometry + push-aside reflow), and
  // CameraDragController.qml (drag state machine + drop-commit via moveCamera).
  const std::string grid_text = read_text(qml_dir() / "components" / "CameraGridPanel.qml");
  const std::string layout_text = read_text(qml_dir() / "components" / "CameraGridLayout.qml");
  const std::string drag_text = read_text(qml_dir() / "components" / "CameraDragController.qml");

  // View: no item-view widget, an explicit Repeater, and the drag-preview chrome.
  expect_not_contains(grid_text, "GridView {");
  expect_contains(grid_text, "Repeater {");
  expect_contains(grid_text, "id: dropPlaceholder");
  expect_contains(grid_text, "id: floatingPreview");
  expect_contains(grid_text, "gridLayout.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)");

  // Layout: geometry helpers, the placeholder identity, and the reflow sequence.
  expect_contains(layout_text, "readonly property string placeholderSourceKey");
  expect_contains(layout_text, "function chooseLayout");
  expect_contains(layout_text, "function layoutForIndex");
  expect_contains(layout_text, "function cameraAspectRatio");
  expect_contains(layout_text, "function previewSequence");
  expect_contains(layout_text, "function previewLayoutForKey");
  expect_contains(layout_text, "function placeholderLayout");
  expect_contains(layout_text, "function floatingPreviewLayout");
  expect_contains(layout_text, "sourceKey: gridLayout.placeholderSourceKey");
  expect_contains(layout_text, "gridLayout.previewSequence()");

  // Drag controller: the drop-insert math and the model-backed reorder commit.
  expect_contains(drag_text, "function updateDropInsertIndex");
  expect_contains(drag_text, "function commitDropInsertIndex");
  expect_contains(drag_text, "moveCamera");
}

TEST(QmlStructure, TimelineInfoUsesEyeSvgAndOmitsTrackKindText)
{
  const std::string row_text = read_text(qml_dir() / "components" / "TimelineInfoRow.qml");

  expect_not_contains(row_text, "root.trackKind");
  expect_not_contains(row_text, "property string trackKind");
  expect_contains(row_text, "../assets/icons/eye.svg");
  expect_contains(row_text, "../assets/icons/eye-off.svg");
  expect_contains(row_text, "id: cameraVisibilityButton");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "assets" / "icons" / "eye.svg"));
  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "assets" / "icons" / "eye-off.svg"));
}

TEST(QmlStructure, TimelineTrackAreaHasAdaptiveWindowAndRangeBar)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string lane_text = read_text(qml_dir() / "components" / "TrackLaneColumn.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineTrackRow.qml");

  expect_contains(panel_text, "property real visibleStartSeconds");
  expect_contains(panel_text, "property real visibleDurationSeconds");
  expect_contains(lane_text, "function formatTickLabel");
  expect_contains(lane_text, "TimelineRangeBar {");
  expect_contains(lane_text, "wheel.modifiers & Qt.ShiftModifier");
  expect_not_contains(panel_text, "Math.floor(root.effectiveDurationSeconds / 5) + 1");

  expect_not_contains(curve_text, "ChartView");
  expect_contains(curve_text, "Canvas {");
  expect_contains(curve_text, "property real visibleStartSeconds");
  expect_contains(curve_text, "property real visibleDurationSeconds");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "TimelineRangeBar.qml"));
}

TEST(QmlStructure, TimelineUsesViewportObjectForWindowMath)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string viewport_text = read_text(qml_dir() / "components" / "TimelineViewport.qml");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "TimelineViewport.qml"));
  expect_contains(panel_text, "TimelineViewport {");
  expect_contains(panel_text, "id: viewport");
  expect_not_contains(panel_text, "function nudgePlayhead");
  expect_not_contains(panel_text, "root.nudgePlayhead");
  expect_contains(viewport_text, "function panByWheel");
  expect_contains(viewport_text, "function zoomAt");
  expect_contains(viewport_text, "function timeAtX");
  expect_contains(viewport_text, "function xAtTime");
  expect_contains(viewport_text, "function isTimeVisible");
}

TEST(QmlStructure, TimelineViewportRenderingRulesAreExplicit)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string lane_text = read_text(qml_dir() / "components" / "TrackLaneColumn.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineTrackRow.qml");
  const std::string viewport_text = read_text(qml_dir() / "components" / "TimelineViewport.qml");
  const std::string range_text = read_text(qml_dir() / "components" / "TimelineRangeBar.qml");

  expect_contains(lane_text, "viewport.isTimeVisible(playheadSeconds)");
  expect_contains(lane_text, "rulerTickTimes");
  expect_contains(lane_text, "property int rulerLabelTickStride: 10");
  expect_contains(lane_text, "index % rulerLabelTickStride === 0");
  expect_contains(lane_text, "function formatTickLabel");
  expect_contains(lane_text, "totalMinutes");
  expect_contains(lane_text, "padStart(2, \"0\")");
  expect_contains(lane_text, "padStart(3, \"0\")");

  expect_contains(viewport_text, "function denseTickInterval");
  expect_contains(viewport_text, "var targetPixels = 10");

  expect_contains(curve_text, "property real plotTopPadding: 4");
  expect_contains(curve_text, "property real plotBottomPadding: 4");
  expect_contains(curve_text, "property real sampleMarkerSpacingThreshold: 12");
  expect_contains(curve_text, "function collectDrawablePoints");
  expect_contains(curve_text, "function interpolateBoundaryPoint");
  expect_contains(curve_text, "function collectVisibleSamples");
  expect_contains(curve_text, "function shouldDrawSampleMarkers");
  expect_contains(curve_text, "function drawSampleMarkers");
  expect_contains(curve_text, "boundary: true");
  expect_contains(curve_text, "boundary: false");
  expect_contains(curve_text, "ctx.arc");

  expect_contains(range_text, "id: thumbBody");
  expect_contains(range_text, "color: Theme.tickStrong");
  expect_contains(range_text, "id: leftHandleGrip");
  expect_contains(range_text, "id: rightHandleGrip");
  expect_contains(range_text, "mapToItem(track");
  expect_contains(range_text, "pressTrackX");
}
