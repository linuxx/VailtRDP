#include "ui/NewGatewayDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QCheckBox>
#include <QLabel>
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
      savedCredentialSourceCombo_(new QComboBox(this)),
      savedCredentialSourceLabel_(nullptr),
      savedCredentialSetCombo_(new QComboBox(this)),
      savedCredentialSetLabel_(nullptr),
      usernameEdit_(new QLineEdit(this)),
      usernameLabel_(nullptr),
      domainEdit_(new QLineEdit(this)),
      domainLabel_(nullptr),
      passwordEdit_(new QLineEdit(this)),
      passwordLabel_(nullptr),
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
  savedCredentialSourceCombo_->addItem("Enter Credentials",
                                       static_cast<int>(SavedCredentialSource::EnterCredentials));
  savedCredentialSourceCombo_->addItem("Use Saved Credential Set",
                                       static_cast<int>(SavedCredentialSource::SavedCredentialSet));
  savedCredentialSetCombo_->addItem("Select saved credential...", QVariant());
  passwordEdit_->setEchoMode(QLineEdit::Password);

  auto* generalPage = new QWidget(this);
  auto* form = new QFormLayout(generalPage);
  auto* separatorAfterPort = new QFrame(generalPage);
  separatorAfterPort->setFrameShape(QFrame::HLine);
  separatorAfterPort->setFrameShadow(QFrame::Sunken);
  auto* separatorAfterCredentialSet = new QFrame(generalPage);
  separatorAfterCredentialSet->setFrameShape(QFrame::HLine);
  separatorAfterCredentialSet->setFrameShadow(QFrame::Sunken);
  form->addRow("Gateway Name", nameEdit_);
  form->addRow("Host", hostEdit_);
  form->addRow("Port", portSpin_);
  form->addRow(separatorAfterPort);
  form->addRow("Credentials", credentialModeCombo_);
  form->addRow("Saved Credential Source", savedCredentialSourceCombo_);
  form->addRow("Credential Set", savedCredentialSetCombo_);
  form->addRow(separatorAfterCredentialSet);
  form->addRow("Username", usernameEdit_);
  form->addRow("Domain", domainEdit_);
  form->addRow("Password", passwordEdit_);
  savedCredentialSourceLabel_ = qobject_cast<QLabel*>(form->labelForField(savedCredentialSourceCombo_));
  savedCredentialSetLabel_ = qobject_cast<QLabel*>(form->labelForField(savedCredentialSetCombo_));
  usernameLabel_ = qobject_cast<QLabel*>(form->labelForField(usernameEdit_));
  domainLabel_ = qobject_cast<QLabel*>(form->labelForField(domainEdit_));
  passwordLabel_ = qobject_cast<QLabel*>(form->labelForField(passwordEdit_));
  tabs_->addTab(generalPage, "General");

  auto* advancedPage = new QWidget(this);
  auto* advancedLayout = new QVBoxLayout(advancedPage);
  advancedLayout->addWidget(allowAnyFolderCheck_);
  advancedLayout->addStretch();
  tabs_->addTab(advancedPage, "Advanced");

  connect(credentialModeCombo_, &QComboBox::currentIndexChanged, this, &NewGatewayDialog::updateCredentialUi);
  connect(savedCredentialSourceCombo_, &QComboBox::currentIndexChanged, this, &NewGatewayDialog::updateCredentialUi);
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

void NewGatewayDialog::setCredentialOptions(const std::vector<std::pair<QString, QString>>& credentialOptions) {
  savedCredentialSetCombo_->clear();
  savedCredentialSetCombo_->addItem("Select saved credential...", QVariant());
  for (const auto& [id, name] : credentialOptions) {
    savedCredentialSetCombo_->addItem(name, id);
  }
}

void NewGatewayDialog::setInitialValues(const QString& name, const QString& host, int port,
                                        vaultrdp::model::GatewayCredentialMode mode, const QString& username,
                                        const QString& domain, const QString& password,
                                        bool allowAnyFolder,
                                        const std::optional<QString>& selectedCredentialId) {
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
  if (selectedCredentialId.has_value() && !selectedCredentialId->trimmed().isEmpty()) {
    const int credentialIndex = savedCredentialSetCombo_->findData(selectedCredentialId.value());
    savedCredentialSetCombo_->setCurrentIndex(credentialIndex >= 0 ? credentialIndex : 0);
    savedCredentialSourceCombo_->setCurrentIndex(
        savedCredentialSourceCombo_->findData(static_cast<int>(SavedCredentialSource::SavedCredentialSet)));
  } else {
    savedCredentialSetCombo_->setCurrentIndex(0);
    savedCredentialSourceCombo_->setCurrentIndex(
        savedCredentialSourceCombo_->findData(static_cast<int>(SavedCredentialSource::EnterCredentials)));
  }
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
  return (credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved &&
          !useSavedCredentialSet())
             ? usernameEdit_->text().trimmed()
             : cachedUsername_.trimmed();
}

QString NewGatewayDialog::domain() const {
  return (credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved &&
          !useSavedCredentialSet())
             ? domainEdit_->text().trimmed()
             : cachedDomain_.trimmed();
}

QString NewGatewayDialog::password() const {
  return (credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved &&
          !useSavedCredentialSet())
             ? passwordEdit_->text()
             : cachedPassword_;
}

SavedCredentialSource NewGatewayDialog::savedCredentialSource() const {
  return static_cast<SavedCredentialSource>(savedCredentialSourceCombo_->currentData().toInt());
}

bool NewGatewayDialog::useSavedCredentialSet() const {
  return credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved &&
         savedCredentialSource() == SavedCredentialSource::SavedCredentialSet;
}

std::optional<QString> NewGatewayDialog::selectedCredentialSetId() const {
  if (!useSavedCredentialSet()) {
    return std::nullopt;
  }
  const QVariant data = savedCredentialSetCombo_->currentData();
  if (!data.isValid()) {
    return std::nullopt;
  }
  const QString id = data.toString().trimmed();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

bool NewGatewayDialog::allowAnyFolder() const {
  return allowAnyFolderCheck_->isChecked();
}

void NewGatewayDialog::updateCredentialUi() {
  const bool separateMode = credentialMode() == vaultrdp::model::GatewayCredentialMode::SeparateSaved;
  const bool useSavedSet = separateMode && useSavedCredentialSet();

  savedCredentialSourceCombo_->setEnabled(separateMode);
  if (savedCredentialSourceLabel_ != nullptr) {
    savedCredentialSourceLabel_->setEnabled(separateMode);
  }
  savedCredentialSetCombo_->setEnabled(useSavedSet);
  if (savedCredentialSetLabel_ != nullptr) {
    savedCredentialSetLabel_->setEnabled(useSavedSet);
  }
  if (separateMode && !useSavedSet) {
    usernameEdit_->setEnabled(true);
    domainEdit_->setEnabled(true);
    passwordEdit_->setEnabled(true);
    if (usernameLabel_ != nullptr) {
      usernameLabel_->setEnabled(true);
    }
    if (domainLabel_ != nullptr) {
      domainLabel_->setEnabled(true);
    }
    if (passwordLabel_ != nullptr) {
      passwordLabel_->setEnabled(true);
    }
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
  if (usernameLabel_ != nullptr) {
    usernameLabel_->setEnabled(false);
  }
  if (domainLabel_ != nullptr) {
    domainLabel_->setEnabled(false);
  }
  if (passwordLabel_ != nullptr) {
    passwordLabel_->setEnabled(false);
  }
}
