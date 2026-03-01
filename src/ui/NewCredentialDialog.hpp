#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QTabWidget;

class NewCredentialDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewCredentialDialog(QWidget* parent = nullptr);

  void setDialogTitle(const QString& title);
  void setInitialValues(const QString& name, const QString& username, const QString& domain,
                        const QString& password, bool allowAnyFolder);

  QString credentialName() const;
  QString username() const;
  QString domain() const;
  QString password() const;
  bool allowAnyFolder() const;

 private:
  QTabWidget* tabs_;
  QLineEdit* nameEdit_;
  QLineEdit* usernameEdit_;
  QLineEdit* domainEdit_;
  QLineEdit* passwordEdit_;
  QCheckBox* allowAnyFolderCheck_;
};
