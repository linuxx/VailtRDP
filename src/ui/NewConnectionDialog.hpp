#pragma once

#include <QDialog>

#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTabWidget;

class NewConnectionDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewConnectionDialog(QWidget* parent = nullptr);
  void setDialogTitle(const QString& title);
  void setGatewayOptions(const std::vector<std::pair<QString, QString>>& gatewayOptions);
  void setInitialValues(const QString& connectionName, const QString& host, int port, const QString& username,
                        const QString& domain, const QString& password, bool saveCredential,
                        bool enableClipboard, bool mapHomeDrive,
                        const std::optional<QString>& selectedGatewayId);

  QString connectionName() const;
  QString host() const;
  int port() const;
  QString username() const;
  QString domain() const;
  QString password() const;
  bool saveCredential() const;
  bool enableClipboard() const;
  bool mapHomeDrive() const;
  std::optional<QString> selectedGatewayId() const;

 private:
  QTabWidget* tabs_;
  QLineEdit* connectionNameEdit_;
  QLineEdit* hostEdit_;
  QSpinBox* portSpin_;
  QLineEdit* usernameEdit_;
  QLineEdit* domainEdit_;
  QLineEdit* passwordEdit_;
  QCheckBox* saveCredentialCheck_;
  QCheckBox* enableClipboardCheck_;
  QCheckBox* mapHomeDriveCheck_;
  QComboBox* gatewayCombo_;
};
