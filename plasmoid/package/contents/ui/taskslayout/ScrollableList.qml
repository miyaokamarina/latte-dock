/*
 * Copyright 2019 Michail Vourlakos <mvourlakos@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.7
import QtQuick.Controls 1.4

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore

Flickable{
    id: flickableContainer
    clip: false
    flickableDirection: plasmoid.formFactor === PlasmaCore.Types.Horizontal ? Flickable.HorizontalFlick : Flickable.VerticalFlick
    interactive: false

    property int offset: 0

    readonly property bool animationsFinished: !horizontalAnimation.running && !verticalAnimation.running
    readonly property bool centered: root.alignment === LatteCore.Types.Center
    readonly property bool reversed: Qt.application.layoutDirection === Qt.RightToLeft

    readonly property bool contentsExceed:  {
        if (root.scrollingEnabled) {
            return (!root.vertical ? Math.floor(contentWidth) > width : Math.floor(contentHeight) > height);
        }

        return false;
    }
    readonly property int contentsExtraSpace: {
        if (contentsExceed) {
            if (!root.vertical) {
                return contentWidth - width;
            } else {
                return contentHeight - height;
            }
        }

        return 0;
    }

    readonly property int scrollFirstPos: 0
    readonly property int scrollLastPos: contentsExtraSpace
    readonly property int scrollStep: metrics.totals.length * 3.5
    readonly property int currentPos: !root.vertical ? contentX : contentY

    readonly property int autoScrollTriggerLength: metrics.iconSize + metrics.totals.lengthEdge

    readonly property int alignment: {
        if (root.location === PlasmaCore.Types.LeftEdge) {
            if (centered) return LatteCore.Types.LeftEdgeCenterAlign;
            if (root.alignment === LatteCore.Types.Top) return LatteCore.Types.LeftEdgeTopAlign;
            if (root.alignment === LatteCore.Types.Bottom) return LatteCore.Types.LeftEdgeBottomAlign;
        }

        if (root.location === PlasmaCore.Types.RightEdge) {
            if (centered) return LatteCore.Types.RightEdgeCenterAlign;
            if (root.alignment === LatteCore.Types.Top) return LatteCore.Types.RightEdgeTopAlign;
            if (root.alignment === LatteCore.Types.Bottom) return LatteCore.Types.RightEdgeBottomAlign;
        }

        if (root.location === PlasmaCore.Types.BottomEdge) {
            if (centered) return LatteCore.Types.BottomEdgeCenterAlign;

            if ((root.alignment === LatteCore.Types.Left && !reversed)
                    || (root.alignment === LatteCore.Types.Right && reversed)) {
                return LatteCore.Types.BottomEdgeLeftAlign;
            }

            if ((root.alignment === LatteCore.Types.Right && !reversed)
                    || (root.alignment === LatteCore.Types.Left && reversed)) {
                return LatteCore.Types.BottomEdgeRightAlign;
            }
        }

        if (root.location === PlasmaCore.Types.TopEdge) {
            if (centered) return LatteCore.Types.TopEdgeCenterAlign;

            if ((root.alignment === LatteCore.Types.Left && !reversed)
                    || (root.alignment === LatteCore.Types.Right && reversed)) {
                return LatteCore.Types.TopEdgeLeftAlign;
            }

            if ((root.alignment === LatteCore.Types.Right && !reversed)
                    || (root.alignment === LatteCore.Types.Left && reversed)) {
                return LatteCore.Types.TopEdgeRightAlign;
            }
        }

        return LatteCore.Types.BottomEdgeCenterAlign;
    }

    function increasePos() {
        increasePosWithStep(scrollStep);
    }

    function decreasePos() {
        decreasePosWithStep(scrollStep);
    }

    function increasePosWithStep(step) {
        if (!root.vertical) {
            contentX = Math.min(scrollLastPos, contentX + step);
        } else {
            contentY = Math.min(scrollLastPos, contentY + step);
        }

    }

    function decreasePosWithStep(step) {
        if (!root.vertical) {
            contentX = Math.max(scrollFirstPos, contentX - step);
        } else {
            contentY = Math.max(scrollFirstPos, contentY - step);
        }
    }

    function focusOn(task) {
        if (!contentsExceed) {
            return;
        }

        var cP = task.mapToItem(scrollableList, 0, 0);
        var distance = 0;

        if (!root.vertical) {
            if (cP.x < 0) {
                distance = Math.abs(cP.x - metrics.iconSize);
                decreasePosWithStep(distance);
            } else if ((cP.x+task.width) > scrollableList.width) {
                distance = Math.abs(cP.x - scrollableList.width + task.width + metrics.iconSize);
                increasePosWithStep(distance);
            }
        } else {
            if (cP.y < 0) {
                distance = Math.abs(cP.y - metrics.iconSize);
                decreasePosWithStep(distance);
            } else if ((cP.y+task.height) > scrollableList.height) {
                distance = Math.abs(cP.y - scrollableList.height + task.height + metrics.iconSize);
                increasePosWithStep(distance);
            }
        }
    }

    function autoScrollFor(task, duringDragging) {
        //! It has TWO IN-QUESTION issues that may have been solved by the
        //! initial checks
        //! 1. when the user uses the mouse wheel at the first or the last task
        //!    the autoscrolling forcefully returns the view to boundaries
        //! 2. when parabolic effect is activated the experience is not that smooth
        //!    at the view boundaries, parabolic effect AND autoscroll at the
        //!    boundaries create animation breakage

        var block = !root.autoScrollTasksEnabled && !duringDragging;

        if (block || !contentsExceed || root.tasksCount < 3
                || (task.itemIndex===task.indexer.lastVisibleItemIndex && parabolic.factor.zoom>1)) {
            //last task with parabolic effect breaks the autoscolling behavior
            return;
        }

        var cP = task.mapToItem(scrollableList, 0, 0);

        var localStep = horizontalAnimation.running || verticalAnimation.running ? 3.5 * metrics.totals.length : metrics.totals.length;

        if (!root.vertical) {
            if (currentPos !== scrollFirstPos && cP.x < autoScrollTriggerLength) {
                decreasePosWithStep(localStep);
            } else if (currentPos !== scrollLastPos && (cP.x+task.width > (scrollableList.width-autoScrollTriggerLength))) {
                increasePosWithStep(localStep);
            }
        } else {
            if (currentPos !== scrollFirstPos && cP.y < autoScrollTriggerLength) {
                decreasePosWithStep(localStep);
            } else if (currentPos !== scrollLastPos && (cP.y+task.height > (scrollableList.height-autoScrollTriggerLength))) {
                increasePosWithStep(localStep);
            }
        }

    }

    onContentsExtraSpaceChanged: {
        if (!root.vertical) {
            if (contentX < scrollFirstPos) {
                contentX = scrollFirstPos;
            } else if (contentX > scrollLastPos) {
                contentX = scrollLastPos;
            }
        } else {
            if (contentY < scrollFirstPos) {
                contentY = scrollFirstPos;
            } else if (contentY > scrollLastPos) {
                contentY = scrollLastPos;
            }
        }
    }

    Behavior on contentX {
        NumberAnimation {
            id: horizontalAnimation
            duration: animations.speedFactor.current*4.1*animations.duration.large
            easing.type: Easing.OutQuad
        }
    }

    Behavior on contentY {
        NumberAnimation {
            id: verticalAnimation
            duration: animations.speedFactor.current*4.1*animations.duration.large
            easing.type: Easing.OutQuad
        }
    }

    //////////////////////////BEGIN states
    // Alignment
    // 0-Center, 1-Left, 2-Right, 3-Top, 4-Bottom
    states: [
        ///Left Edge
        State {
            name: "leftCenter"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: flickableContainer.offset;
            }
        },
        State {
            name: "leftTop"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeTopAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.offset;    anchors.bottomMargin:flickableContainer.lastMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "leftBottom"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeBottomAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.lastMargin;    anchors.bottomMargin:flickableContainer.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Right Edge
        State {
            name: "rightCenter"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: flickableContainer.offset;
            }
        },
        State {
            name: "rightTop"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeTopAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.offset;    anchors.bottomMargin:flickableContainer.lastMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "rightBottom"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeBottomAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.lastMargin;    anchors.bottomMargin:flickableContainer.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Bottom Edge
        State {
            name: "bottomCenter"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: flickableContainer.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomLeft"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeLeftAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.offset;    anchors.rightMargin:flickableContainer.lastMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomRight"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeRightAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.lastMargin;    anchors.rightMargin:flickableContainer.offset;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Top Edge
        State {
            name: "topCenter"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: flickableContainer.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topLeft"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeLeftAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.offset;    anchors.rightMargin:flickableContainer.lastMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topRight"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeRightAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.lastMargin;    anchors.rightMargin:flickableContainer.offset;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        }
    ]
}
