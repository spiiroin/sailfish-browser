/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Petri M. Gerdt <petri.gerdt@jollamobile.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "declarativetabmodel.h"
#include "dbmanager.h"
#include "linkvalidator.h"

#include <QFile>
#include <QDebug>
#include <QStringList>
#include <QUrl>

static QList<int> s_tabOrder;

DeclarativeTabModel::DeclarativeTabModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loaded(false)
    , m_browsing(false)
    , m_nextTabId(DBManager::instance()->getMaxTabId() + 1)
{
    connect(DBManager::instance(), SIGNAL(tabsAvailable(QList<Tab>)),
            this, SLOT(tabsAvailable(QList<Tab>)));
    connect(DBManager::instance(), SIGNAL(tabChanged(Tab)),
            this, SLOT(tabChanged(Tab)));
    connect(DBManager::instance(), SIGNAL(titleChanged(int,int,QString,QString)),
            this, SLOT(updateTitle(int,int,QString,QString)));
}

QHash<int, QByteArray> DeclarativeTabModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ThumbPathRole] = "thumbnailPath";
    roles[TitleRole] = "title";
    roles[UrlRole] = "url";
    roles[TabIdRole] = "tabId";
    roles[FaviconRole] = "favicon";
    roles[FavoritedRole] = "favorited";
    return roles;
}

void DeclarativeTabModel::addTab(const QString& url, const QString &title) {
    if (!LinkValidator::navigable(QUrl(url))) {
        return;
    }
    int tabId = DBManager::instance()->createTab();
    int linkId = DBManager::instance()->createLink(tabId, url, title);

    Tab tab(tabId, Link(linkId, url, "", title), 0, 0);
    if (m_bookmarkModel) {
        tab.setBookmarked(m_bookmarkModel->contains(url));
    }
#ifdef DEBUG_LOGS
    qDebug() << "new tab data:" << &tab;
#endif
    beginInsertRows(QModelIndex(), 0, 0);
    int oldActiveTab = -1;
    if (!m_tabs.empty()) {
        oldActiveTab = m_tabs.at(0).tabId();
    }

    m_tabs.insert(0, tab);
    endInsertRows();
    updateActiveTab(oldActiveTab, tab.tabId());

    emit countChanged();
    emit tabAdded(tabId);

    m_nextTabId = ++tabId;
    emit nextTabIdChanged();
}

int DeclarativeTabModel::nextTabId() const
{
    return m_nextTabId;
}

void DeclarativeTabModel::remove(int index) {
    if (index == 0) {
        closeActiveTab();
    } else if (!m_tabs.isEmpty() && index >= 0 && index < m_tabs.count()) {
        removeTab(m_tabs.at(index).tabId(), m_tabs.at(index).thumbnailPath(), index);
        saveTabOrder();
    }
}

void DeclarativeTabModel::removeTabById(int tabId, bool activeTab)
{
    int index = -1;
    if (!activeTab) {
        index = findTabIndex(tabId);
    }

    if (activeTab) {
        closeActiveTab();
    } else if (index >= 0){
        remove(index);
    }
}

void DeclarativeTabModel::clear()
{
    if (count() == 0)
        return;

    for (int i = m_tabs.count() - 1; i >= 0; --i) {
        removeTab(m_tabs.at(i).tabId(), m_tabs.at(i).thumbnailPath(), i);
    }
    resetNewTabData();
    emit tabsCleared();
}


bool DeclarativeTabModel::activateTab(const QString& url)
{
    for (int i = 0; i < m_tabs.size(); i++) {
        if (m_tabs.at(i).url() == url) {
            return activateTab(i);
        }
    }
    return false;
}

