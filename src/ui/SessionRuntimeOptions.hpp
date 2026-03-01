#pragma once

#include <QString>

namespace vaultrdp::ui {

struct SessionRuntimeOptions {
  bool enableClipboard = true;
  bool mapHomeDrive = true;
  QString lastSuccessfulUsername;
};

SessionRuntimeOptions parseSessionRuntimeOptions(const QString& optionsJson);

QString makeSessionRuntimeOptionsJson(bool enableClipboard, bool mapHomeDrive,
                                      const QString& lastSuccessfulUsername = QString());

}  // namespace vaultrdp::ui

