#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>

#include "core/AppLogging.hpp"
#include "core/AppPaths.hpp"
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

  if (!vaultrdp::core::ensureStateDirectory()) {
    return 1;
  }
  const QString stateDir = vaultrdp::core::stateDirectory();

  QCommandLineParser parser;
  parser.setApplicationDescription("VaultRDP");
  parser.addHelpOption();
  QCommandLineOption debugOption("debug", "Enable verbose debug logging.");
  parser.addOption(debugOption);
  parser.process(app);

  const bool debugMode = parser.isSet(debugOption);
  app.setProperty("debugMode", debugMode);
  vaultrdp::core::initializeAppLogging(stateDir, debugMode);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    vaultrdp::core::shutdownAppLogging();
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
