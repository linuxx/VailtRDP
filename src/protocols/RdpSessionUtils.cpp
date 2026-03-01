#include "protocols/RdpSessionUtils.hpp"

#include <freerdp/error.h>
#include <freerdp/scancode.h>
#include <freerdp/settings.h>
#include <freerdp/settings_keys.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

namespace vaultrdp::protocols::rdp {

QString vaultStateDirectory() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/VaultRDP";
  QDir().mkpath(dir);
  return dir;
}

QString certificateStorePath() {
  return vaultStateDirectory() + "/known_hosts.json";
}

QString describeLastFreerdpError(rdpContext* context) {
  if (context == nullptr) {
    return QString("Unknown FreeRDP error");
  }

  const UINT32 code = freerdp_get_last_error(context);
  const char* name = freerdp_get_last_error_name(code);
  const char* desc = freerdp_get_last_error_string(code);
  const char* category = freerdp_get_last_error_category(code);
  const QString codeText = QString::number(code);
  const QString nameText = (name != nullptr) ? QString::fromUtf8(name) : QString("unknown");
  const QString descText = (desc != nullptr) ? QString::fromUtf8(desc) : QString("no description");
  const QString categoryText = (category != nullptr) ? QString::fromUtf8(category) : QString();
  const bool gatewayEnabled =
      context->settings != nullptr &&
      freerdp_settings_get_bool(context->settings, FreeRDP_GatewayEnabled);
  const bool isGatewayCategory = categoryText.compare("gateway", Qt::CaseInsensitive) == 0;

  if (isGatewayCategory &&
      (code == FREERDP_ERROR_CONNECT_ACCESS_DENIED || code == FREERDP_ERROR_CONNECT_LOGON_FAILURE ||
       code == FREERDP_ERROR_CONNECT_WRONG_PASSWORD || code == FREERDP_ERROR_AUTHENTICATION_FAILED ||
       code == FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS)) {
    return QString("Gateway authentication failed. Check gateway username, password, and domain. (%1: %2 - %3)")
        .arg(codeText)
        .arg(nameText)
        .arg(descText);
  }

  if (isGatewayCategory || gatewayEnabled) {
    if (code == FREERDP_ERROR_CONNECT_TRANSPORT_FAILED ||
        code == FREERDP_ERROR_CONNECT_ACCESS_DENIED) {
      return QString("Gateway connection failed. Check gateway host/port, target host, and credentials. (%1: %2 - %3)")
          .arg(codeText)
          .arg(nameText)
          .arg(descText);
    }
  }

  switch (code) {
    case FREERDP_ERROR_AUTHENTICATION_FAILED:
    case FREERDP_ERROR_CONNECT_LOGON_FAILURE:
    case FREERDP_ERROR_CONNECT_WRONG_PASSWORD:
    case FREERDP_ERROR_CONNECT_ACCESS_DENIED:
    case FREERDP_ERROR_CONNECT_ACCOUNT_RESTRICTION:
    case FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT:
    case FREERDP_ERROR_CONNECT_ACCOUNT_EXPIRED:
    case FREERDP_ERROR_CONNECT_LOGON_TYPE_NOT_GRANTED:
    case FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS:
      return QString("Authentication failed. Check username, password, and domain. (%1: %2 - %3)")
          .arg(codeText)
          .arg(nameText)
          .arg(descText);
    case FREERDP_ERROR_TLS_CONNECT_FAILED:
      return QString("TLS handshake or certificate validation failed. Check trusted certificate pin in %4. (%1: %2 - %3)")
          .arg(codeText)
          .arg(nameText)
          .arg(descText)
          .arg(certificateStorePath());
    case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
      return QString("Host not found. (%1: %2 - %3)").arg(codeText).arg(nameText).arg(descText);
    case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
      return QString("Network transport failed. Check host/port, gateway, and firewall. (%1: %2 - %3)")
          .arg(codeText)
          .arg(nameText)
          .arg(descText);
    case FREERDP_ERROR_PRE_CONNECT_FAILED:
      return QString("RDP pre-connect initialization failed. (%1: %2 - %3)")
          .arg(codeText)
          .arg(nameText)
          .arg(descText);
    default:
      break;
  }

  return QString("FreeRDP error (%1): %2 (%3)").arg(codeText).arg(nameText).arg(descText);
}

