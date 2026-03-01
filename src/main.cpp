#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
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

#include "core/DatabaseManager.hpp"
#include "core/VaultManager.hpp"
#include "system/ScreenLockMonitor.hpp"
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName("VaultRDP");
  QCoreApplication::setOrganizationName("VaultRDP");
  app.setWindowIcon(QIcon(":/icons/vaultrdp.png"));
#if defined(Q_OS_LINUX)
  QGuiApplication::setDesktopFileName("vaultrdp");
#endif

  const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/VaultRDP";
  if (!QDir().mkpath(stateDir)) {
    return 1;
  }

  initializeFileLogging(stateDir);

  QCommandLineParser parser;
  parser.setApplicationDescription("VaultRDP");
  parser.addHelpOption();
  QCommandLineOption debugOption("debug", "Enable verbose debug logging.");
  parser.addOption(debugOption);
  parser.process(app);

  gDebugLoggingEnabled = parser.isSet(debugOption);
  app.setProperty("debugMode", gDebugLoggingEnabled);
  gPreviousMessageHandler = qInstallMessageHandler(appMessageHandler);
  if (gDebugLoggingEnabled) {
    qputenv("WLOG_LEVEL", "DEBUG");
    qputenv("WLOG_FILTER", "com.freerdp.core.update:WARN,com.freerdp.client.common.cliprdr.file:INFO");
    qInfo() << "Debug logging enabled.";
  } else {
    qputenv("WLOG_LEVEL", "WARN");
    qunsetenv("WLOG_FILTER");
  }
  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    QMutexLocker lock(&gLogFileMutex);
    if (gLogFile && gLogFile->isOpen()) {
      gLogFile->flush();
      gLogFile->close();
    }
  });

  DatabaseManager databaseManager(stateDir + "/vaultrdp.db");
  if (!databaseManager.initialize()) {
    return 1;
  }

  vaultrdp::core::VaultManager vaultManager(&databaseManager);
  MainWindow mainWindow(&databaseManager, &vaultManager);

  vaultrdp::system::ScreenLockMonitor screenLockMonitor;
  QObject::connect(&screenLockMonitor, &vaultrdp::system::ScreenLockMonitor::screenLockChanged,
                   [&vaultManager, &mainWindow](bool locked) {
                     qInfo() << "Screen lock changed. Locked =" << locked;
                     if (locked) {
                       vaultManager.lock();
                       mainWindow.refreshVaultUi();
                     }
                   });
  screenLockMonitor.start();

  mainWindow.show();

  return app.exec();
}
