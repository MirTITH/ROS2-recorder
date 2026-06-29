#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QJSValue>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace
{
std::filesystem::path qml_dir() { return std::filesystem::path(DATA_RECORDER_QML_DIR); }

QString read_curve_plot_js()
{
  const auto path = qml_dir() / "components" / "curve_plot.js";
  std::ifstream input(path);
  if (!input.is_open()) {
    ADD_FAILURE() << "Cannot open " << path.string();
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return QString::fromStdString(buffer.str());
}

class CurvePlotJs : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char name[] = "test_curve_plot_js";
      static char * argv[] = {name, nullptr};
      app_ = std::make_unique<QCoreApplication>(argc, argv);
    }
  }
  static void TearDownTestSuite() { app_.reset(); }

  void SetUp() override
  {
    const QJSValue lib = engine_.evaluate(read_curve_plot_js());
    ASSERT_FALSE(lib.isError()) << lib.toString().toStdString();
  }

  QJSValue eval(const QString & snippet) { return engine_.evaluate(snippet); }

  static std::unique_ptr<QCoreApplication> app_;
  QJSEngine engine_;
};
std::unique_ptr<QCoreApplication> CurvePlotJs::app_;
}  // namespace

TEST_F(CurvePlotJs, VisibleIndexRangeFindsInclusiveWindow)
{
  QJSValue r = eval(
    "var xs=[0,1,2,3,4,5,6,7,8,9];"
    "var g=visibleIndexRange(xs,2.5,6.5);"
    "[g.lo,g.hi];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 3);  // first >= 2.5
  EXPECT_EQ(r.property(1).toInt(), 6);  // last <= 6.5
}

TEST_F(CurvePlotJs, VisibleIndexRangeEmptyWhenWindowBetweenPoints)
{
  QJSValue r = eval(
    "var g=visibleIndexRange([0,10],3,7);"
    "[g.lo,g.hi];");
  EXPECT_GT(r.property(0).toInt(), r.property(1).toInt());  // lo > hi => none inside
}

TEST_F(CurvePlotJs, BuildSeriesCacheSkipsHiddenAndKeepsBaselineRange)
{
  QJSValue r = eval(
    "var sl=[{visible:true,color:'#111111',points:[{x:0,y:0.5},{x:1,y:0.25}]},"
    "        {visible:false,color:'#222222',points:[{x:0,y:99}]}];"
    "var c=buildSeriesCache(sl);"
    "[c.series.length,c.minY,c.maxY,c.series[0].xs.length,c.series[0].ys[0]];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 1);             // hidden skipped
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), -1);  // baseline [-1,1] kept (0.25 > -1)
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 1);   // baseline kept (0.5 < 1)
  EXPECT_EQ(r.property(3).toInt(), 2);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 0.5);
}

TEST_F(CurvePlotJs, BuildSeriesCacheExpandsRangeBeyondBaseline)
{
  QJSValue r = eval(
    "var c=buildSeriesCache([{visible:true,points:[{x:0,y:-3},{x:1,y:5}]}]);"
    "[c.minY,c.maxY];");
  EXPECT_DOUBLE_EQ(r.property(0).toNumber(), -3);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 5);
}

TEST_F(CurvePlotJs, DrawablePolylineInterpolatesToWindowEdges)
{
  QJSValue r = eval(
    "var p=drawablePolyline([0,2,4],[0,2,4],1,3);"
    "[p.xs.length,p.xs[0],p.ys[0],p.xs[p.xs.length-1],p.ys[p.ys.length-1]];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 3);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 1);
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 1);
  EXPECT_DOUBLE_EQ(r.property(3).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 3);
}

TEST_F(CurvePlotJs, DrawablePolylineSpansGapWhenNoPointInside)
{
  QJSValue r = eval(
    "var p=drawablePolyline([0,10],[0,10],3,7);"
    "[p.xs.length,p.xs[0],p.ys[0],p.xs[1],p.ys[1]];");
  EXPECT_EQ(r.property(0).toInt(), 2);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(3).toNumber(), 7);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 7);
}

TEST_F(CurvePlotJs, DrawablePolylineHandlesEmptyInput)
{
  QJSValue r = eval(
    "var p=drawablePolyline([],[],0,1);"
    "[p.xs.length,p.ys.length];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 0);
  EXPECT_EQ(r.property(1).toInt(), 0);
}