QString certificateEndpointKey(const QString& host, UINT16 port, DWORD flags) {
  const QString type = (flags & VERIFY_CERT_FLAG_GATEWAY) ? "gateway" : "rdp";
  return QString("%1:%2|%3").arg(host.toLower()).arg(port).arg(type);
}

QString normalizeCertificateFingerprint(const char* fingerprint, DWORD flags) {
  if (fingerprint == nullptr) {
    return QString();
  }
  const QByteArray raw = QByteArray(fingerprint);
  if (raw.isEmpty()) {
    return QString();
  }
  if ((flags & VERIFY_CERT_FLAG_FP_IS_PEM) != 0) {
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex()).toUpper();
  }
  QString normalized = QString::fromUtf8(raw).trimmed().toUpper();
  normalized.remove(':');
  normalized.remove(' ');
  return normalized;
}

QJsonObject loadCertificateStore() {
  QFile file(certificateStorePath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    return {};
  }
  return doc.object();
}

bool saveCertificateStore(const QJsonObject& root) {
  const QString path = certificateStorePath();
  const QString tmp = path + ".tmp";
  {
    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
      return false;
    }
    const QJsonDocument doc(root);
    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0) {
      return false;
    }
    file.flush();
    file.close();
  }
  QFile::remove(path);
  return QFile::rename(tmp, path);
}

bool isRemoteLogoffError(UINT32 code) {
  return code == FREERDP_ERROR_LOGOFF_BY_USER ||
         code == FREERDP_ERROR_RPC_INITIATED_LOGOFF;
}

