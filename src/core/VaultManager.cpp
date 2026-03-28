#include "core/VaultManager.hpp"

#include <argon2.h>
#include <sodium.h>

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <limits>
#include <vector>

#include "core/DatabaseManager.hpp"
#include "core/security/PassphrasePolicy.hpp"

namespace {

constexpr unsigned char kBlobVersion = 1;
constexpr int kMasterKeySize = 32;
constexpr int kSaltSize = 16;
constexpr const char* kVerifierPlaintext = "VaultRDP::Verifier::v1";

}  // namespace

namespace vaultrdp::core {

VaultManager::VaultManager(DatabaseManager* databaseManager)
    : databaseManager_(databaseManager), state_(VaultState::Disabled) {
  if (sodium_init() < 0) {
    qCritical() << "Failed to initialize libsodium";
  }
  refreshStateFromDatabase();
}

VaultManager::~VaultManager() {
  clearMasterKeyFromMemory();
}

bool VaultManager::refreshStateFromDatabase() {
  QByteArray salt;
  KdfParams params;
  if (!loadKdfParams(&salt, &params)) {
    return false;
  }

  if (salt.isEmpty()) {
    clearMasterKeyFromMemory();
    state_ = VaultState::Disabled;
    return true;
  }

  if (masterKey_.isEmpty()) {
    state_ = VaultState::Locked;
  } else {
    state_ = VaultState::Unlocked;
  }

  return true;
}

bool VaultManager::enable(const QString& passphrase) {
  if (!isEnabled()) {
    QStringList violations;
    if (!security::PassphrasePolicy::isValid(passphrase, &violations)) {
      qWarning() << "Passphrase does not satisfy policy:" << violations.join(", ");
      return false;
    }

    QByteArray salt(kSaltSize, Qt::Uninitialized);
    randombytes_buf(salt.data(), salt.size());

    KdfParams params;
    std::optional<QByteArray> maybeKey = deriveMasterKey(passphrase, salt, params);
    if (!maybeKey.has_value()) {
      return false;
    }

    std::optional<QByteArray> maybeVerifier = encryptWithKey(QByteArray(kVerifierPlaintext), maybeKey.value());
    if (!maybeVerifier.has_value()) {
      return false;
    }

    QSqlDatabase db = databaseManager_->database();
    if (!db.transaction()) {
      qCritical() << "Failed to begin enable vault transaction:" << db.lastError().text();
      return false;
    }

    if (!storeKdfParams(salt, params) || !upsertVerifierBlob(maybeVerifier.value()) ||
        !encryptAllSecretsWithKey(maybeKey.value())) {
      db.rollback();
      return false;
    }

    if (!db.commit()) {
      qCritical() << "Failed to commit enable vault transaction:" << db.lastError().text();
      db.rollback();
      return false;
    }

    clearMasterKeyFromMemory();
    masterKey_ = maybeKey.value();
    state_ = VaultState::Unlocked;
    return true;
  }

  qWarning() << "Vault is already enabled";
  return false;
}

bool VaultManager::disable() {
  if (!isEnabled()) {
    state_ = VaultState::Disabled;
    return true;
  }
  if (masterKey_.isEmpty()) {
    qWarning() << "Cannot disable encryption while vault is locked";
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    qCritical() << "Failed to begin disable vault transaction:" << db.lastError().text();
    return false;
  }

  if (!decryptAllSecretsWithKey(masterKey_)) {
    db.rollback();
    return false;
  }

  {
    QSqlQuery clearMeta(db);
    clearMeta.prepare(
        "UPDATE crypto_meta SET kdf_salt = NULL, kdf_memlimit = NULL, kdf_opslimit = NULL, kdf_alg = NULL WHERE id = 1");
    if (!clearMeta.exec()) {
      qCritical() << "Failed to clear crypto_meta:" << clearMeta.lastError().text();
      db.rollback();
      return false;
    }
  }
  {
    QSqlQuery clearVerifier(db);
    clearVerifier.prepare("UPDATE vault_meta SET verifier_blob = NULL WHERE id = 1");
    if (!clearVerifier.exec()) {
      qCritical() << "Failed to clear vault verifier blob:" << clearVerifier.lastError().text();
      db.rollback();
      return false;
    }
  }

  if (!db.commit()) {
    qCritical() << "Failed to commit disable vault transaction:" << db.lastError().text();
    db.rollback();
    return false;
  }

  clearMasterKeyFromMemory();
  state_ = VaultState::Disabled;
  return true;
}

bool VaultManager::unlock(const QString& passphrase) {
  QByteArray salt;
  KdfParams params;
  if (!loadKdfParams(&salt, &params)) {
    return false;
  }
  if (salt.isEmpty()) {
    qWarning() << "Vault is disabled; unlock is not applicable";
    return false;
  }

  std::optional<QByteArray> maybeKey = deriveMasterKey(passphrase, salt, params);
  if (!maybeKey.has_value()) {
    return false;
  }

  if (!verifyDerivedKey(maybeKey.value())) {
    return false;
  }

  clearMasterKeyFromMemory();
  masterKey_ = maybeKey.value();
  state_ = VaultState::Unlocked;
  return true;
}

void VaultManager::lock() {
  clearMasterKeyFromMemory();
  if (isEnabled()) {
    state_ = VaultState::Locked;
  } else {
    state_ = VaultState::Disabled;
  }
}

bool VaultManager::rotatePassphrase(const QString& oldPassphrase, const QString& newPassphrase) {
  if (!isEnabled()) {
    qWarning() << "Cannot rotate passphrase: vault is disabled";
    return false;
  }

  QStringList violations;
  if (!security::PassphrasePolicy::isValid(newPassphrase, &violations)) {
    qWarning() << "New passphrase does not satisfy policy:" << violations.join(", ");
    return false;
  }

  QByteArray salt;
  KdfParams params;
  if (!loadKdfParams(&salt, &params) || salt.isEmpty()) {
    return false;
  }

  std::optional<QByteArray> maybeOldKey = deriveMasterKey(oldPassphrase, salt, params);
  if (!maybeOldKey.has_value() || !verifyDerivedKey(maybeOldKey.value())) {
    qWarning() << "Old passphrase verification failed";
    return false;
  }

  QByteArray newSalt(kSaltSize, Qt::Uninitialized);
  randombytes_buf(newSalt.data(), newSalt.size());
  KdfParams newParams = params;

  std::optional<QByteArray> maybeNewKey = deriveMasterKey(newPassphrase, newSalt, newParams);
  if (!maybeNewKey.has_value()) {
    return false;
  }

  std::optional<QByteArray> maybeVerifier = encryptWithKey(QByteArray(kVerifierPlaintext), maybeNewKey.value());
  if (!maybeVerifier.has_value()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    qCritical() << "Failed to start rotate passphrase transaction:" << db.lastError().text();
    return false;
  }

  if (!reencryptAllSecrets(maybeOldKey.value(), maybeNewKey.value()) || !storeKdfParams(newSalt, newParams) ||
      !upsertVerifierBlob(maybeVerifier.value())) {
    db.rollback();
    return false;
  }

  if (!db.commit()) {
    qCritical() << "Failed to commit rotate passphrase transaction:" << db.lastError().text();
    db.rollback();
    return false;
  }

  clearMasterKeyFromMemory();
  masterKey_ = maybeNewKey.value();
  state_ = VaultState::Unlocked;
  return true;
}

bool VaultManager::isLocked() const {
  return state_ == VaultState::Locked;
}

bool VaultManager::isEnabled() const {
  return state_ == VaultState::Locked || state_ == VaultState::Unlocked;
}

VaultState VaultManager::state() const {
  return state_;
}

void VaultManager::clearMasterKeyFromMemory() {
  if (!masterKey_.isEmpty()) {
    sodium_memzero(masterKey_.data(), static_cast<size_t>(masterKey_.size()));
    masterKey_.clear();
  }
}

std::optional<QByteArray> VaultManager::encryptSecret(const QByteArray& plaintext) const {
  if (!isEnabled()) {
    return plaintext;
  }
  if (masterKey_.isEmpty()) {
    return std::nullopt;
  }
  return encryptWithKey(plaintext, masterKey_);
}

std::optional<QByteArray> VaultManager::decryptSecret(const QByteArray& encBlob) const {
  if (!isEnabled()) {
    return encBlob;
  }
  if (masterKey_.isEmpty()) {
    return std::nullopt;
  }
  return decryptWithKey(encBlob, masterKey_);
}

bool VaultManager::loadKdfParams(QByteArray* saltOut, KdfParams* paramsOut) const {
  QSqlDatabase db = databaseManager_->database();
  if (!db.isOpen()) {
    qCritical() << "Database is not open for loadKdfParams";
    return false;
  }

  QSqlQuery query(db);
  if (!query.exec("SELECT kdf_salt, kdf_memlimit, kdf_opslimit, kdf_alg FROM crypto_meta WHERE id = 1")) {
    qCritical() << "Failed to query crypto_meta:" << query.lastError().text();
    return false;
  }

  if (!query.next()) {
    qCritical() << "crypto_meta row missing";
    return false;
  }

  QByteArray salt;
  if (!query.value(0).isNull()) {
    salt = query.value(0).toByteArray();
  }

  KdfParams params;
  if (!query.value(1).isNull()) {
    params.memLimitKiB = query.value(1).toInt();
  }
  if (!query.value(2).isNull()) {
    params.opsLimit = query.value(2).toInt();
  }
  if (!query.value(3).isNull()) {
    params.alg = query.value(3).toInt();
  }

  if (saltOut != nullptr) {
    *saltOut = salt;
  }
  if (paramsOut != nullptr) {
    *paramsOut = params;
  }

  return true;
}

bool VaultManager::storeKdfParams(const QByteArray& salt, const KdfParams& params) const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare(
      "UPDATE crypto_meta SET kdf_salt = ?, kdf_memlimit = ?, kdf_opslimit = ?, kdf_alg = ? WHERE id = 1");
  query.addBindValue(salt);
  query.addBindValue(params.memLimitKiB);
  query.addBindValue(params.opsLimit);
  query.addBindValue(params.alg);

