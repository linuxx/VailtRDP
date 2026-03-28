#include "ui/NewConnectionDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

NewConnectionDialog::NewConnectionDialog(QWidget* parent)
    : QDialog(parent),
      tabs_(new QTabWidget(this)),
      connectionNameEdit_(new QLineEdit(this)),
      hostEdit_(new QLineEdit(this)),
      portSpin_(new QSpinBox(this)),
      credentialSourceCombo_(new QComboBox(this)),
      credentialSetCombo_(new QComboBox(this)),
      usernameEdit_(new QLineEdit(this)),
      domainEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      enableClipboardCheck_(new QCheckBox("Enable clipboard", this)),
      mapHomeDriveCheck_(new QCheckBox("Map home drive", this)),
      gatewayCombo_(new QComboBox(this)) {
  setWindowTitle("New Connection");
  resize(520, 380);

  portSpin_->setRange(1, 65535);
  portSpin_->setValue(3389);

  passwordEdit_->setEchoMode(QLineEdit::Password);
  credentialSourceCombo_->addItem("Enter Credentials",
                                  static_cast<int>(ConnectionCredentialSource::EnterCredentials));
  credentialSourceCombo_->addItem("Use Saved Credential Set",
                                  static_cast<int>(ConnectionCredentialSource::SavedCredentialSet));
  credentialSourceCombo_->addItem("Prompt Every Time",
                                  static_cast<int>(ConnectionCredentialSource::PromptEveryTime));
  credentialSetCombo_->addItem("Select saved credential...", QVariant());
  enableClipboardCheck_->setChecked(true);
  mapHomeDriveCheck_->setChecked(true);

  auto* generalPage = new QWidget(this);
  auto* form = new QFormLayout(generalPage);
  auto* separatorAfterPort = new QFrame(generalPage);
  separatorAfterPort->setFrameShape(QFrame::HLine);
  separatorAfterPort->setFrameShadow(QFrame::Sunken);
  auto* separatorAfterCredentialSet = new QFrame(generalPage);
  separatorAfterCredentialSet->setFrameShape(QFrame::HLine);
  separatorAfterCredentialSet->setFrameShadow(QFrame::Sunken);
  form->addRow("Connection Name", connectionNameEdit_);
  form->addRow("Host", hostEdit_);
  form->addRow("Port", portSpin_);
  form->addRow(separatorAfterPort);
  form->addRow("Credentials", credentialSourceCombo_);
  form->addRow("Credential Set", credentialSetCombo_);
  form->addRow(separatorAfterCredentialSet);
  form->addRow("Username", usernameEdit_);
  form->addRow("Domain", domainEdit_);
  form->addRow("Password", passwordEdit_);
  tabs_->addTab(generalPage, "General");

  connect(credentialSourceCombo_, &QComboBox::currentIndexChanged, this, [this]() { updateCredentialUi(); });
  updateCredentialUi();

  auto* advancedPage = new QWidget(this);
  auto* advancedLayout = new QVBoxLayout(advancedPage);
  advancedLayout->addWidget(enableClipboardCheck_);
  advancedLayout->addWidget(mapHomeDriveCheck_);
  advancedLayout->addStretch();
  tabs_->addTab(advancedPage, "Advanced");

  auto* gatewayPage = new QWidget(this);
  auto* gatewayForm = new QFormLayout(gatewayPage);
  gatewayCombo_->addItem("None", QVariant());
  gatewayForm->addRow("Gateway", gatewayCombo_);
  tabs_->addTab(gatewayPage, "Gateway");

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);
  layout->addWidget(buttons);
}

void NewConnectionDialog::setDialogTitle(const QString& title) {
  if (!title.trimmed().isEmpty()) {
    setWindowTitle(title);
  }
}

void NewConnectionDialog::setGatewayOptions(const std::vector<std::pair<QString, QString>>& gatewayOptions) {
  gatewayCombo_->clear();
  gatewayCombo_->addItem("None", QVariant());
  for (const auto& [id, name] : gatewayOptions) {
    gatewayCombo_->addItem(name, id);
  }
}

void NewConnectionDialog::setCredentialOptions(const std::vector<std::pair<QString, QString>>& credentialOptions) {
  credentialSetCombo_->clear();
  credentialSetCombo_->addItem("Select saved credential...", QVariant());
  for (const auto& [id, name] : credentialOptions) {
    credentialSetCombo_->addItem(name, id);
  }
}