bool mapQtKeyToRdp(int qtKey, quint32 nativeScanCode, UINT8* code, UINT16* flags) {
  UINT16 scan = 0;
  switch (nativeScanCode) {
    case 28: scan = RDP_SCANCODE_RETURN; break;
    case 96: scan = RDP_SCANCODE_RETURN_KP; break;
    case 29: scan = RDP_SCANCODE_LCONTROL; break;
    case 97: scan = RDP_SCANCODE_RCONTROL; break;
    case 42: scan = RDP_SCANCODE_LSHIFT; break;
    case 54: scan = RDP_SCANCODE_RSHIFT; break;
    case 56: scan = RDP_SCANCODE_LMENU; break;
    case 100: scan = RDP_SCANCODE_RMENU; break;
    case 125: scan = RDP_SCANCODE_LWIN; break;
    case 126: scan = RDP_SCANCODE_RWIN; break;
    default: break;
  }

  if (scan != 0) {
    *code = RDP_SCANCODE_CODE(scan);
    *flags = RDP_SCANCODE_EXTENDED(scan) ? KBD_FLAGS_EXTENDED : 0;
    return true;
  }

  switch (qtKey) {
    case Qt::Key_A: scan = RDP_SCANCODE_KEY_A; break;
    case Qt::Key_B: scan = RDP_SCANCODE_KEY_B; break;
    case Qt::Key_C: scan = RDP_SCANCODE_KEY_C; break;
    case Qt::Key_D: scan = RDP_SCANCODE_KEY_D; break;
    case Qt::Key_E: scan = RDP_SCANCODE_KEY_E; break;
    case Qt::Key_F: scan = RDP_SCANCODE_KEY_F; break;
    case Qt::Key_G: scan = RDP_SCANCODE_KEY_G; break;
    case Qt::Key_H: scan = RDP_SCANCODE_KEY_H; break;
    case Qt::Key_I: scan = RDP_SCANCODE_KEY_I; break;
    case Qt::Key_J: scan = RDP_SCANCODE_KEY_J; break;
    case Qt::Key_K: scan = RDP_SCANCODE_KEY_K; break;
    case Qt::Key_L: scan = RDP_SCANCODE_KEY_L; break;
    case Qt::Key_M: scan = RDP_SCANCODE_KEY_M; break;
    case Qt::Key_N: scan = RDP_SCANCODE_KEY_N; break;
    case Qt::Key_O: scan = RDP_SCANCODE_KEY_O; break;
    case Qt::Key_P: scan = RDP_SCANCODE_KEY_P; break;
    case Qt::Key_Q: scan = RDP_SCANCODE_KEY_Q; break;
    case Qt::Key_R: scan = RDP_SCANCODE_KEY_R; break;
    case Qt::Key_S: scan = RDP_SCANCODE_KEY_S; break;
    case Qt::Key_T: scan = RDP_SCANCODE_KEY_T; break;
    case Qt::Key_U: scan = RDP_SCANCODE_KEY_U; break;
    case Qt::Key_V: scan = RDP_SCANCODE_KEY_V; break;
    case Qt::Key_W: scan = RDP_SCANCODE_KEY_W; break;
    case Qt::Key_X: scan = RDP_SCANCODE_KEY_X; break;
    case Qt::Key_Y: scan = RDP_SCANCODE_KEY_Y; break;
    case Qt::Key_Z: scan = RDP_SCANCODE_KEY_Z; break;
    case Qt::Key_0: scan = RDP_SCANCODE_KEY_0; break;
    case Qt::Key_1: scan = RDP_SCANCODE_KEY_1; break;
    case Qt::Key_2: scan = RDP_SCANCODE_KEY_2; break;
    case Qt::Key_3: scan = RDP_SCANCODE_KEY_3; break;
    case Qt::Key_4: scan = RDP_SCANCODE_KEY_4; break;
    case Qt::Key_5: scan = RDP_SCANCODE_KEY_5; break;
    case Qt::Key_6: scan = RDP_SCANCODE_KEY_6; break;
    case Qt::Key_7: scan = RDP_SCANCODE_KEY_7; break;
    case Qt::Key_8: scan = RDP_SCANCODE_KEY_8; break;
    case Qt::Key_9: scan = RDP_SCANCODE_KEY_9; break;
    case Qt::Key_Return: scan = RDP_SCANCODE_RETURN; break;
    case Qt::Key_Enter: scan = RDP_SCANCODE_RETURN; break;
    case Qt::Key_Backspace: scan = RDP_SCANCODE_BACKSPACE; break;
    case Qt::Key_Tab: scan = RDP_SCANCODE_TAB; break;
    case Qt::Key_Space: scan = RDP_SCANCODE_SPACE; break;
    case Qt::Key_Escape: scan = RDP_SCANCODE_ESCAPE; break;
    case Qt::Key_Control: scan = RDP_SCANCODE_LCONTROL; break;
    case Qt::Key_Shift: scan = RDP_SCANCODE_LSHIFT; break;
    case Qt::Key_Alt: scan = RDP_SCANCODE_LMENU; break;
    case Qt::Key_AltGr: scan = RDP_SCANCODE_RMENU; break;
    case Qt::Key_Meta: scan = RDP_SCANCODE_LWIN; break;
    case Qt::Key_Super_L: scan = RDP_SCANCODE_LWIN; break;
    case Qt::Key_Super_R: scan = RDP_SCANCODE_RWIN; break;
    case Qt::Key_Left: scan = RDP_SCANCODE_LEFT; break;
    case Qt::Key_Right: scan = RDP_SCANCODE_RIGHT; break;
    case Qt::Key_Up: scan = RDP_SCANCODE_UP; break;
    case Qt::Key_Down: scan = RDP_SCANCODE_DOWN; break;
    case Qt::Key_Delete: scan = RDP_SCANCODE_DELETE; break;
    default: return false;
  }

  *code = RDP_SCANCODE_CODE(scan);
  *flags = RDP_SCANCODE_EXTENDED(scan) ? KBD_FLAGS_EXTENDED : 0;
  return true;
}

UINT16 mouseFlagForButton(Qt::MouseButton button) {
  switch (button) {
    case Qt::LeftButton: return PTR_FLAGS_BUTTON1;
    case Qt::RightButton: return PTR_FLAGS_BUTTON2;
    case Qt::MiddleButton: return PTR_FLAGS_BUTTON3;
    default: return 0;
  }
}

}  // namespace vaultrdp::protocols::rdp