bool DeclarativeTabModel::activateTab(int index)
{
    if (index > 0 && index < m_tabs.count()) {
        Tab newActiveTab = m_tabs.at(index);
#ifdef DEBUG_LOGS
        qDebug() << "activate tab: " << index << &newActiveTab;
#endif
        int oldTabId = m_tabs.at(0).tabId();
        // Remove tab from index position.
        beginRemoveRows(QModelIndex(), index, index);
        m_tabs.removeAt(index);
        endRemoveRows();

        // Current active tab back to model data.
        beginInsertRows(QModelIndex(), 0, 0);
        m_tabs.insert(0, newActiveTab);
        endInsertRows();

        updateActiveTab(oldTabId, newActiveTab.tabId());
        return true;
    }
    // In case of activating the first tab, handle it here.
    // No need to suffle model, but just return true.
    return index == 0 && !m_tabs.isEmpty();
}

bool DeclarativeTabModel::activateTabById(int tabId)
{
    int index = findTabIndex(tabId);
    if (index >= 0) {
        return activateTab(index);
    }
    return false;
}

void DeclarativeTabModel::closeActiveTab()
{
    if (!m_tabs.isEmpty()) {
        // Clear active tab data and try to active a tab from the first model index.
        int oldActiveTabId = m_tabs.at(0).tabId();
        // Invalidate
        m_tabs[0].setTabId(0);
        removeTab(oldActiveTabId, m_tabs.at(0).thumbnailPath(), 0);
        if (m_tabs.isEmpty()) {
            // Last active tab got closed.
            emit activeTabChanged(oldActiveTabId, 0);
        } else {
            updateActiveTab(oldActiveTabId, m_tabs.at(0).tabId());
        }
    }
}

void DeclarativeTabModel::addFavoriteIcon(int tabId, const QString &icon)
{
    for (int i = 0; i < m_tabs.count(); ++i) {
        const Tab &tab = m_tabs.at(i);
        if (tab.tabId() == tabId && tab.favoriteIcon() != icon) {
            m_tabs[i].setFavoriteIcon(icon);
            QVector<int> roles;
            roles << FaviconRole;

            QModelIndex start = index(i, 0);
            QModelIndex end = index(i, 0);
            emit dataChanged(start, end, roles);
        }
    }
}

void DeclarativeTabModel::newTab(const QString &url, const QString &title)
{
    // TODO: This doesn't really fit in here. We should consider adding of custom event for new tab creation.
    emit newTabRequested(url, title);
}

void DeclarativeTabModel::newTabData(const QString &url, const QString &title, QObject *contentItem, int parentId)
{
    updateNewTabData(new NewTabData(url, title, contentItem, parentId));
}

void DeclarativeTabModel::resetNewTabData()
{
    updateNewTabData(0);
}

void DeclarativeTabModel::dumpTabs() const
{
    for (int i = 0; i < m_tabs.size(); i++) {
        qDebug() << "tab[" << i << "]:" << &m_tabs.at(i);
    }
}

int DeclarativeTabModel::count() const
{
    return m_tabs.count();
}

int DeclarativeTabModel::rowCount(const QModelIndex & parent) const {
    Q_UNUSED(parent);
    return m_tabs.count();
}

QVariant DeclarativeTabModel::data(const QModelIndex & index, int role) const {
    if (index.row() < 0 || index.row() > m_tabs.count())
        return QVariant();

    const Tab &tab = m_tabs.at(index.row());
    if (role == ThumbPathRole) {
        return tab.thumbnailPath();
    } else if (role == TitleRole) {
        return tab.title();
    } else if (role == UrlRole) {
        return tab.url();
    } else if (role == TabIdRole) {
        return tab.tabId();
    } else if (role == FaviconRole) {
        return tab.favoriteIcon();
    } else if (role == FavoritedRole) {
        return tab.bookmarked();
    }
    return QVariant();
}

void DeclarativeTabModel::componentComplete()
{
}

bool DeclarativeTabModel::loaded() const
{
    return m_loaded;
}

bool DeclarativeTabModel::browsing() const
{
    return m_browsing;
}

