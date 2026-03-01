#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include <optional>

#include "core/DatabaseManager.hpp"
#include "core/VaultManager.hpp"
#include "core/repository/ConnectionRepository.hpp"
#include "core/repository/CredentialRepository.hpp"
#include "core/repository/SecretRepository.hpp"
#include "protocols/RdpSession.hpp"

namespace {

bool check(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << "[test] FAILED:" << message;
    return false;
  }
  qInfo().noquote() << "[test] OK:" << message;
  return true;
}

bool waitForState(vaultrdp::protocols::RdpSession* session, vaultrdp::protocols::SessionState expected,
                  int timeoutMs) {
  if (session == nullptr) {
    return false;
  }
  if (session->state() == expected) {
    return true;
  }

  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(session, &vaultrdp::protocols::ISession::stateChanged, &loop,
                   [&loop, session, expected](vaultrdp::protocols::SessionState) {
                     if (session->state() == expected) {
                       loop.quit();
                     }
                   });
  timeout.start(timeoutMs);
  loop.exec();
  return session->state() == expected;
}

bool runVaultAndRepoIntegration() {
  QTemporaryDir tempDir;
  if (!check(tempDir.isValid(), "temporary test directory created")) {
    return false;
  }

  const QString dbPath = tempDir.filePath("integration.db");
  DatabaseManager dbm(dbPath);
  if (!check(dbm.initialize(), "database initialized")) {
    return false;
  }

  vaultrdp::core::VaultManager vault(&dbm);
  vaultrdp::core::repository::SecretRepository secretRepo(&dbm);
  vaultrdp::core::repository::CredentialRepository credentialRepo(&dbm);
  vaultrdp::core::repository::ConnectionRepository connectionRepo(&dbm);

  if (!check(vault.state() == vaultrdp::core::VaultState::Disabled, "vault starts disabled")) {
    return false;
  }

  const auto maybeSecretId = secretRepo.createPasswordSecret("P@ssw0rd123", &vault);
  if (!check(maybeSecretId.has_value(), "password secret created")) {
    return false;
  }

  const auto maybeCredential = credentialRepo.createCredential(
      "Test Credential", "sysop", std::optional<QString>(QStringLiteral("LAB")), maybeSecretId.value());
  if (!check(maybeCredential.has_value(), "credential created")) {
    return false;
  }

  const auto maybeConnection = connectionRepo.createConnection("Test Connection", "127.0.0.1", 3389,
                                                               std::nullopt, maybeCredential->id, std::nullopt, "{}");
  if (!check(maybeConnection.has_value(), "connection created")) {
    return false;
  }

  const auto launchPlain = connectionRepo.resolveLaunchInfo(maybeConnection->id, &vault);
  if (!check(launchPlain.has_value(), "launch info resolved with vault disabled")) {
    return false;
  }
  if (!check(launchPlain->password.has_value() && launchPlain->password.value() == "P@ssw0rd123",
             "plaintext password resolved while vault disabled")) {
    return false;
  }

  if (!check(vault.enable("StrongPass1"), "vault encryption enabled")) {
    return false;
  }

  const auto launchEncrypted = connectionRepo.resolveLaunchInfo(maybeConnection->id, &vault);
  if (!check(launchEncrypted.has_value(), "launch info resolved with vault enabled/unlocked")) {
    return false;
  }
  if (!check(launchEncrypted->password.has_value() && launchEncrypted->password.value() == "P@ssw0rd123",
             "password decrypts while vault unlocked")) {
    return false;
  }

  vault.lock();
  if (!check(vault.state() == vaultrdp::core::VaultState::Locked, "vault locks")) {
    return false;
  }

  const auto launchLocked = connectionRepo.resolveLaunchInfo(maybeConnection->id, &vault);
  if (!check(launchLocked.has_value(), "launch info resolves while locked")) {
    return false;
  }
  if (!check(!launchLocked->password.has_value(), "password is withheld while vault locked")) {
    return false;
  }

  if (!check(!vault.unlock("Wrong1Pass"), "vault rejects incorrect passphrase")) {
    return false;
  }
  if (!check(vault.unlock("StrongPass1"), "vault unlocks with correct passphrase")) {
    return false;
  }

  const auto maybeGatewaySecretId = secretRepo.createPasswordSecret("GwPass123", &vault);
  if (!check(maybeGatewaySecretId.has_value(), "gateway password secret created")) {
    return false;
  }
  const auto maybeGatewayCredential = credentialRepo.createCredential(
      "Gateway Credential", "gw-user", std::optional<QString>(QStringLiteral("GWLAB")),
      maybeGatewaySecretId.value());
  if (!check(maybeGatewayCredential.has_value(), "gateway credential created")) {
    return false;
  }

  const QString gatewayId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QSqlDatabase db = dbm.database();
  QSqlQuery gatewayInsert(db);
  gatewayInsert.prepare(
      "INSERT INTO gateways (id, name, host, port, credential_mode, credential_id, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, strftime('%s','now'))");
  gatewayInsert.addBindValue(gatewayId);
  gatewayInsert.addBindValue("Lab Gateway");
  gatewayInsert.addBindValue("gw.example.internal");
  gatewayInsert.addBindValue(443);
  gatewayInsert.addBindValue(static_cast<int>(vaultrdp::model::GatewayCredentialMode::SeparateSaved));
  gatewayInsert.addBindValue(maybeGatewayCredential->id);
  if (!check(gatewayInsert.exec(), "gateway inserted")) {
    return false;
  }

  QSqlQuery connectionUpdate(db);
  connectionUpdate.prepare("UPDATE connections SET gateway_id = ? WHERE id = ?");
  connectionUpdate.addBindValue(gatewayId);
  connectionUpdate.addBindValue(maybeConnection->id);
  if (!check(connectionUpdate.exec(), "connection gateway linkage updated")) {
    return false;
  }

  const auto launchWithGateway = connectionRepo.resolveLaunchInfo(maybeConnection->id, &vault);
  if (!check(launchWithGateway.has_value(), "launch info resolves with gateway")) {
    return false;
  }
  if (!check(launchWithGateway->gatewayHost.has_value() &&
                 launchWithGateway->gatewayHost.value() == "gw.example.internal",
             "gateway host resolved")) {
    return false;
  }
  if (!check(launchWithGateway->gatewayPassword.has_value() &&
                 launchWithGateway->gatewayPassword.value() == "GwPass123",
             "gateway password resolved from separate credential")) {
    return false;
  }
  return true;
}

