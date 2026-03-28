#pragma once

#include <QString>

namespace vaultrdp::core {

void initializeAppLogging(const QString& stateDir, bool debugMode);
void shutdownAppLogging();

}  // namespace vaultrdp::core

