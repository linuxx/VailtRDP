#include "ui/NewCredentialDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

NewCredentialDialog::NewCredentialDialog(QWidget* parent)
    : QDialog(parent),
      tabs_(new QTabWidget(this)),
      nameEdit_(new QLineEdit(this)),
      usernameEdit_(new QLineEdit(this)),
      domainEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      allowAnyFolderCheck_(new QCheckBox("Credential can be used from any folder", this)) {
  setWindowTitle("New Credential Set");
  resize(460, 280);

  passwordEdit_->setEchoMode(QLineEdit::Password);

  auto* generalPage = new QWidget(this);
  auto* form = new QFormLayout(generalPage);
  form->addRow("Credential Name", nameEdit_);
  form->addRow("Username", usernameEdit_);
  form->addRow("Domain", domainEdit_);
  form->addRow("Password", passwordEdit_);
  tabs_->addTab(generalPage, "General");

  auto* advancedPage = new QWidget(this);
  auto* advancedLayout = new QVBoxLayout(advancedPage);
  advancedLayout->addWidget(allowAnyFolderCheck_);
  advancedLayout->addStretch();
  tabs_->addTab(advancedPage, "Advanced");

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);
  layout->addWidget(buttons);
}

void NewCredentialDialog::setDialogTitle(const QString& title) {
  if (!title.trimmed().isEmpty()) {
    setWindowTitle(title);
  }
}

void NewCredentialDialog::setInitialValues(const QString& name, const QString& username, const QString& domain,
                                           const QString& password, bool allowAnyFolder) {
  nameEdit_->setText(name);
  usernameEdit_->setText(username);
  domainEdit_->setText(domain);
  passwordEdit_->setText(password);
  allowAnyFolderCheck_->setChecked(allowAnyFolder);
}

QString NewCredentialDialog::credentialName() const {
  return nameEdit_->text().trimmed();
}

QString NewCredentialDialog::username() const {
  return usernameEdit_->text().trimmed();
}

QString NewCredentialDialog::domain() const {
  return domainEdit_->text().trimmed();
}

QString NewCredentialDialog::password() const {
  return passwordEdit_->text();
}

bool NewCredentialDialog::allowAnyFolder() const {
  return allowAnyFolderCheck_->isChecked();
}