void NewConnectionDialog::setInitialValues(const QString& connectionName, const QString& host, int port,
                                           const QString& username, const QString& domain,
                                           const QString& password, bool promptEveryTime, bool enableClipboard,
                                           bool mapHomeDrive,
                                           const std::optional<QString>& selectedGatewayId,
                                           const std::optional<QString>& selectedCredentialId) {
  connectionNameEdit_->setText(connectionName);
  hostEdit_->setText(host);
  if (port > 0 && port <= 65535) {
    portSpin_->setValue(port);
  }
  usernameEdit_->setText(username);
  domainEdit_->setText(domain);
  passwordEdit_->setText(password);
  enableClipboardCheck_->setChecked(enableClipboard);
  mapHomeDriveCheck_->setChecked(mapHomeDrive);
  if (selectedGatewayId.has_value() && !selectedGatewayId->trimmed().isEmpty()) {
    const int index = gatewayCombo_->findData(selectedGatewayId.value());
    gatewayCombo_->setCurrentIndex(index >= 0 ? index : 0);
  } else {
    gatewayCombo_->setCurrentIndex(0);
  }

  if (selectedCredentialId.has_value() && !selectedCredentialId->trimmed().isEmpty()) {
    const int credentialIndex = credentialSetCombo_->findData(selectedCredentialId.value());
    credentialSetCombo_->setCurrentIndex(credentialIndex >= 0 ? credentialIndex : 0);
    credentialSourceCombo_->setCurrentIndex(
        credentialSourceCombo_->findData(static_cast<int>(ConnectionCredentialSource::SavedCredentialSet)));
  } else if (promptEveryTime) {
    credentialSetCombo_->setCurrentIndex(0);
    credentialSourceCombo_->setCurrentIndex(
        credentialSourceCombo_->findData(static_cast<int>(ConnectionCredentialSource::PromptEveryTime)));
  } else {
    credentialSetCombo_->setCurrentIndex(0);
    credentialSourceCombo_->setCurrentIndex(
        credentialSourceCombo_->findData(static_cast<int>(ConnectionCredentialSource::EnterCredentials)));
  }
  updateCredentialUi();
}

QString NewConnectionDialog::connectionName() const {
  return connectionNameEdit_->text().trimmed();
}

QString NewConnectionDialog::host() const {
  return hostEdit_->text().trimmed();
}

int NewConnectionDialog::port() const {
  return portSpin_->value();
}

QString NewConnectionDialog::username() const {
  if (useSavedCredentialSet() || promptEveryTime()) {
    return QString();
  }
  return usernameEdit_->text().trimmed();
}

QString NewConnectionDialog::domain() const {
  if (useSavedCredentialSet() || promptEveryTime()) {
    return QString();
  }
  return domainEdit_->text().trimmed();
}

QString NewConnectionDialog::password() const {
  if (useSavedCredentialSet() || promptEveryTime()) {
    return QString();
  }
  return passwordEdit_->text();
}

ConnectionCredentialSource NewConnectionDialog::credentialSource() const {
  return static_cast<ConnectionCredentialSource>(credentialSourceCombo_->currentData().toInt());
}

bool NewConnectionDialog::useSavedCredentialSet() const {
  return credentialSource() == ConnectionCredentialSource::SavedCredentialSet;
}

bool NewConnectionDialog::promptEveryTime() const {
  return credentialSource() == ConnectionCredentialSource::PromptEveryTime;
}

std::optional<QString> NewConnectionDialog::selectedCredentialSetId() const {
  if (!useSavedCredentialSet()) {
    return std::nullopt;
  }
  const QVariant data = credentialSetCombo_->currentData();
  if (!data.isValid()) {
    return std::nullopt;
  }
  const QString id = data.toString().trimmed();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

bool NewConnectionDialog::enableClipboard() const {
  return enableClipboardCheck_->isChecked();
}

bool NewConnectionDialog::mapHomeDrive() const {
  return mapHomeDriveCheck_->isChecked();
}

std::optional<QString> NewConnectionDialog::selectedGatewayId() const {
  const QVariant data = gatewayCombo_->currentData();
  if (!data.isValid()) {
    return std::nullopt;
  }
  const QString id = data.toString().trimmed();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

void NewConnectionDialog::updateCredentialUi() {
  const bool useSaved = credentialSource() == ConnectionCredentialSource::SavedCredentialSet;
  const bool promptEveryTimeMode = credentialSource() == ConnectionCredentialSource::PromptEveryTime;
  credentialSetCombo_->setEnabled(useSaved);
  usernameEdit_->setEnabled(!useSaved && !promptEveryTimeMode);
  domainEdit_->setEnabled(!useSaved && !promptEveryTimeMode);
  passwordEdit_->setEnabled(!useSaved && !promptEveryTimeMode);
}
