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

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Vertical
                handle: ResizeHandle { lineOrientation: Qt.Horizontal }

                CameraGridPanel {
                    model: appController.topicModel
                    visibleCameraCount: appController.visibleCameraCount
                }

                SplitView {
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 320
                    orientation: Qt.Horizontal
                    handle: ResizeHandle { lineOrientation: Qt.Vertical }

                    SplitView {
                        SplitView.preferredWidth: 268
                        SplitView.minimumWidth: 100
                        SplitView.maximumWidth: 390
                        orientation: Qt.Vertical
                        handle: ResizeHandle { lineOrientation: Qt.Horizontal }

                        RecordingSessionsPanel {
                            // SplitView.preferredHeight: 
                            SplitView.fillHeight: true
                            SplitView.minimumHeight: 160
                            model: appController.recordingSessionModel
                        }

                        RecordingTagsPanel {
                            SplitView.preferredHeight: 65
                            SplitView.minimumHeight: 20
                            model: appController.tagModel
                        }
                    }

                    TimelinePanel {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        controller: appController
                        model: appController.topicModel
                        eventMarkerModel: appController.eventMarkerModel
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
