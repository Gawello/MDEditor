#include "app/AppSettings.h"

#include <QSettings>

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

QVariant AppSettings::value(const QString &key, const QVariant &defaultValue) const
{
    return QSettings().value(key, defaultValue);
}

void AppSettings::setValue(const QString &key, const QVariant &value)
{
    QSettings settings;
    if (settings.value(key) == value)
        return;
    settings.setValue(key, value);
    emit changed(key);
}

QString AppSettings::formatterCommand(const QString &language) const
{
    return value(QStringLiteral("format/%1/command").arg(language)).toString();
}

void AppSettings::setFormatterCommand(const QString &language, const QString &command)
{
    setValue(QStringLiteral("format/%1/command").arg(language), command);
}