  if (!query.exec()) {
    qCritical() << "Failed to store KDF params:" << query.lastError().text();
    return false;
  }
  return true;
}

std::optional<QByteArray> VaultManager::deriveMasterKey(const QString& passphrase, const QByteArray& salt,
                                                        const KdfParams& params) const {
  if (salt.size() != kSaltSize) {
    qCritical() << "Invalid salt size for Argon2id";
    return std::nullopt;
  }

  QByteArray passphraseUtf8 = passphrase.toUtf8();
  QByteArray key(kMasterKeySize, Qt::Uninitialized);

  const int rc = argon2id_hash_raw(
      static_cast<uint32_t>(params.opsLimit), static_cast<uint32_t>(params.memLimitKiB), 1,
      passphraseUtf8.constData(), static_cast<size_t>(passphraseUtf8.size()),
      reinterpret_cast<const uint8_t*>(salt.constData()), static_cast<size_t>(salt.size()),
      reinterpret_cast<uint8_t*>(key.data()), static_cast<size_t>(key.size()));

  sodium_memzero(passphraseUtf8.data(), static_cast<size_t>(passphraseUtf8.size()));

  if (rc != ARGON2_OK) {
    qCritical() << "Argon2id derivation failed with code" << rc;
    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
    return std::nullopt;
  }

  return key;
}

