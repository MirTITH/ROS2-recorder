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

TEST(QmlStructure, PanelChromeHasOnlyOuterRoundedCorners)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "Panel.qml");

  EXPECT_EQ(count_token(panel_text, "radius:"), 1U);
  expect_contains(panel_text, "radius: 6");
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
  expect_contains(grid_text, "function chooseLayout");
  expect_contains(grid_text, "function layoutForIndex");
  expect_contains(grid_text, "function cameraAspectRatio");
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
  expect_contains(panel_text, "function niceTickInterval");
  expect_contains(panel_text, "function formatTickLabel");
  expect_contains(panel_text, "function nudgePlayhead");
  expect_contains(panel_text, "TimelineRangeBar {");
  expect_contains(panel_text, "wheel.modifiers & Qt.ShiftModifier");
  expect_not_contains(panel_text, "Math.floor(root.effectiveDurationSeconds / 5) + 1");

  expect_not_contains(curve_text, "ChartView");
  expect_contains(curve_text, "Canvas {");
  expect_contains(curve_text, "property real visibleStartSeconds");
  expect_contains(curve_text, "property real visibleDurationSeconds");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "TimelineRangeBar.qml"));
}
