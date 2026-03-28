#pragma once

#include <QString>

namespace vaultrdp::ui {

struct SessionRuntimeOptions {
  bool enableClipboard = true;
  bool mapHomeDrive = true;
  bool promptEveryTime = false;
  QString lastSuccessfulUsername;
};

SessionRuntimeOptions parseSessionRuntimeOptions(const QString& optionsJson);

QString makeSessionRuntimeOptionsJson(bool enableClipboard, bool mapHomeDrive, bool promptEveryTime,
                                      const QString& lastSuccessfulUsername = QString());

}  // namespace vaultrdp::ui
