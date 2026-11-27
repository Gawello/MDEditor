#pragma once

#include "app/Updates.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// The network side of updates: asks for the latest release and downloads a package, checking its size and
// SHA-256. One request at a time; nothing here shows any UI.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject *parent = nullptr);
    ~Updater() override;

    bool busy() const { return !m_reply.isNull(); }
    void check(const QUrl &url = Updates::releaseUrl());
    // Downloads `asset` to `path`, replacing it. On any failure the file is removed.
    void download(const Updates::Asset &asset, const QString &path);
    void cancel(); // silently; a partial download is removed

signals:
    void checked(const Updates::Release &release); // release.error says why the check failed
    void progress(qint64 received, qint64 total);
    void downloaded(const QString &path, const QString &error); // empty error: done and verified

private:
    QNetworkReply *get(const QUrl &url);
    void finishDownload();

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    Updates::Asset m_asset;
};
