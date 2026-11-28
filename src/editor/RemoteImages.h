#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;

// Images from the web (![](https://…)), shared by every editor: downloaded once in the background, kept in
// memory and in a disk cache (so they show offline later). A failed download isn't retried until restart.
class RemoteImages : public QObject {
    Q_OBJECT

public:
    static RemoteImages &instance();

    // The image if it's here; otherwise starts fetching it (when allowed) and returns a null image.
    QImage image(const QUrl &url);
    bool failed(const QUrl &url) const { return m_failed.contains(url); }
    static bool isRemote(const QString &target); // http(s) URL

    static constexpr qint64 maxBytes = 10 * 1024 * 1024;
    static constexpr int timeoutMs = 20000;

signals:
    void loaded(const QUrl &url); // arrived (or failed): editors showing it lay out again

private:
    RemoteImages();
    QNetworkAccessManager *m_network;
    QHash<QUrl, QImage> m_images;
    QSet<QUrl> m_pending;
    QSet<QUrl> m_failed;
};
