#include "ui/CredentialPromptDialog.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace vaultrdp::ui {

bool promptForCredentials(QWidget* parent, const std::optional<QString>& suggestedUsername,
                          const std::optional<QString>& suggestedDomain, bool forGateway,
                          CredentialPromptResult* resultOut) {
  if (resultOut == nullptr) {
    return false;
  }

  QDialog dialog(parent);
  dialog.setWindowTitle("");
  dialog.setMinimumSize(420, 180);
  dialog.resize(460, 190);

  auto* layout = new QVBoxLayout(&dialog);

  auto* intro = new QLabel(forGateway ? "Enter gateway credentials." : "Enter connection credentials.", &dialog);
  intro->setWordWrap(true);
  auto* usernameEdit = new QLineEdit(&dialog);
  auto* domainEdit = new QLineEdit(&dialog);
  auto* passwordEdit = new QLineEdit(&dialog);
  passwordEdit->setEchoMode(QLineEdit::Password);

  if (suggestedUsername.has_value()) {
    usernameEdit->setText(suggestedUsername.value());
  }
  if (suggestedDomain.has_value()) {
    domainEdit->setText(suggestedDomain.value());
  }

  auto* form = new QFormLayout();
  form->addRow("Username", usernameEdit);
  form->addRow("Domain", domainEdit);
  form->addRow("Password", passwordEdit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText("Retry");
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  layout->addWidget(intro);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }

  const QString usernameText = usernameEdit->text().trimmed();
  const QString domainText = domainEdit->text().trimmed();
  const QString passwordText = passwordEdit->text();

  resultOut->username = usernameText.isEmpty() ? std::nullopt : std::optional<QString>(usernameText);
  resultOut->domain = domainText.isEmpty() ? std::nullopt : std::optional<QString>(domainText);
  resultOut->password = passwordText.isEmpty() ? std::nullopt : std::optional<QString>(passwordText);
  return true;
}

}  // namespace vaultrdp::ui
