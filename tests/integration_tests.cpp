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
#include "core/repository/GatewayRepository.hpp"
#include "core/repository/Repository.hpp"
#include "core/repository/SecretRepository.hpp"
#include "protocols/RdpSession.hpp"
#include "ui/CredentialScope.hpp"
#include "ui/GatewayScope.hpp"

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
                                                               std::nullopt, maybeCredential->id, std::nullopt,
                                                               std::nullopt, std::nullopt, std::nullopt, "{}");
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

bool runConnectionCredentialPolicyIntegration() {
  QTemporaryDir tempDir;
  if (!check(tempDir.isValid(), "temporary test directory (credential policy) created")) {
    return false;
  }

  const QString dbPath = tempDir.filePath("credential-policy.db");
  DatabaseManager dbm(dbPath);
  if (!check(dbm.initialize(), "database initialized (credential policy)")) {
    return false;
  }

  vaultrdp::core::VaultManager vault(&dbm);
  vaultrdp::core::repository::SecretRepository secretRepo(&dbm);
  vaultrdp::core::repository::CredentialRepository credentialRepo(&dbm);
  vaultrdp::core::repository::ConnectionRepository connectionRepo(&dbm);

  const auto maybeSharedSecretId = secretRepo.createPasswordSecret("SharedPass1", &vault);
  if (!check(maybeSharedSecretId.has_value(), "shared credential secret created")) {
    return false;
  }
  const auto maybeSharedCredential = credentialRepo.createCredential(
      "Shared Credential", "shared-user", std::optional<QString>(QStringLiteral("SHARED")),
      maybeSharedSecretId.value());
  if (!check(maybeSharedCredential.has_value(), "shared credential created")) {
    return false;
  }

  const auto sharedConnection = connectionRepo.createConnection(
      "Shared Connection", "shared.local", 3389, std::nullopt, maybeSharedCredential->id, std::nullopt, std::nullopt,
      std::nullopt, std::nullopt, "{\"promptEveryTime\":false}");
  if (!check(sharedConnection.has_value(), "shared-credential connection created")) {
    return false;
  }

  const auto sharedLaunch = connectionRepo.resolveLaunchInfo(sharedConnection->id, &vault);
  if (!check(sharedLaunch.has_value(), "shared-credential launch info resolved")) {
    return false;
  }
  if (!check(sharedLaunch->username.has_value() && sharedLaunch->username.value() == "shared-user",
             "shared credential username resolved")) {
    return false;
  }
  if (!check(sharedLaunch->domain.has_value() && sharedLaunch->domain.value() == "SHARED",
             "shared credential domain resolved")) {
    return false;
  }
  if (!check(sharedLaunch->password.has_value() && sharedLaunch->password.value() == "SharedPass1",
             "shared credential password resolved")) {
    return false;
  }
  if (!check(!sharedLaunch->promptEveryTime, "shared credential policy does not prompt every time")) {
    return false;
  }

  const auto maybeLocalSecretId = secretRepo.createPasswordSecret("LocalPass1", &vault);
  if (!check(maybeLocalSecretId.has_value(), "connection-owned secret created")) {
    return false;
  }
  const auto localConnection = connectionRepo.createConnection(
      "Local Connection", "local.local", 3389, std::nullopt, std::nullopt,
      std::optional<QString>(QStringLiteral("local-user")), std::optional<QString>(QStringLiteral("LOCAL")),
      maybeLocalSecretId, std::nullopt, "{\"promptEveryTime\":false}");
  if (!check(localConnection.has_value(), "connection-owned credential connection created")) {
    return false;
  }

  const auto localLaunch = connectionRepo.resolveLaunchInfo(localConnection->id, &vault);
  if (!check(localLaunch.has_value(), "connection-owned launch info resolved")) {
    return false;
  }
  if (!check(localLaunch->username.has_value() && localLaunch->username.value() == "local-user",
             "connection-owned username resolved")) {
    return false;
  }
  if (!check(localLaunch->domain.has_value() && localLaunch->domain.value() == "LOCAL",
             "connection-owned domain resolved")) {
    return false;
  }
  if (!check(localLaunch->password.has_value() && localLaunch->password.value() == "LocalPass1",
             "connection-owned password resolved")) {
    return false;
  }
  if (!check(!localLaunch->promptEveryTime, "connection-owned credential policy does not prompt every time")) {
    return false;
  }

  const auto maybeOverrideSecretId = secretRepo.createPasswordSecret("OverridePass1", &vault);
  if (!check(maybeOverrideSecretId.has_value(), "override secret created")) {
    return false;
  }
  const auto overrideConnection = connectionRepo.createConnection(
      "Override Connection", "override.local", 3389, std::nullopt, maybeSharedCredential->id,
      std::optional<QString>(QStringLiteral("override-user")), std::optional<QString>(QStringLiteral("OVERRIDE")),
      maybeOverrideSecretId, std::nullopt, "{\"promptEveryTime\":false}");
  if (!check(overrideConnection.has_value(), "override connection created")) {
    return false;
  }

  const auto overrideLaunch = connectionRepo.resolveLaunchInfo(overrideConnection->id, &vault);
  if (!check(overrideLaunch.has_value(), "override launch info resolved")) {
    return false;
  }
  if (!check(overrideLaunch->username.has_value() && overrideLaunch->username.value() == "override-user",
             "connection-owned username overrides shared credential")) {
    return false;
  }
  if (!check(overrideLaunch->domain.has_value() && overrideLaunch->domain.value() == "OVERRIDE",
             "connection-owned domain overrides shared credential")) {
    return false;
  }
  if (!check(overrideLaunch->password.has_value() && overrideLaunch->password.value() == "OverridePass1",
             "connection-owned password overrides shared credential")) {
    return false;
  }

  const auto promptConnection = connectionRepo.createConnection(
      "Prompt Connection", "prompt.local", 3389, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
      std::nullopt, std::nullopt, "{\"promptEveryTime\":true}");
  if (!check(promptConnection.has_value(), "prompt-every-time connection created")) {
    return false;
  }

  const auto promptLaunch = connectionRepo.resolveLaunchInfo(promptConnection->id, &vault);
  if (!check(promptLaunch.has_value(), "prompt-every-time launch info resolved")) {
    return false;
  }
  if (!check(promptLaunch->promptEveryTime, "prompt-every-time policy is surfaced in launch info")) {
    return false;
  }
  if (!check(!promptLaunch->username.has_value() && !promptLaunch->password.has_value(),
             "prompt-every-time connection has no stored primary credentials")) {
    return false;
  }

  QSqlDatabase db = dbm.database();
  QSqlQuery secretExists(db);
  secretExists.prepare("SELECT COUNT(*) FROM secrets WHERE id = ?");
  secretExists.addBindValue(maybeLocalSecretId.value());
  if (!check(secretExists.exec() && secretExists.next() && secretExists.value(0).toInt() == 1,
             "connection-owned secret exists before delete")) {
    return false;
  }

  if (!check(connectionRepo.deleteConnection(localConnection->id), "connection-owned credential connection deleted")) {
    return false;
  }

  QSqlQuery secretRemoved(db);
  secretRemoved.prepare("SELECT COUNT(*) FROM secrets WHERE id = ?");
  secretRemoved.addBindValue(maybeLocalSecretId.value());
  if (!check(secretRemoved.exec() && secretRemoved.next() && secretRemoved.value(0).toInt() == 0,
             "deleting connection removes connection-owned secret")) {
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

bool runRepositoryMoveAndGatewayScopeIntegration() {
  QTemporaryDir tempDir;
  if (!check(tempDir.isValid(), "temporary test directory (move/scope) created")) {
    return false;
  }

  const QString dbPath = tempDir.filePath("movescope.db");
  DatabaseManager dbm(dbPath);
  if (!check(dbm.initialize(), "database initialized (move/scope)")) {
    return false;
  }

  vaultrdp::core::repository::Repository folderRepo(&dbm);
  vaultrdp::core::repository::ConnectionRepository connectionRepo(&dbm);
  vaultrdp::core::repository::CredentialRepository credentialRepo(&dbm);
  vaultrdp::core::repository::SecretRepository secretRepo(&dbm);
  vaultrdp::core::repository::GatewayRepository gatewayRepo(&dbm);
  vaultrdp::core::VaultManager vault(&dbm);

  const auto folderA = folderRepo.createFolder("FolderA", std::nullopt);
  const auto folderB = folderRepo.createFolder("FolderB", std::nullopt);
  const auto folderC = folderRepo.createFolder("FolderC", std::nullopt);
  if (!check(folderA.has_value() && folderB.has_value() && folderC.has_value(), "folders created")) {
    return false;
  }

  const auto connection =
      connectionRepo.createConnection("ConnA", "10.0.0.1", 3389, folderA->id, std::nullopt, std::nullopt,
                                      std::nullopt, std::nullopt, std::nullopt, "{}");
  if (!check(connection.has_value(), "connection created in folderA")) {
    return false;
  }
  if (!check(connectionRepo.moveConnectionToFolder(connection->id, folderB->id), "connection moved to folderB")) {
    return false;
  }
  const auto movedConnection = connectionRepo.findConnectionById(connection->id);
  if (!check(movedConnection.has_value() && movedConnection->folderId == folderB->id,
             "moved connection persisted")) {
    return false;
  }

  const auto gateway =
      gatewayRepo.createGateway("GwA", "gw.local", 443, vaultrdp::model::GatewayCredentialMode::PromptEachTime,
                                std::nullopt, folderA->id, false);
  if (!check(gateway.has_value(), "gateway created in folderA")) {
    return false;
  }
  if (!check(gatewayRepo.moveGatewayToFolder(gateway->id, folderB->id), "gateway moved to folderB")) {
    return false;
  }
  const auto movedGateway = gatewayRepo.findGatewayById(gateway->id);
  if (!check(movedGateway.has_value() && movedGateway->folderId.has_value() &&
                 movedGateway->folderId.value() == folderB->id,
             "moved gateway persisted")) {
    return false;
  }

  const auto maybeSecretId = secretRepo.createPasswordSecret("Pass123A", &vault);
  if (!check(maybeSecretId.has_value(), "credential secret created")) {
    return false;
  }
  const auto credential = credentialRepo.createCredential("CredA", "userA", std::nullopt, maybeSecretId.value(),
                                                          folderA->id, false);
  if (!check(credential.has_value(), "credential created in folderA")) {
    return false;
  }
  if (!check(credentialRepo.moveCredentialToFolder(credential->id, folderB->id), "credential moved to folderB")) {
    return false;
  }
  const auto movedCredential = credentialRepo.findCredentialById(credential->id);
  if (!check(movedCredential.has_value() && movedCredential->folderId.has_value() &&
                 movedCredential->folderId.value() == folderB->id,
             "moved credential persisted")) {
    return false;
  }

  if (!check(folderRepo.moveFolderToParent(folderB->id, folderA->id), "folderB moved under folderA")) {
    return false;
  }
  const auto folders = folderRepo.listFolders();
  bool foundChild = false;
  for (const auto& folder : folders) {
    if (folder.id == folderB->id && folder.parentId.has_value() && folder.parentId.value() == folderA->id) {
      foundChild = true;
      break;
    }
  }
  if (!check(foundChild, "folder hierarchy move persisted")) {
    return false;
  }

  const auto scopedGateway =
      gatewayRepo.createGateway("ScopedGw", "scoped.local", 443, vaultrdp::model::GatewayCredentialMode::PromptEachTime,
                                std::nullopt, folderA->id, false);
  const auto globalGateway =
      gatewayRepo.createGateway("GlobalGw", "global.local", 443, vaultrdp::model::GatewayCredentialMode::PromptEachTime,
                                std::nullopt, folderA->id, true);
  if (!check(scopedGateway.has_value() && globalGateway.has_value(), "scope test gateways created")) {
    return false;
  }

  const auto allGateways = gatewayRepo.listGateways();
  const auto allFoldersForScope = folderRepo.listFolders();
  const auto optionsInFolderB = vaultrdp::ui::gatewayOptionsForFolder(allGateways, folderB->id, allFoldersForScope);
  bool hasScopedInB = false;
  bool hasGlobalInB = false;
  for (const auto& opt : optionsInFolderB) {
    if (opt.first == scopedGateway->id) {
      hasScopedInB = true;
    }
    if (opt.first == globalGateway->id) {
      hasGlobalInB = true;
    }
  }
  if (!check(hasScopedInB && hasGlobalInB, "gateway root-scope filtering behavior correct")) {
    return false;
  }
  const auto optionsInFolderC = vaultrdp::ui::gatewayOptionsForFolder(allGateways, folderC->id, allFoldersForScope);
  bool hasScopedInC = false;
  bool hasGlobalInC = false;
  for (const auto& opt : optionsInFolderC) {
    if (opt.first == scopedGateway->id) {
      hasScopedInC = true;
    }
    if (opt.first == globalGateway->id) {
      hasGlobalInC = true;
    }
  }
  if (!check(!hasScopedInC && hasGlobalInC, "gateway root-scope blocks cross-root scoped references")) {
    return false;
  }

  const auto maybeGlobalSecretId = secretRepo.createPasswordSecret("Pass123B", &vault);
  if (!check(maybeGlobalSecretId.has_value(), "global credential secret created")) {
    return false;
  }
  const auto scopedCredential =
      credentialRepo.createCredential("ScopedCred", "scoped", std::nullopt, maybeSecretId.value(), folderA->id, false);
  const auto globalCredential =
      credentialRepo.createCredential("GlobalCred", "global", std::nullopt, maybeGlobalSecretId.value(), folderA->id,
                                      true);
  if (!check(scopedCredential.has_value() && globalCredential.has_value(),
             "scope test credentials created")) {
    return false;
  }
  const auto allCredentials = credentialRepo.listCredentials();
  const auto credentialOptionsInFolderB =
      vaultrdp::ui::credentialOptionsForFolder(allCredentials, folderB->id, allFoldersForScope);
  bool hasScopedCredentialInB = false;
  bool hasGlobalCredentialInB = false;
  for (const auto& opt : credentialOptionsInFolderB) {
    if (opt.first == scopedCredential->id) {
      hasScopedCredentialInB = true;
    }
    if (opt.first == globalCredential->id) {
      hasGlobalCredentialInB = true;
    }
  }
  if (!check(hasScopedCredentialInB && hasGlobalCredentialInB,
             "credential root-scope filtering behavior correct")) {
    return false;
  }
  const auto credentialOptionsInFolderC =
      vaultrdp::ui::credentialOptionsForFolder(allCredentials, folderC->id, allFoldersForScope);
  bool hasScopedCredentialInC = false;
  bool hasGlobalCredentialInC = false;
  for (const auto& opt : credentialOptionsInFolderC) {
    if (opt.first == scopedCredential->id) {
      hasScopedCredentialInC = true;
    }
    if (opt.first == globalCredential->id) {
      hasGlobalCredentialInC = true;
    }
  }
  if (!check(!hasScopedCredentialInC && hasGlobalCredentialInC,
             "credential root-scope blocks cross-root scoped references")) {
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok = runVaultAndRepoIntegration() && ok;
  ok = runConnectionCredentialPolicyIntegration() && ok;
  ok = runRepositoryMoveAndGatewayScopeIntegration() && ok;
  ok = runSessionLifecycleSmoke() && ok;
  ok = runSessionTeardownStress() && ok;

  return ok ? 0 : 1;
}