void DeclarativeTabModel::setBrowsing(bool browsing)
{
    if (browsing != m_browsing) {
        m_browsing = browsing;
        if (m_browsing) {
            emit updateActiveThumbnail();
        }
        emit browsingChanged();
    }
}

bool DeclarativeTabModel::hasNewTabData() const
{
    return m_newTabData && !m_newTabData->url.isEmpty();
}

QString DeclarativeTabModel::newTabUrl() const
{
    return hasNewTabData() ? m_newTabData->url : "";
}

DeclarativeBookmarkModel *DeclarativeTabModel::bookmarkModel() const
{
    return m_bookmarkModel;
}

void DeclarativeTabModel::setBookmarkModel(DeclarativeBookmarkModel *bookmarkModel)
{
    if (bookmarkModel != m_bookmarkModel) {
        if (m_bookmarkModel) {
            disconnect(m_bookmarkModel, 0, this, 0);
        }

        m_bookmarkModel = bookmarkModel;
        connect(m_bookmarkModel, SIGNAL(bookmarkAdded(QString)), this, SLOT(handleBookmarkAdded(QString)));
        connect(m_bookmarkModel, SIGNAL(bookmarkRemoved(QString)), this, SLOT(handleBookmarkRemoved(QString)));
        emit bookmarkModelChanged();
    }
}

QString DeclarativeTabModel::newTabTitle() const
{
    return hasNewTabData() ? m_newTabData->title : "";
}

QObject *DeclarativeTabModel::newTabPreviousPage() const
{
    return hasNewTabData() ? m_newTabData->previousPage : 0;
}

const QList<Tab> &DeclarativeTabModel::tabs() const
{
    return m_tabs;
}

const Tab &DeclarativeTabModel::activeTab() const
{
    if (!m_tabs.isEmpty()) {
        return m_tabs.at(0);
    } else {
        // This is an invalid tab.
        return m_dummyTab;
    }
}

int DeclarativeTabModel::newTabParentId() const
{
    return hasNewTabData() ? m_newTabData->parentId : 0;
}

void DeclarativeTabModel::classBegin()
{
    DBManager::instance()->getAllTabs();
}

void DeclarativeTabModel::tabsAvailable(QList<Tab> tabs)
{
    beginResetModel();
    int oldCount = count();
    m_tabs.clear();
    m_tabs = tabs;

    if (m_tabs.count() > 0) {
        loadTabOrder();
        qSort(m_tabs.begin(), m_tabs.end(), DeclarativeTabModel::tabSort);
    } else {
        emit tabsCleared();
    }

    endResetModel();

    if (count() != oldCount) {
        emit countChanged();
    }

    int maxTabId = DBManager::instance()->getMaxTabId();
    if (m_nextTabId != maxTabId + 1) {
        m_nextTabId = maxTabId + 1;
        emit nextTabIdChanged();
    }

    // Startup should be synced to this.
    if (!m_loaded) {
        m_loaded = true;
        emit loadedChanged();
    }
}

void DeclarativeTabModel::tabChanged(const Tab &tab)
{
#ifdef DEBUG_LOGS
    qDebug() << "new tab data:" << &tab;
#endif
    if (m_tabs.isEmpty()) {
        qWarning() << "No tabs!";
        return;
    }

    int oldActiveTabId = m_tabs.at(0).tabId();
    int i = m_tabs.indexOf(tab); // match based on tab_id
    if (i > -1) {
        QVector<int> roles;
        Tab oldTab = m_tabs[i];
        if (oldTab.url() != tab.url()) {
            roles << UrlRole;
        }
        if (oldTab.title() != tab.title()) {
            roles << TitleRole;
        }
        if (oldTab.thumbnailPath() != tab.thumbnailPath()) {
            roles << ThumbPathRole;
        }
        m_tabs[i] = tab;
        QModelIndex start = index(i, 0);
        QModelIndex end = index(i, 0);
        emit dataChanged(start, end, roles);
    }

    updateActiveTab(oldActiveTabId, tab.tabId());
}

