/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Vesa-Matti Hartikainen <vesa-matti.hartikainen@jollamobile.com>
**
****************************************************************************/
import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Silica.theme 1.0

Image {
    property string favicon
    property string link

    source: favicon != "" ? favicon : WebUtils.getFaviconForUrl(link)
    height: Theme.iconSizeSmall
    width: Theme.iconSizeSmall
    asynchronous: true
    smooth: true
    fillMode: Image.PreserveAspectCrop
    clip: true

    onStatusChanged: {
        if (status == Image.Error) {
            source = "image://theme/icon-m-region"
        }
    }
}