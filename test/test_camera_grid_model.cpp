#include <gtest/gtest.h>

#include "data_recorder/camera_grid_model.hpp"
#include "data_recorder/ui_models.hpp"

namespace
{
data_recorder::TopicEntry make_topic(const std::string & name, const std::string & backend,
  data_recorder::TopicUiCategory cat)
{
  data_recorder::TopicEntry t;
  t.topic_name = name;
  t.backend_name = backend;
  t.ui_category = cat;
  return t;
}
}  // namespace

TEST(CameraGridModel, ExposesOnlyVisibleCameras)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/joint_states", "rosbag", data_recorder::TopicUiCategory::NumericTrack),
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/right_camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });

  data_recorder::CameraGridModel model(&source);
  EXPECT_EQ(model.rowCount(), 2);
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::CameraGridModel::TopicNameRole)
      .toString().toStdString(),
    "/camera/image_raw");
}

TEST(CameraGridModel, ReactsToVisibilityToggle)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/right_camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);
  ASSERT_EQ(model.rowCount(), 2);

  source.toggleVisible(0);  // hide /camera/image_raw
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::CameraGridModel::TopicNameRole)
      .toString().toStdString(),
    "/right_camera/image_raw");
}

TEST(CameraGridModel, MoveCameraReorders)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/a/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/b/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/c/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);

  model.moveCamera(0, 2);  // a -> after b,c  => order b,c,a
  EXPECT_EQ(model.data(model.index(0, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/b/image_raw");
  EXPECT_EQ(model.data(model.index(2, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/a/image_raw");
}

TEST(CameraGridModel, HiddenCameraReappearsInOriginalSlot)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/a/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/b/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/c/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);
  ASSERT_EQ(model.rowCount(), 3);

  source.toggleVisible(1);  // hide /b/image_raw (the middle camera)
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(model.data(model.index(0, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/a/image_raw");
  EXPECT_EQ(model.data(model.index(1, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/c/image_raw");

  source.toggleVisible(1);  // show /b/image_raw again
  ASSERT_EQ(model.rowCount(), 3);
  // It returns to its original slot between a and c, not appended at the end.
  EXPECT_EQ(model.data(model.index(1, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/b/image_raw");
}

TEST(CameraGridModel, ExposesTopicKeyAndFrameSeq)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);
  const auto idx = model.index(0, 0);
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::TopicKeyRole).toString().toStdString(),
    "/camera/image_raw");
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::FrameSeqRole).toInt(), 0);

  model.updateFrameSeq("/camera/image_raw", 7);
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::FrameSeqRole).toInt(), 7);
}