void DeclarativeTabModel::updateTitle(int tabId, int linkId, QString url, QString title)
{
    for (int i = 0; i < m_tabs.count(); ++i) {
        const Tab &tab = m_tabs.at(i);
        if (tab.tabId() == tabId && tab.currentLink() == linkId && tab.url() == url) {
            m_tabs[i].setTitle(title);
            QVector<int> roles;
            roles << TitleRole;
            QModelIndex start = index(i, 0);
            QModelIndex end = index(i, 0);
            emit dataChanged(start, end, roles);
            break;
        }
    }
}

void DeclarativeTabModel::handleBookmarkAdded(QString url)
{
    updateBookmarkedStatus(url, true);
}

void DeclarativeTabModel::handleBookmarkRemoved(QString url)
{
    updateBookmarkedStatus(url, false);
}

void DeclarativeTabModel::updateUrl(int tabId, bool activeTab, QString url, bool backForwardNavigation, bool initialLoad)
{
    if (backForwardNavigation)
    {
        updateTabUrl(tabId, activeTab, url, false);
    } else if (!hasNewTabData()) {
        updateTabUrl(tabId, activeTab, url, !initialLoad);
    } else {
        addTab(url, newTabTitle());
    }
    resetNewTabData();

    if (m_bookmarkModel) {
        updateBookmarkedStatus(url, m_bookmarkModel->contains(url));
        addFavoriteIcon(tabId, "");
    }
}

void DeclarativeTabModel::updateTitle(int tabId, bool activeTab, QString title)
{
    int tabIndex = findTabIndex(tabId);
    bool updateDb = false;
    int linkId = 0;
    if (tabIndex >= 0 && (m_tabs.at(tabIndex).title() != title || activeTab)) {
        QVector<int> roles;
        roles << TitleRole;
        m_tabs[tabIndex].setTitle(title);
        linkId = m_tabs.at(tabIndex).currentLink();
        emit dataChanged(index(tabIndex, 0), index(tabIndex, 0), roles);
        updateDb = true;
    }

    if (updateDb) {
        DBManager::instance()->updateTitle(tabId, linkId, title);
    }
}

void DeclarativeTabModel::removeTab(int tabId, const QString &thumbnail, int index)
{
#ifdef DEBUG_LOGS
    qDebug() << "index:" << index << tabId;
#endif
    DBManager::instance()->removeTab(tabId);
    QFile f(thumbnail);
    if (f.exists()) {
        f.remove();
    }

    if (index >= 0) {
        beginRemoveRows(QModelIndex(), index, index);
        m_tabs.removeAt(index);
        endRemoveRows();
    }

    emit countChanged();
    emit tabClosed(tabId);
}

int DeclarativeTabModel::findTabIndex(int tabId) const
{
    for (int i = 0; i < m_tabs.size(); i++) {
        if (m_tabs.at(i).tabId() == tabId) {
            return i;
        }
    }
    return -1;
}

void DeclarativeTabModel::saveTabOrder()
{
    QString tabOrder = "";
    for (int i = 0; i < m_tabs.count(); ++i) {
        const Tab &tab = m_tabs.at(i);
        tabOrder.append(QString("%1,").arg(tab.tabId()));
#ifdef DEBUG_LOGS
        qDebug() << "append:" << &tab;
#endif
    }
    DBManager::instance()->saveSetting("tabOrder", tabOrder);
}

void DeclarativeTabModel::loadTabOrder()
{
    QString tabOrder = DBManager::instance()->getSetting("tabOrder");
    QStringList tmpList = tabOrder.split(",");
    bool ok = false;
    for (int i = 0; i < tmpList.count(); ++i) {
        const QString &strTab = tmpList.at(i);
        int tabId = strTab.toInt(&ok);
        if (ok) {
            s_tabOrder << tabId;
        }
    }
}
void DeclarativeTabModel::updateActiveTab(int oldActiveTabId, int activeTabId)
{
#ifdef DEBUG_LOGS
    qDebug() << "new active tab id:" << activeTabId << "old:" << oldActiveTabId << "count:" << m_tabs.count();
#endif
    if (m_tabs.isEmpty()) {
        return;
    }

    if (oldActiveTabId != activeTabId) {
        emit activeTabChanged(oldActiveTabId, activeTabId);
        saveTabOrder();
    }
}

