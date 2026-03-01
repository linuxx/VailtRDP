#pragma once

#include <QDialog>

#include <optional>
#include <utility>
#include <vector>

namespace vaultrdp::model {
enum class GatewayCredentialMode : int;
}

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QTabWidget;

enum class SavedCredentialSource : int {
  EnterCredentials = 0,
  SavedCredentialSet = 1,
};

class NewGatewayDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewGatewayDialog(QWidget* parent = nullptr);

  void setDialogTitle(const QString& title);
  void setCredentialOptions(const std::vector<std::pair<QString, QString>>& credentialOptions);
  void setInitialValues(const QString& name, const QString& host, int port,
                        vaultrdp::model::GatewayCredentialMode mode, const QString& username,
                        const QString& domain, const QString& password,
                        bool allowAnyFolder,
                        const std::optional<QString>& selectedCredentialId = std::nullopt);

  QString gatewayName() const;
  QString host() const;
  int port() const;
  vaultrdp::model::GatewayCredentialMode credentialMode() const;
  QString username() const;
  QString domain() const;
  QString password() const;
  SavedCredentialSource savedCredentialSource() const;
  bool useSavedCredentialSet() const;
  std::optional<QString> selectedCredentialSetId() const;
  bool allowAnyFolder() const;

 private Q_SLOTS:
  void updateCredentialUi();

 private:
  QTabWidget* tabs_;
  QLineEdit* nameEdit_;
  QLineEdit* hostEdit_;
  QSpinBox* portSpin_;
  QComboBox* credentialModeCombo_;
  QComboBox* savedCredentialSourceCombo_;
  QLabel* savedCredentialSourceLabel_;
  QComboBox* savedCredentialSetCombo_;
  QLabel* savedCredentialSetLabel_;
  QLineEdit* usernameEdit_;
  QLabel* usernameLabel_;
  QLineEdit* domainEdit_;
  QLabel* domainLabel_;
  QLineEdit* passwordEdit_;
  QLabel* passwordLabel_;
  QCheckBox* allowAnyFolderCheck_;
  QString cachedUsername_;
  QString cachedDomain_;
  QString cachedPassword_;
};