std::optional<QByteArray> VaultManager::encryptWithKey(const QByteArray& plaintext, const QByteArray& key) const {
  if (key.size() != kMasterKeySize) {
    return std::nullopt;
  }

  QByteArray nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, Qt::Uninitialized);
  randombytes_buf(nonce.data(), static_cast<size_t>(nonce.size()));

  const size_t ciphertextLen = static_cast<size_t>(plaintext.size()) + crypto_aead_xchacha20poly1305_ietf_ABYTES;
  QByteArray ciphertext(static_cast<int>(ciphertextLen), Qt::Uninitialized);

  unsigned long long written = 0;
  const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
      reinterpret_cast<unsigned char*>(ciphertext.data()), &written,
      reinterpret_cast<const unsigned char*>(plaintext.constData()), static_cast<unsigned long long>(plaintext.size()),
      nullptr, 0, nullptr,
      reinterpret_cast<const unsigned char*>(nonce.constData()),
      reinterpret_cast<const unsigned char*>(key.constData()));

  if (rc != 0) {
    qCritical() << "XChaCha20-Poly1305 encryption failed";
    sodium_memzero(ciphertext.data(), static_cast<size_t>(ciphertext.size()));
    return std::nullopt;
  }

  ciphertext.resize(static_cast<int>(written));

  QByteArray blob;
  blob.reserve(1 + nonce.size() + ciphertext.size());
  blob.append(static_cast<char>(kBlobVersion));
  blob.append(nonce);
  blob.append(ciphertext);
  return blob;
}

