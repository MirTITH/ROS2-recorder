#include "data_recorder/session_player.hpp"

#include <algorithm>
#include <filesystem>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/path_utils.hpp"
#include "data_recorder/video_clip_reader.hpp"

namespace fs = std::filesystem;

namespace data_recorder
{

namespace
{
constexpr int kTickMs = 33;  // ~30fps
}

SessionPlayer::SessionPlayer(LiveBridge * bridge, QObject * parent)
: QObject(parent), bridge_(bridge)
{
  timer_ = new QTimer(this);
  timer_->setInterval(kTickMs);
  connect(timer_, &QTimer::timeout, this, &SessionPlayer::onTick);
}

SessionPlayer::~SessionPlayer() = default;

void SessionPlayer::load(const SessionRecord & session)
{
  timer_->stop();
  playing_ = false;
  clips_.clear();
  playhead_ = 0.0;
  duration_ = session.duration_seconds;

  const fs::path video_dir = fs::path(session.directory) / "video";
  for (const auto & topic : session.topics) {
    if (topic.backend != "video") { continue; }
    const std::string base = file_base_for_topic(topic.name);
    const std::string mp4 = (video_dir / (base + ".mp4")).string();
    const std::string csv = (video_dir / (base + ".csv")).string();
    auto reader = std::make_unique<VideoClipReader>();
    if (!reader->open(mp4, csv)) { continue; }
    duration_ = std::max(duration_, reader->duration_seconds());
    Clip clip;
    clip.topic_key = QString::fromStdString(topic.name);
    clip.reader = std::move(reader);
    clips_.push_back(std::move(clip));
  }

  pushFramesAt(0.0);
  emit playheadAdvanced(playhead_);
  emit playingChanged(playing_);
}

void SessionPlayer::pushFramesAt(double t)
{
  if (!bridge_) { return; }
  for (auto & clip : clips_) {
    if (!clip.reader) { continue; }
    QImage img = clip.reader->frameAtSeconds(t);
    if (img.isNull()) { continue; }
    bridge_->push_playback_frame(clip.topic_key, std::make_shared<QImage>(std::move(img)));
  }
}

void SessionPlayer::play()
{
  if (playing_ || clips_.empty() || duration_ <= 0.0) { return; }
  if (playhead_ >= duration_) {
    playhead_ = 0.0;
    pushFramesAt(playhead_);
    emit playheadAdvanced(playhead_);
  }
  playing_ = true;
  timer_->start();
  emit playingChanged(playing_);
}

void SessionPlayer::pause()
{
  if (!playing_) { return; }
  playing_ = false;
  timer_->stop();
  emit playingChanged(playing_);
}

void SessionPlayer::togglePlay()
{
  if (playing_) { pause(); } else { play(); }
}

void SessionPlayer::seek(double seconds)
{
  playhead_ = std::clamp(seconds, 0.0, duration_ > 0.0 ? duration_ : 0.0);
  pushFramesAt(playhead_);
  emit playheadAdvanced(playhead_);
}

void SessionPlayer::stop()
{
  timer_->stop();
  playing_ = false;
  clips_.clear();
  playhead_ = 0.0;
  duration_ = 0.0;
  emit playheadAdvanced(playhead_);
  emit playingChanged(playing_);
}

void SessionPlayer::onTick()
{
  playhead_ += static_cast<double>(kTickMs) / 1000.0;
  if (playhead_ >= duration_) {
    playhead_ = duration_;
    pushFramesAt(playhead_);
    pause();
    emit playheadAdvanced(playhead_);
    return;
  }
  pushFramesAt(playhead_);
  emit playheadAdvanced(playhead_);
}

}  // namespace data_recorder
