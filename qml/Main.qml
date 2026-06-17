import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

ApplicationWindow {
    id: window

    width: 1480
    height: 930
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "DataRecorder"
    color: "#e9edf3"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            Layout.fillWidth: true
            controller: appController
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            padding: 8

            Item {
                SplitView.preferredHeight: 284
                SplitView.minimumHeight: 180

                ScrollView {
                    id: cameraScroll

                    anchors.fill: parent
                    clip: true
                    contentWidth: cameraRow.implicitWidth
                    contentHeight: availableHeight

                    RowLayout {
                        id: cameraRow

                        height: cameraScroll.availableHeight
                        spacing: 8

                        Repeater {
                            model: appController.cameraModel

                            delegate: CameraPreviewPanel {
                                Layout.preferredWidth: 470
                                Layout.minimumWidth: 320
                                Layout.fillHeight: true
                                topicName: model.topicName
                                backendName: model.backendName
                                frequencyText: model.frequencyText
                                seriesColor: model.seriesColor
                                visibleState: model.isVisible
                            }
                        }
                    }
                }
            }

            SplitView {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 320
                orientation: Qt.Horizontal

                SplitView {
                    SplitView.preferredWidth: 268
                    SplitView.minimumWidth: 220
                    SplitView.maximumWidth: 390
                    orientation: Qt.Vertical

                    RecordingSessionsPanel {
                        SplitView.preferredHeight: 284
                        SplitView.minimumHeight: 160
                        model: appController.recordingSessionModel
                    }

                    RecordingTagsPanel {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 120
                        model: appController.tagModel
                    }
                }

                SplitView {
                    SplitView.fillWidth: true
                    orientation: Qt.Vertical

                    EventMarkersPanel {
                        SplitView.preferredHeight: 82
                        SplitView.minimumHeight: 70
                        SplitView.maximumHeight: 112
                        model: appController.eventMarkerModel
                    }

                    SplitView {
                        SplitView.fillHeight: true
                        orientation: Qt.Horizontal

                        TopicListPanel {
                            SplitView.preferredWidth: 320
                            SplitView.minimumWidth: 240
                            SplitView.maximumWidth: 440
                            model: appController.topicModel
                        }

                        TimelinePanel {
                            SplitView.fillWidth: true
                            controller: appController
                            model: appController.trackModel
                        }
                    }
                }
            }
        }

        StatusBar {
            Layout.fillWidth: true
            controller: appController
        }
    }
}
