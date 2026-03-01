#include "ui/SessionRuntimeOptions.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace vaultrdp::ui {

SessionRuntimeOptions parseSessionRuntimeOptions(const QString& optionsJson) {
  SessionRuntimeOptions options;
  const QByteArray utf8 = optionsJson.toUtf8();
  if (utf8.trimmed().isEmpty()) {
    return options;
  }

  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(utf8, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return options;
  }

  const QJsonObject obj = doc.object();
  if (obj.contains("enableClipboard") && obj.value("enableClipboard").isBool()) {
    options.enableClipboard = obj.value("enableClipboard").toBool();
  }
  if (obj.contains("mapHomeDrive") && obj.value("mapHomeDrive").isBool()) {
    options.mapHomeDrive = obj.value("mapHomeDrive").toBool();
  }
  if (obj.contains("lastSuccessfulUsername") && obj.value("lastSuccessfulUsername").isString()) {
    options.lastSuccessfulUsername = obj.value("lastSuccessfulUsername").toString().trimmed();
  }

  return options;
}

QString makeSessionRuntimeOptionsJson(bool enableClipboard, bool mapHomeDrive,
                                      const QString& lastSuccessfulUsername) {
  QJsonObject obj;
  obj.insert("enableClipboard", enableClipboard);
  obj.insert("mapHomeDrive", mapHomeDrive);
  if (!lastSuccessfulUsername.trimmed().isEmpty()) {
    obj.insert("lastSuccessfulUsername", lastSuccessfulUsername.trimmed());
  }
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

}  // namespace vaultrdp::ui

