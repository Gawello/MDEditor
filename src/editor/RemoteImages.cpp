#include "editor/RemoteImages.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

RemoteImages &RemoteImages::instance()
{
    static RemoteImages *images = new RemoteImages; // lives as long as the application (parented to it)
    return *images;
}

RemoteImages::RemoteImages()
    : QObject(QCoreApplication::instance())
    , m_network(new QNetworkAccessManager(this))
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images"));
    cache->setMaximumCacheSize(200 * 1024 * 1024);
    m_network->setCache(cache);
    m_network->setTransferTimeout(timeoutMs);
}

bool RemoteImages::isRemote(const QString &target)
{
    const QString scheme = QUrl(target).scheme().toLower();
    return scheme == u"http" || scheme == u"https";
}

QImage RemoteImages::image(const QUrl &url)
{
    if (const auto it = m_images.constFind(url); it != m_images.constEnd())
        return *it;
    if (m_pending.contains(url) || m_failed.contains(url) || !url.isValid())
        return {};
    m_pending.insert(url);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MDEditor/%1").arg(QCoreApplication::applicationVersion()));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes)
            reply->abort(); // not an image for a note
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        m_pending.remove(url);
        QImage image;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QBuffer buffer(&data);
            QImageReader reader(&buffer);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.width() > 2400)
                image = image.scaledToWidth(2400, Qt::SmoothTransformation);
        }
        if (image.isNull())
            m_failed.insert(url);
        else
            m_images.insert(url, image);
        emit loaded(url);
    });
    return {};
}
