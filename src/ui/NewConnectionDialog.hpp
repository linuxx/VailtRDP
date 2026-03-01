#pragma once

#include <QDialog>

#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;

enum class ConnectionCredentialSource : int {
  EnterCredentials = 0,
  SavedCredentialSet = 1,
};

class NewConnectionDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewConnectionDialog(QWidget* parent = nullptr);
  void setDialogTitle(const QString& title);
  void setGatewayOptions(const std::vector<std::pair<QString, QString>>& gatewayOptions);
  void setCredentialOptions(const std::vector<std::pair<QString, QString>>& credentialOptions);
  void setInitialValues(const QString& connectionName, const QString& host, int port, const QString& username,
                        const QString& domain, const QString& password, bool saveCredential,
                        bool enableClipboard, bool mapHomeDrive,
                        const std::optional<QString>& selectedGatewayId,
                        const std::optional<QString>& selectedCredentialId = std::nullopt);

  QString connectionName() const;
  QString host() const;
  int port() const;
  QString username() const;
  QString domain() const;
  QString password() const;
  bool saveCredential() const;
  ConnectionCredentialSource credentialSource() const;
  bool useSavedCredentialSet() const;
  std::optional<QString> selectedCredentialSetId() const;
  bool enableClipboard() const;
  bool mapHomeDrive() const;
  std::optional<QString> selectedGatewayId() const;

 private:
  void updateCredentialUi();

  QTabWidget* tabs_;
  QLineEdit* connectionNameEdit_;
  QLineEdit* hostEdit_;
  QSpinBox* portSpin_;
  QComboBox* credentialSourceCombo_;
  QComboBox* credentialSetCombo_;
  QLabel* credentialSetLabel_;
  QLineEdit* usernameEdit_;
  QLabel* usernameLabel_;
  QLineEdit* domainEdit_;
  QLabel* domainLabel_;
  QLineEdit* passwordEdit_;
  QLabel* passwordLabel_;
  QCheckBox* saveCredentialCheck_;
  QCheckBox* enableClipboardCheck_;
  QCheckBox* mapHomeDriveCheck_;
  QComboBox* gatewayCombo_;
};
