#pragma once

#include <QDialog>

namespace vaultrdp::model {
enum class GatewayCredentialMode : int;
}

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QTabWidget;

class NewGatewayDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewGatewayDialog(QWidget* parent = nullptr);

  void setDialogTitle(const QString& title);
  void setInitialValues(const QString& name, const QString& host, int port,
                        vaultrdp::model::GatewayCredentialMode mode, const QString& username,
                        const QString& domain, const QString& password,
                        bool allowAnyFolder);

  QString gatewayName() const;
  QString host() const;
  int port() const;
  vaultrdp::model::GatewayCredentialMode credentialMode() const;
  QString username() const;
  QString domain() const;
  QString password() const;
  bool allowAnyFolder() const;

 private Q_SLOTS:
  void updateCredentialUi();

 private:
  QTabWidget* tabs_;
  QLineEdit* nameEdit_;
  QLineEdit* hostEdit_;
  QSpinBox* portSpin_;
  QComboBox* credentialModeCombo_;
  QLineEdit* usernameEdit_;
  QLineEdit* domainEdit_;
  QLineEdit* passwordEdit_;
  QCheckBox* allowAnyFolderCheck_;
  QString cachedUsername_;
  QString cachedDomain_;
  QString cachedPassword_;
};
