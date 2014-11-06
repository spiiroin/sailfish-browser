/****************************************************************************
**
** Copyright (C) 2014 Jolla Ltd.
** Contact: Vesa-Matti Hartikainen <vesa-matti.hartikainen@jolla.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.1
import Sailfish.Silica 1.0

SilicaListView {
    id: view
    property string search

    signal load(string url, string title)

    // To prevent model to steal focus
    currentIndex: -1

    delegate: BackgroundItem {
        id: historyDelegate
        width: view.width
        height: titleText.height * 2 + Theme.paddingMedium
        ListView.onAdd: AddAnimation { target: historyDelegate }

        Column {
            width: view.width - Theme.paddingLarge * 2
            x: Theme.paddingLarge
            anchors.verticalCenter: parent.verticalCenter

            Label {
                id: titleText
                text: Theme.highlightText(title, search, Theme.highlightColor)
                color: highlighted ? Theme.highlightColor : Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
                width: parent.width
            }

            Label {
                text: Theme.highlightText(url, search, Theme.highlightColor)
                opacity: 0.6
                color: highlighted ? Theme.highlightColor : Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
                width: parent.width
            }
        }

        onClicked: {
            view.focus = true
            view.load(model.url, model.title)
        }
    }

    VerticalScrollDecorator {
        parent: view
        flickable: view
    }
}
