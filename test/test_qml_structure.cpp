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
  expect_contains(status_text, "objectName: \"recordButton\"");
  expect_contains(status_text, "root.controller.toggleRecording()");
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

  expect_not_contains(main_text, "EventMarkersPanel");
  EXPECT_FALSE(std::filesystem::exists(qml_dir() / "components" / "EventMarkersPanel.qml"));
  expect_contains(main_text, "eventMarkerModel: appController.eventMarkerModel");
  expect_contains(panel_text, "property var eventMarkerModel");
  expect_contains(panel_text, "model: root.eventMarkerModel");
  expect_contains(panel_text, "EventTrackInfoRow {");
  expect_contains(panel_text, "EventTrackRow {");

  const auto event_position = panel_text.find("model: root.eventMarkerModel");
  const auto topic_position = panel_text.find("model: root.model");
  ASSERT_NE(event_position, std::string::npos);
  ASSERT_NE(topic_position, std::string::npos);
  EXPECT_LT(event_position, topic_position);

  const std::filesystem::path info_row_path = qml_dir() / "components" / "EventTrackInfoRow.qml";
  const std::filesystem::path track_row_path = qml_dir() / "components" / "EventTrackRow.qml";
  EXPECT_TRUE(std::filesystem::exists(info_row_path));
  EXPECT_TRUE(std::filesystem::exists(track_row_path));

  const std::string info_text = read_text(info_row_path);
  expect_contains(info_text, "objectName: \"eventMarkerActionButton_\" + root.shortcut");
  expect_contains(info_text, "root.eventName + \"（共 \" + root.count + \" 个）\"");
  expect_contains(info_text, "signal actionRequested()");

  const std::string track_text = read_text(track_row_path);
  expect_contains(track_text, "property var viewport");
  expect_contains(track_text, "property var markerModel");
  expect_contains(track_text, "id: pendingRangePreview");
  expect_contains(track_text, "rotation: 45");
  expect_contains(track_text, "id: leftResizeHandle");
  expect_contains(track_text, "id: rightResizeHandle");
  expect_contains(track_text, "root.markerModel.movePoint");
  expect_contains(track_text, "root.markerModel.moveRange");
  expect_contains(track_text, "root.markerModel.deleteInstance");
  expect_contains(track_text, "function requestDelete(instanceId, localX, localY)");
  expect_contains(track_text, "deleteMenu.popup(root, localX, localY)");
  expect_contains(track_text, "MenuItem");
  expect_contains(track_text, "text: \"删除\"");
  expect_contains(panel_text, "height: eventInfoRepeater.count > 0 ? 1 : 0");
  expect_contains(panel_text, "height: eventCurveRepeater.count > 0 ? 1 : 0");
}

TEST(QmlStructure, ResizeHandleUsesCompactUnifiedHitArea)
{
  const std::string handle_text = read_text(qml_dir() / "components" / "ResizeHandle.qml");

  expect_contains(handle_text, "implicitWidth: 5");
  expect_contains(handle_text, "implicitHeight: 5");
  expect_not_contains(handle_text, "lineGravity");
}

TEST(QmlStructure, RecordingTagsPanelCanCollapseToTitleBar)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");

  expect_contains(main_text, "RecordingTagsPanel {");
  expect_contains(main_text, "SplitView.minimumHeight: 20");
}

TEST(QmlStructure, CameraPreviewTileIsSquareCornered)
{
  const std::string tile_text = read_text(qml_dir() / "components" / "CameraPreviewTile.qml");

  expect_not_contains(tile_text, "radius:");
}

TEST(QmlStructure, CameraGridUsesExplicitLayoutAndDragPreview)
{
  const std::string grid_text = read_text(qml_dir() / "components" / "CameraGridPanel.qml");

  expect_not_contains(grid_text, "GridView {");
  expect_contains(grid_text, "Repeater {");
  expect_contains(grid_text, "readonly property string placeholderSourceKey");
  expect_contains(grid_text, "function chooseLayout");
  expect_contains(grid_text, "function layoutForIndex");
  expect_contains(grid_text, "function cameraAspectRatio");
  expect_contains(grid_text, "function previewSequence");
  expect_contains(grid_text, "function previewLayoutForKey");
  expect_contains(grid_text, "function placeholderLayout");
  expect_contains(grid_text, "function floatingPreviewLayout");
  expect_contains(grid_text, "root.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)");
  expect_contains(grid_text, "sourceKey: root.placeholderSourceKey");
  expect_contains(grid_text, "root.previewSequence()");
  expect_contains(grid_text, "function updateDropInsertIndex");
  expect_contains(grid_text, "function commitDropInsertIndex");
  expect_contains(grid_text, "id: dropPlaceholder");
  expect_contains(grid_text, "id: floatingPreview");
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

TEST(QmlStructure, TimelineCurveAreaHasAdaptiveWindowAndRangeBar)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineCurveRow.qml");

  expect_contains(panel_text, "property real visibleStartSeconds");
  expect_contains(panel_text, "property real visibleDurationSeconds");
  expect_contains(panel_text, "function formatTickLabel");
  expect_contains(panel_text, "TimelineRangeBar {");
  expect_contains(panel_text, "wheel.modifiers & Qt.ShiftModifier");
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
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineCurveRow.qml");
  const std::string viewport_text = read_text(qml_dir() / "components" / "TimelineViewport.qml");
  const std::string range_text = read_text(qml_dir() / "components" / "TimelineRangeBar.qml");

  expect_contains(panel_text, "viewport.isTimeVisible(root.playheadSeconds)");
  expect_contains(panel_text, "rulerTickTimes");
  expect_contains(panel_text, "property int rulerLabelTickStride: 10");
  expect_contains(panel_text, "index % root.rulerLabelTickStride === 0");
  expect_contains(panel_text, "function formatTickLabel");
  expect_contains(panel_text, "totalMinutes");
  expect_contains(panel_text, "padStart(2, \"0\")");
  expect_contains(panel_text, "padStart(3, \"0\")");

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
  expect_contains(range_text, "color: \"#9aa8ba\"");
  expect_contains(range_text, "id: leftHandleGrip");
  expect_contains(range_text, "id: rightHandleGrip");
  expect_contains(range_text, "mapToItem(track");
  expect_contains(range_text, "pressTrackX");
}
