#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

class LiveBridge;
class VideoClipReader;

// 历史会话回放器：每路相机一个 VideoClipReader，按播放头取帧推给 LiveBridge。
// 设计为可同步直接调用（测试）或 moveToThread 后经队列调用（AppController）。
class SessionPlayer : public QObject
{
  Q_OBJECT

public:
  explicit SessionPlayer(LiveBridge * bridge, QObject * parent = nullptr);
  ~SessionPlayer() override;

  double playheadSeconds() const { return playhead_; }
  double durationSeconds() const { return duration_; }
  bool playing() const { return playing_; }

public slots:
  void load(const data_recorder::SessionRecord & session);
  void play();
  void pause();
  void togglePlay();
  void seek(double seconds);
  void stop();

signals:
  void playheadAdvanced(double seconds);
  void playingChanged(bool playing);

private slots:
  void onTick();

private:
  void pushFramesAt(double t);

  LiveBridge * bridge_{nullptr};
  QTimer * timer_{nullptr};

  struct Clip
  {
    QString topic_key;
    std::unique_ptr<VideoClipReader> reader;
  };
  std::vector<Clip> clips_;

  double playhead_{0.0};
  double duration_{0.0};
  bool playing_{false};
};

}  // namespace data_recorder
