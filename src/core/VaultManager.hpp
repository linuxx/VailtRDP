#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

class DatabaseManager;

namespace vaultrdp::core {

enum class VaultState {
  Disabled = 0,
  Locked = 1,
  Unlocked = 2,
};

class VaultManager {
 public:
  explicit VaultManager(DatabaseManager* databaseManager);
  ~VaultManager();

  VaultManager(const VaultManager&) = delete;
  VaultManager& operator=(const VaultManager&) = delete;

  bool refreshStateFromDatabase();

  bool enable(const QString& passphrase);
  bool disable();
  bool unlock(const QString& passphrase);
  void lock();
  bool rotatePassphrase(const QString& oldPassphrase, const QString& newPassphrase);

  bool isLocked() const;
  bool isEnabled() const;
  VaultState state() const;
  void clearMasterKeyFromMemory();

  std::optional<QByteArray> encryptSecret(const QByteArray& plaintext) const;
  std::optional<QByteArray> decryptSecret(const QByteArray& encBlob) const;

 private:
  struct KdfParams {
    int memLimitKiB = 65536;
    int opsLimit = 3;
    int alg = 1;
  };

  bool loadKdfParams(QByteArray* saltOut, KdfParams* paramsOut) const;
  bool storeKdfParams(const QByteArray& salt, const KdfParams& params) const;

  std::optional<QByteArray> deriveMasterKey(const QString& passphrase, const QByteArray& salt,
                                            const KdfParams& params) const;

  std::optional<QByteArray> encryptWithKey(const QByteArray& plaintext, const QByteArray& key) const;
  std::optional<QByteArray> decryptWithKey(const QByteArray& encBlob, const QByteArray& key) const;

  bool upsertVerifierBlob(const QByteArray& verifierBlob) const;
  std::optional<QByteArray> loadVerifierBlob() const;

  bool verifyDerivedKey(const QByteArray& key) const;
  bool reencryptAllSecrets(const QByteArray& oldKey, const QByteArray& newKey) const;
  bool encryptAllSecretsWithKey(const QByteArray& key) const;
  bool decryptAllSecretsWithKey(const QByteArray& key) const;

  DatabaseManager* databaseManager_;
  VaultState state_;
  QByteArray masterKey_;
};

}  // namespace vaultrdp::core
