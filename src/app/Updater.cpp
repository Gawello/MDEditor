#include "app/Updater.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

Updater::~Updater()
{
    cancel();
}

QNetworkReply *Updater::get(const QUrl &url)
{
    QNetworkRequest request(url);
    // GitHub's API rejects requests without a User-Agent. The app's name and version only.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MDEditor/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000); // without any data for this long
    return m_network->get(request);
}

void Updater::check(const QUrl &url)
{
    cancel();
    QNetworkReply *reply = get(url);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply = nullptr;
        Updates::Release release;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            release.error = tr("No release has been published yet.");
        } else if (reply->error() != QNetworkReply::NoError) {
            release.error = tr("Can't reach the update server: %1").arg(reply->errorString());
        } else {
            release = Updates::parseRelease(reply->readAll());
        }
        emit checked(release);
    });
}

void Updater::download(const Updates::Asset &asset, const QString &path)
{
    cancel();
    m_asset = asset;
    m_hash.reset();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = m_file.errorString();
        emit downloaded(path, tr("Can't write %1: %2").arg(path, error));
        return;
    }

    QNetworkReply *reply = get(asset.url);
    m_reply = reply;
    reply->setReadBufferSize(1 << 20);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray data = reply->readAll();
        m_hash.addData(data);
        if (m_file.write(data) != data.size())
            reply->abort(); // reported as a write error in finishDownload
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit progress(received, total > 0 ? total : m_asset.size);
    });
    connect(reply, &QNetworkReply::finished, this, &Updater::finishDownload);
}

void Updater::finishDownload()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    reply->deleteLater();
    m_reply = nullptr;
    const QByteArray rest = reply->readAll();
    m_hash.addData(rest);
    const bool written = m_file.write(rest) == rest.size() && m_file.error() == QFile::NoError && m_file.flush();
    const qint64 size = m_file.size();
    m_file.close();

    QString error;
    if (!written)
        error = tr("Can't write %1: %2").arg(m_file.fileName(), m_file.errorString());
    else if (reply->error() != QNetworkReply::NoError)
        error = tr("Download failed: %1").arg(reply->errorString());
    else if (m_asset.size >= 0 && size != m_asset.size)
        error = tr("The download is incomplete (%1 of %2 bytes).").arg(size).arg(m_asset.size);
    else if (!m_asset.sha256.isEmpty() && m_hash.result() != m_asset.sha256)
        error = tr("The downloaded file is damaged: its checksum doesn't match the release.");
    if (!error.isEmpty())
        m_file.remove();
    emit downloaded(m_file.fileName(), error);
}

// Silent: whoever cancels knows. A cancelled download leaves no file behind.
void Updater::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }
}
