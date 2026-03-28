#include "core/AppLogging.hpp"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>
#include <memory>

namespace {

QtMessageHandler gPreviousMessageHandler = nullptr;
bool gDebugLoggingEnabled = false;
QMutex gLogFileMutex;
std::unique_ptr<QFile> gLogFile;

QString messageTypeLabel(QtMsgType type) {
  switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
  }
  return "UNKNOWN";
}

bool initializeFileLogging(const QString& stateDir) {
  const QString currentLog = stateDir + "/vaultrdp.log";
  const QString oldestLog = stateDir + "/vaultrdp.log.5";
  QFile::remove(oldestLog);
  for (int i = 4; i >= 1; --i) {
    const QString from = stateDir + QString("/vaultrdp.log.%1").arg(i);
    const QString to = stateDir + QString("/vaultrdp.log.%1").arg(i + 1);
    if (QFile::exists(from)) {
      QFile::remove(to);
      QFile::rename(from, to);
    }
  }
  if (QFile::exists(currentLog)) {
    QFile::remove(stateDir + "/vaultrdp.log.1");
    QFile::rename(currentLog, stateDir + "/vaultrdp.log.1");
  }

  gLogFile = std::make_unique<QFile>(currentLog);
  if (!gLogFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
    gLogFile.reset();
    return false;
  }
  return true;
}

void appMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  const QString formatted =
      QString("%1 [%2] %3")
          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
          .arg(messageTypeLabel(type))
          .arg(message);
  {
    QMutexLocker lock(&gLogFileMutex);
    if (gLogFile && gLogFile->isOpen()) {
      QTextStream stream(gLogFile.get());
      stream << formatted << '\n';
      stream.flush();
      gLogFile->flush();
    }
  }

  if (!gDebugLoggingEnabled && (type == QtDebugMsg || type == QtInfoMsg)) {
    return;
  }
  if (gPreviousMessageHandler != nullptr) {
    gPreviousMessageHandler(type, context, message);
    return;
  }
  const QByteArray text = message.toLocal8Bit();
  fprintf(stderr, "%s\n", text.constData());
}

}  // namespace

namespace vaultrdp::core {

void initializeAppLogging(const QString& stateDir, bool debugMode) {
  gDebugLoggingEnabled = debugMode;
  initializeFileLogging(stateDir);
  gPreviousMessageHandler = qInstallMessageHandler(appMessageHandler);

  if (debugMode) {
    qputenv("WLOG_LEVEL", "DEBUG");
    qputenv("WLOG_FILTER", "com.freerdp.core.update:WARN,com.freerdp.client.common.cliprdr.file:INFO");
    qInfo() << "Debug logging enabled.";
  } else {
    qputenv("WLOG_LEVEL", "WARN");
    qunsetenv("WLOG_FILTER");
  }
}

void shutdownAppLogging() {
  QMutexLocker lock(&gLogFileMutex);
  if (gLogFile && gLogFile->isOpen()) {
    gLogFile->flush();
    gLogFile->close();
  }
}

}  // namespace vaultrdp::core

