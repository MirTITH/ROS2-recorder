#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>

#include <vector>

#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

class CameraGridModel : public QAbstractListModel
{
  Q_OBJECT
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    ResolutionTextRole,
    SeriesColorRole,
    TopicKeyRole,
    FrameSeqRole,
  };

  explicit CameraGridModel(TopicListModel * source, QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void moveCamera(int from, int to);
  Q_INVOKABLE void updateFrameSeq(const QString & topic_key, int seq);

signals:
  void countChanged();

private:
  struct Camera
  {
    QString topic_name;
    QString backend_name;
    QString resolution_text;
    QString series_color;
    int frame_seq{0};
  };

  QString key_of(const QString & topic, const QString & backend) const;
  void rebuild();

  TopicListModel * source_;
  std::vector<QString> order_;    // remembered full-camera order (keys)
  std::vector<Camera> visible_;   // current visible rows, filtered by order_
};

}  // namespace data_recorder
