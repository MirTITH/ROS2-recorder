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

    onActiveChanged: {
        if (active) {
            windowRoot.forceActiveFocus()
        }
    }

    Timer {
        id: liveEdgeTimer
        interval: 100
        repeat: true
        running: appController.recording
        onTriggered: appController.advanceLiveEdge(appController.liveEdgeSeconds + interval / 1000.0)
    }

    FocusScope {
        id: windowRoot

        objectName: "windowRoot"
        anchors.fill: parent
        focus: true
        activeFocusOnTab: true

        Component.onCompleted: forceActiveFocus()

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

                CameraGridPanel {
                    model: appController.topicModel
                    visibleCameraCount: appController.visibleCameraCount
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

                        TimelinePanel {
                            SplitView.fillHeight: true
                            SplitView.fillWidth: true
                            controller: appController
                            model: appController.topicModel
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
}