void DeclarativeTabModel::updateTabUrl(int tabId, bool activeTab, const QString &url, bool navigate)
{
    if (!LinkValidator::navigable(QUrl(url))) {
#ifdef DEBUG_LOGS
        qDebug() << "invalid url: " << url;
#endif
        return;
    }

    int tabIndex = findTabIndex(tabId);
    bool updateDb = false;
    if (tabIndex >= 0 && (m_tabs.at(tabIndex).url() != url || activeTab)) {
        QVector<int> roles;
        roles << UrlRole << TitleRole << ThumbPathRole;
        m_tabs[tabIndex].setUrl(url);

        if (navigate) {
            m_tabs[tabIndex].setNextLink(0);
            int currentLinkId = m_tabs.at(tabIndex).currentLink();
            m_tabs[tabIndex].setPreviousLink(currentLinkId);
            m_tabs[tabIndex].setCurrentLink(DBManager::instance()->nextLinkId());
        }
        m_tabs[tabIndex].setTitle("");
        m_tabs[tabIndex].setThumbnailPath("");
        emit dataChanged(index(tabIndex, 0), index(tabIndex, 0), roles);
        updateDb = true;
    }

    if (updateDb) {
        if (!navigate) {
            DBManager::instance()->updateTab(tabId, url, "", "");
        } else {
            DBManager::instance()->navigateTo(tabId, url, "", "");
        }
    }
}

void DeclarativeTabModel::updateBookmarkedStatus(const QString &url, bool bookmarked)
{
    for (int i = 0; i < m_tabs.count(); ++i) {
        const Tab &tab = m_tabs.at(i);
        if (tab.url() == url && bookmarked != tab.bookmarked()) {
            QVector<int> roles;
            m_tabs[i].setBookmarked(bookmarked);
            roles << FavoritedRole;

            if (!bookmarked && !tab.favoriteIcon().isEmpty()) {
                roles << FaviconRole;
                m_tabs[i].setFavoriteIcon("");
            }

            QModelIndex start = index(i, 0);
            QModelIndex end = index(i, 0);
            emit dataChanged(start, end, roles);
        }
    }
}

void DeclarativeTabModel::updateNewTabData(NewTabData *newTabData)
{
    bool hadNewTabData = hasNewTabData();
    QString currentTabUrl = newTabUrl();
    bool urlChanged = newTabData ? currentTabUrl != newTabData->url : !currentTabUrl.isEmpty();

    m_newTabData.reset(newTabData);
    if (urlChanged) {
        emit newTabUrlChanged();
    }

    if (hadNewTabData != hasNewTabData()) {
        emit hasNewTabDataChanged();
    }
}

bool DeclarativeTabModel::tabSort(const Tab &t1, const Tab &t2)
{
    int i1 = s_tabOrder.indexOf(t1.tabId());
    int i2 = s_tabOrder.indexOf(t2.tabId());
    if (i2 == -1) {
        return true;
    } else {
        return i1 < i2;
    }
}

void DeclarativeTabModel::updateThumbnailPath(int tabId, QString path)
{
    QVector<int> roles;
    roles << ThumbPathRole;
    for (int i = 0; i < m_tabs.count(); i++) {
        if (m_tabs.at(i).tabId() == tabId) {
#ifdef DEBUG_LOGS
            qDebug() << "model tab thumbnail updated: " << path << tabId;
#endif
            m_tabs[i].setThumbnailPath(path);
            QModelIndex start = index(i, 0);
            QModelIndex end = index(i, 0);
            emit dataChanged(start, end, roles);
        }
    }
}
