import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 800
    height: 900
    visible: true
    title: "Inspector Test Bench"

    // Status bar at the bottom to show last event
    footer: ToolBar {
        Label {
            id: statusLabel
            objectName: "statusLabel"
            anchors.fill: parent
            anchors.margins: 4
            text: "Ready"
            elide: Text.ElideRight
        }
    }

    function log(msg) {
        statusLabel.text = msg
        console.log("[UI]", msg)
    }

    function keyName(key) {
        const names = {}
        names[Qt.Key_Return]    = "Return"
        names[Qt.Key_Enter]     = "Enter"
        names[Qt.Key_Escape]    = "Escape"
        names[Qt.Key_Tab]       = "Tab"
        names[Qt.Key_Backspace] = "Backspace"
        names[Qt.Key_Delete]    = "Delete"
        names[Qt.Key_Space]     = "Space"
        names[Qt.Key_Left]      = "Left"
        names[Qt.Key_Right]     = "Right"
        names[Qt.Key_Up]        = "Up"
        names[Qt.Key_Down]      = "Down"
        names[Qt.Key_Home]      = "Home"
        names[Qt.Key_End]       = "End"
        names[Qt.Key_PageUp]    = "PageUp"
        names[Qt.Key_PageDown]  = "PageDown"
        names[Qt.Key_F1]  = "F1";  names[Qt.Key_F2]  = "F2"
        names[Qt.Key_F3]  = "F3";  names[Qt.Key_F4]  = "F4"
        names[Qt.Key_F5]  = "F5";  names[Qt.Key_F6]  = "F6"
        names[Qt.Key_F7]  = "F7";  names[Qt.Key_F8]  = "F8"
        names[Qt.Key_F9]  = "F9";  names[Qt.Key_F10] = "F10"
        names[Qt.Key_F11] = "F11"; names[Qt.Key_F12] = "F12"
        return names[key] || ""
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            width: root.width - 32
            spacing: 20

            // ── Section 1: Click ──────────────────────────────
            GroupBox {
                title: "Click Test"
                Layout.fillWidth: true

                RowLayout {
                    spacing: 12

                    Button {
                        objectName: "btnHello"
                        text: "Say Hello"
                        onClicked: root.log("btnHello clicked")
                    }

                    Button {
                        objectName: "btnCount"
                        property int count: 0
                        text: "Count: " + count
                        onClicked: {
                            count++
                            root.log("btnCount = " + count)
                        }
                    }

                    Button {
                        objectName: "btnRightClick"
                        text: "Right-click me"
                        onClicked: (mouse) => root.log("btnRightClick: left click (ignored)")
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: root.log("btnRightClick: right button!")
                        }
                    }

                    Rectangle {
                        objectName: "btnDouble"
                        width: btnDoubleLabel.implicitWidth + 24
                        height: 25
                        color: "#F0F0F0"
                        border.width: 1
                        border.color: "#999"
                        radius: 4

                        Label {
                            id: btnDoubleLabel
                            anchors.centerIn: parent
                            text: "Double-click me"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onDoubleClicked: root.log("btnDouble: double-clicked!")
                        }
                    }
                }
            }

            // ── Section 2: Hover ──────────────────────────────
            GroupBox {
                title: "Hover Test"
                Layout.fillWidth: true

                RowLayout {
                    spacing: 12

                    Rectangle {
                        objectName: "hoverRect"
                        width: 120; height: 60
                        color: hoverArea.containsMouse ? "#4CAF50" : "#E0E0E0"
                        radius: 6
                        border.width: 1
                        border.color: "#999"

                        Label {
                            anchors.centerIn: parent
                            text: hoverArea.containsMouse ? "Hovered!" : "Hover me"
                            color: hoverArea.containsMouse ? "white" : "black"
                        }

                        MouseArea {
                            id: hoverArea
                            objectName: "hoverArea"
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: root.log("hoverRect: entered")
                            onExited: root.log("hoverRect: exited")
                        }
                    }

                    Rectangle {
                        objectName: "hoverHandlerRect"
                        width: 120; height: 60
                        color: hh.hovered ? "#2196F3" : "#E0E0E0"
                        radius: 6
                        border.width: 1
                        border.color: "#999"

                        Label {
                            anchors.centerIn: parent
                            text: hh.hovered ? "HH Hovered!" : "HoverHandler"
                            color: hh.hovered ? "white" : "black"
                        }

                        HoverHandler {
                            id: hh
                            onHoveredChanged: root.log("HoverHandler hovered=" + hovered)
                        }
                    }
                }
            }

            // ── Section 3: Focus & Text Input ─────────────────
            GroupBox {
                title: "Focus & Input Test"
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 8

                    RowLayout {
                        spacing: 12

                        TextField {
                            id: inputName
                            objectName: "inputName"
                            placeholderText: "Name"
                            Layout.preferredWidth: 200
                            onTextChanged: root.log("inputName text=" + text)
                            onActiveFocusChanged: root.log("inputName activeFocus=" + activeFocus)
                        }

                        TextField {
                            id: inputEmail
                            objectName: "inputEmail"
                            placeholderText: "Email"
                            Layout.preferredWidth: 200
                            onTextChanged: root.log("inputEmail text=" + text)
                            onActiveFocusChanged: root.log("inputEmail activeFocus=" + activeFocus)
                        }

                        Button {
                            objectName: "btnSubmit"
                            text: "Submit"
                            onClicked: root.log("Submit: name=" + inputName.text + " email=" + inputEmail.text)
                        }
                    }

                    TextArea {
                        objectName: "textArea"
                        placeholderText: "Multi-line text area..."
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        onTextChanged: root.log("textArea length=" + text.length)
                    }
                }
            }

            // ── Section 4: Key Press ──────────────────────────
            GroupBox {
                title: "Key Press Test"
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 8

                    Label {
                        objectName: "keyLabel"
                        text: "Press keys when focused (click the area first)"
                    }

                    Rectangle {
                        objectName: "keyArea"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: keyFocus.activeFocus ? "#FFF3E0" : "#F5F5F5"
                        border.width: 2
                        border.color: keyFocus.activeFocus ? "#FF9800" : "#CCC"
                        radius: 6
                        focus: true

                        Label {
                            id: keyDisplay
                            objectName: "keyDisplay"
                            anchors.centerIn: parent
                            text: "No key pressed yet"
                            font.pixelSize: 16
                        }

                        Keys.onPressed: (event) => {
                            let desc = event.text || root.keyName(event.key) || ("0x" + event.key.toString(16))
                            let mods = []
                            if (event.modifiers & Qt.ControlModifier) mods.push("Ctrl")
                            if (event.modifiers & Qt.ShiftModifier) mods.push("Shift")
                            if (event.modifiers & Qt.AltModifier) mods.push("Alt")
                            if (event.modifiers & Qt.MetaModifier) mods.push("Meta")
                            let full = mods.length ? mods.join("+") + "+" + desc : desc
                            keyDisplay.text = "Key: " + full
                            root.log("keyArea key=" + full)
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: parent.forceActiveFocus()
                        }

                        FocusScope {
                            id: keyFocus
                            anchors.fill: parent
                            focus: true
                        }
                    }
                }
            }

            // ── Section 5: Scroll ─────────────────────────────
            GroupBox {
                title: "Scroll Test"
                Layout.fillWidth: true
                Layout.preferredHeight: 190

                ListView {
                    objectName: "scrollList"
                    anchors.fill: parent
                    clip: true
                    model: 50
                    delegate: ItemDelegate {
                        objectName: "listItem_" + index
                        width: ListView.view ? ListView.view.width : 100
                        text: "Item #" + (index + 1)
                        onClicked: root.log("scrollList item " + (index + 1) + " clicked")
                    }

                    onContentYChanged: root.log("scrollList contentY=" + Math.round(contentY))
                }
            }

            // ── Section 6: Property Manipulation ──────────────
            GroupBox {
                title: "Property Test (set_property)"
                Layout.fillWidth: true

                RowLayout {
                    spacing: 12

                    Rectangle {
                        objectName: "colorBox"
                        width: 80; height: 80
                        color: "#FF5722"
                        radius: 8
                        onColorChanged: root.log("colorBox color=" + color)

                        Label {
                            anchors.centerIn: parent
                            text: "Color"
                            color: "white"
                            font.bold: true
                        }
                    }

                    Rectangle {
                        objectName: "opacityBox"
                        width: 80; height: 80
                        color: "#9C27B0"
                        radius: 8
                        opacity: 1.0
                        onOpacityChanged: root.log("opacityBox opacity=" + opacity.toFixed(2))

                        Label {
                            anchors.centerIn: parent
                            text: "Opacity\n" + parent.opacity.toFixed(1)
                            color: "white"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Rectangle {
                        objectName: "visibleBox"
                        width: 80; height: 80
                        color: "#3F51B5"
                        radius: 8
                        visible: true
                        onVisibleChanged: root.log("visibleBox visible=" + visible)

                        Label {
                            anchors.centerIn: parent
                            text: "Visible"
                            color: "white"
                            font.bold: true
                        }
                    }

                    Label {
                        id: dynamicLabel
                        objectName: "dynamicLabel"
                        text: "Change me!"
                        font.pixelSize: 18
                        onTextChanged: root.log("dynamicLabel text=" + dynamicLabel.text)
                    }

                    CheckBox {
                        objectName: "checkBox"
                        text: "Toggle"
                        onCheckedChanged: root.log("checkBox checked=" + checked)
                    }

                    Slider {
                        objectName: "slider"
                        from: 0; to: 100; value: 50
                        Layout.preferredWidth: 150
                        onValueChanged: root.log("slider value=" + Math.round(value))
                    }
                }
            }
        }
    }
}