std::optional<QByteArray> VaultManager::decryptWithKey(const QByteArray& encBlob, const QByteArray& key) const {
  if (key.size() != kMasterKeySize) {
    return std::nullopt;
  }

  const int nonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
  if (encBlob.size() < 1 + nonceLen + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
    return std::nullopt;
  }

  const unsigned char version = static_cast<unsigned char>(encBlob.at(0));
  if (version != kBlobVersion) {
    return std::nullopt;
  }

  const QByteArray nonce = encBlob.mid(1, nonceLen);
  const QByteArray ciphertext = encBlob.mid(1 + nonceLen);

  QByteArray plaintext(ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, Qt::Uninitialized);
  unsigned long long written = 0;

  const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
      reinterpret_cast<unsigned char*>(plaintext.data()), &written, nullptr,
      reinterpret_cast<const unsigned char*>(ciphertext.constData()), static_cast<unsigned long long>(ciphertext.size()),
      nullptr, 0,
      reinterpret_cast<const unsigned char*>(nonce.constData()),
      reinterpret_cast<const unsigned char*>(key.constData()));

  if (rc != 0) {
    sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
    return std::nullopt;
  }

  plaintext.resize(static_cast<int>(written));
  return plaintext;
}

bool VaultManager::upsertVerifierBlob(const QByteArray& verifierBlob) const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE vault_meta SET verifier_blob = ? WHERE id = 1");
  query.addBindValue(verifierBlob);

  if (!query.exec()) {
    qCritical() << "Failed to update vault verifier blob:" << query.lastError().text();
    return false;
  }

  return true;
}

std::optional<QByteArray> VaultManager::loadVerifierBlob() const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  if (!query.exec("SELECT verifier_blob FROM vault_meta WHERE id = 1")) {
    qCritical() << "Failed to query vault verifier blob:" << query.lastError().text();
    return std::nullopt;
  }

  if (!query.next()) {
    qCritical() << "vault_meta row missing";
    return std::nullopt;
  }

  if (query.value(0).isNull()) {
    return QByteArray();
  }

  return query.value(0).toByteArray();
}

bool VaultManager::verifyDerivedKey(const QByteArray& key) const {
  std::optional<QByteArray> maybeVerifierBlob = loadVerifierBlob();
  if (!maybeVerifierBlob.has_value()) {
    return false;
  }

  if (maybeVerifierBlob->isEmpty()) {
    qWarning() << "Vault verifier blob is empty; vault not initialized properly";
    return false;
  }

  std::optional<QByteArray> maybePlain = decryptWithKey(maybeVerifierBlob.value(), key);
  if (!maybePlain.has_value()) {
    return false;
  }

  return maybePlain.value() == QByteArray(kVerifierPlaintext);
}