bool runSessionLifecycleSmoke() {
  auto* session = new vaultrdp::protocols::RdpSession(
      "127.0.0.1", 1, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 443, std::nullopt,
      std::nullopt, std::nullopt, false, 1024, 768, false, false, nullptr);
  if (!check(session != nullptr, "session created")) {
    return false;
  }

  session->connectSession();
  const bool reachedTerminal =
      waitForState(session, vaultrdp::protocols::SessionState::Error, 4000) ||
      waitForState(session, vaultrdp::protocols::SessionState::Disconnected, 4000);
  if (!check(reachedTerminal, "session reaches terminal state after failed connect")) {
    delete session;
    return false;
  }

  session->disconnectSession();
  if (!check(session->state() == vaultrdp::protocols::SessionState::Disconnected, "session disconnects cleanly")) {
    delete session;
    return false;
  }

  delete session;
  return check(true, "session object deletes without running thread assertion");
}

bool runSessionTeardownStress() {
  constexpr int kIterations = 8;
  for (int i = 0; i < kIterations; ++i) {
    auto* session = new vaultrdp::protocols::RdpSession(
        "127.0.0.1", 1, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 443, std::nullopt,
        std::nullopt, std::nullopt, false, 1024, 768, false, false, nullptr);
    if (!check(session != nullptr, QString("stress session created #%1").arg(i + 1))) {
      return false;
    }

    session->connectSession();
    const bool terminal =
        waitForState(session, vaultrdp::protocols::SessionState::Error, 2500) ||
        waitForState(session, vaultrdp::protocols::SessionState::Disconnected, 2500);
    if (!check(terminal, QString("stress session reached terminal state #%1").arg(i + 1))) {
      delete session;
      return false;
    }

    session->disconnectSession();
    delete session;
  }
  return check(true, "session teardown stress completed");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok = runVaultAndRepoIntegration() && ok;
  ok = runSessionLifecycleSmoke() && ok;
  ok = runSessionTeardownStress() && ok;

  return ok ? 0 : 1;
}
