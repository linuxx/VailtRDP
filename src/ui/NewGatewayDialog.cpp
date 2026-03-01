#include "ui/NewGatewayDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "core/model/Entities.hpp"

NewGatewayDialog::NewGatewayDialog(QWidget* parent)
    : QDialog(parent),
      tabs_(new QTabWidget(this)),
      nameEdit_(new QLineEdit(this)),
      hostEdit_(new QLineEdit(this)),
      portSpin_(new QSpinBox(this)),
      credentialModeCombo_(new QComboBox(this)),
      usernameEdit_(new QLineEdit(this)),
      domainEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      allowAnyFolderCheck_(new QCheckBox("Gateway can be used from any folder", this)) {
  setWindowTitle("New Gateway");
  resize(460, 280);

  portSpin_->setRange(1, 65535);
  portSpin_->setValue(443);
  credentialModeCombo_->addItem("Use Connection Credentials",
                                static_cast<int>(vaultrdp::model::GatewayCredentialMode::SameAsConnection));
  credentialModeCombo_->addItem("Saved Credentials",
                                static_cast<int>(vaultrdp::model::GatewayCredentialMode::SeparateSaved));
  credentialModeCombo_->addItem("Prompt Each Time",
                                static_cast<int>(vaultrdp::model::GatewayCredentialMode::PromptEachTime));
  passwordEdit_->setEchoMode(QLineEdit::Password);

  auto* generalPage = new QWidget(this);
  auto* form = new QFormLayout(generalPage);
  form->addRow("Gateway Name", nameEdit_);
  form->addRow("Host", hostEdit_);
  form->addRow("Port", portSpin_);
  form->addRow("Credentials", credentialModeCombo_);
  form->addRow("Username", usernameEdit_);
  form->addRow("Domain", domainEdit_);
  form->addRow("Password", passwordEdit_);
  tabs_->addTab(generalPage, "General");

  auto* advancedPage = new QWidget(this);
  auto* advancedLayout = new QVBoxLayout(advancedPage);
  advancedLayout->addWidget(allowAnyFolderCheck_);
  advancedLayout->addStretch();
  tabs_->addTab(advancedPage, "Advanced");

  connect(credentialModeCombo_, &QComboBox::currentIndexChanged, this, &NewGatewayDialog::updateCredentialUi);
  updateCredentialUi();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);
  layout->addWidget(buttons);
}

void NewGatewayDialog::setDialogTitle(const QString& title) {
  if (!title.trimmed().isEmpty()) {
    setWindowTitle(title);
  }
}

void NewGatewayDialog::setInitialValues(const QString& name, const QString& host, int port,
                                        vaultrdp::model::GatewayCredentialMode mode, const QString& username,
                                        const QString& domain, const QString& password,
                                        bool allowAnyFolder) {
  nameEdit_->setText(name);
  hostEdit_->setText(host);
  if (port > 0 && port <= 65535) {
    portSpin_->setValue(port);
  }
  cachedUsername_ = username;
  cachedDomain_ = domain;
  cachedPassword_ = password;
  allowAnyFolderCheck_->setChecked(allowAnyFolder);
  usernameEdit_->setText(username);
  domainEdit_->setText(domain);
  passwordEdit_->setText(password);

  const int modeIndex = credentialModeCombo_->findData(static_cast<int>(mode));
  credentialModeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
  updateCredentialUi();
}

QString NewGatewayDialog::gatewayName() const {
  return nameEdit_->text().trimmed();
}

QString NewGatewayDialog::host() const {
  return hostEdit_->text().trimmed();
}

int NewGatewayDialog::port() const {
  return portSpin_->value();
}

vaultrdp::model::GatewayCredentialMode NewGatewayDialog::credentialMode() const {
  return static_cast<vaultrdp::model::GatewayCredentialMode>(
      credentialModeCombo_->currentData().toInt());
}

QString NewGatewayDialog::username() const {
  return credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved ? usernameEdit_->text().trimmed()
                                                                                    : cachedUsername_.trimmed();
}

QString NewGatewayDialog::domain() const {
  return credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved ? domainEdit_->text().trimmed()
                                                                                    : cachedDomain_.trimmed();
}

QString NewGatewayDialog::password() const {
  return credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved ? passwordEdit_->text()
                                                                                    : cachedPassword_;
}

bool NewGatewayDialog::allowAnyFolder() const {
  return allowAnyFolderCheck_->isChecked();
}

void NewGatewayDialog::updateCredentialUi() {
  const bool saved = credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved;
  if (saved) {
    usernameEdit_->setEnabled(true);
    domainEdit_->setEnabled(true);
    passwordEdit_->setEnabled(true);
    usernameEdit_->setText(cachedUsername_);
    domainEdit_->setText(cachedDomain_);
    passwordEdit_->setText(cachedPassword_);
    return;
  }

  cachedUsername_ = usernameEdit_->text();
  cachedDomain_ = domainEdit_->text();
  cachedPassword_ = passwordEdit_->text();
  usernameEdit_->clear();
  domainEdit_->clear();
  passwordEdit_->clear();
  usernameEdit_->setEnabled(false);
  domainEdit_->setEnabled(false);
  passwordEdit_->setEnabled(false);
}