bool VaultManager::reencryptAllSecrets(const QByteArray& oldKey, const QByteArray& newKey) const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  if (!query.exec("SELECT id, enc_blob FROM secrets")) {
    qCritical() << "Failed to query secrets for passphrase rotation:" << query.lastError().text();
    return false;
  }

  struct Row {
    QString id;
    QByteArray blob;
  };
  std::vector<Row> rows;

  while (query.next()) {
    rows.push_back(Row{query.value(0).toString(), query.value(1).toByteArray()});
  }

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  for (const Row& row : rows) {
    std::optional<QByteArray> plain = decryptWithKey(row.blob, oldKey);
    if (!plain.has_value()) {
      qCritical() << "Failed to decrypt existing secret during rotation. Secret id:" << row.id;
      return false;
    }

    std::optional<QByteArray> reEncrypted = encryptWithKey(plain.value(), newKey);
    sodium_memzero(plain->data(), static_cast<size_t>(plain->size()));
    if (!reEncrypted.has_value()) {
      qCritical() << "Failed to re-encrypt secret during rotation. Secret id:" << row.id;
      return false;
    }

    QSqlQuery update(db);
    update.prepare("UPDATE secrets SET enc_blob = ?, updated_at = ? WHERE id = ?");
    update.addBindValue(reEncrypted.value());
    update.addBindValue(now);
    update.addBindValue(row.id);

    if (!update.exec()) {
      qCritical() << "Failed to update rotated secret:" << update.lastError().text();
      return false;
    }
  }

  return true;
}

bool VaultManager::encryptAllSecretsWithKey(const QByteArray& key) const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  if (!query.exec("SELECT id, enc_blob FROM secrets")) {
    qCritical() << "Failed to query secrets for encryption:" << query.lastError().text();
    return false;
  }

  struct Row {
    QString id;
    QByteArray blob;
  };
  std::vector<Row> rows;
  while (query.next()) {
    rows.push_back(Row{query.value(0).toString(), query.value(1).toByteArray()});
  }

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  for (const Row& row : rows) {
    std::optional<QByteArray> encrypted = encryptWithKey(row.blob, key);
    if (!encrypted.has_value()) {
      qCritical() << "Failed to encrypt secret while enabling vault. Secret id:" << row.id;
      return false;
    }
    QSqlQuery update(db);
    update.prepare("UPDATE secrets SET enc_blob = ?, updated_at = ? WHERE id = ?");
    update.addBindValue(encrypted.value());
    update.addBindValue(now);
    update.addBindValue(row.id);
    if (!update.exec()) {
      qCritical() << "Failed to update encrypted secret:" << update.lastError().text();
      return false;
    }
  }
  return true;
}

bool VaultManager::decryptAllSecretsWithKey(const QByteArray& key) const {
  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  if (!query.exec("SELECT id, enc_blob FROM secrets")) {
    qCritical() << "Failed to query secrets for decryption:" << query.lastError().text();
    return false;
  }

  struct Row {
    QString id;
    QByteArray blob;
  };
  std::vector<Row> rows;
  while (query.next()) {
    rows.push_back(Row{query.value(0).toString(), query.value(1).toByteArray()});
  }

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  for (const Row& row : rows) {
    std::optional<QByteArray> plain = decryptWithKey(row.blob, key);
    if (!plain.has_value()) {
      qCritical() << "Failed to decrypt secret while disabling vault. Secret id:" << row.id;
      return false;
    }
    QSqlQuery update(db);
    update.prepare("UPDATE secrets SET enc_blob = ?, updated_at = ? WHERE id = ?");
    update.addBindValue(plain.value());
    update.addBindValue(now);
    update.addBindValue(row.id);
    if (!update.exec()) {
      qCritical() << "Failed to update decrypted secret:" << update.lastError().text();
      return false;
    }
  }
  return true;
}

}  // namespace vaultrdp::core
