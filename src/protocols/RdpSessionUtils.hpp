#pragma once

#include <freerdp/freerdp.h>
#include <winpr/wtypes.h>

#include <QJsonObject>
#include <QString>
#include <Qt>

namespace vaultrdp::protocols::rdp {

constexpr UINT32 kMaxClipboardPayloadBytes = 8u * 1024u * 1024u;
constexpr qint64 kClipboardFormatListMinIntervalMs = 250;

QString vaultStateDirectory();
QString certificateStorePath();
QString describeLastFreerdpError(rdpContext* context);
QString certificateEndpointKey(const QString& host, UINT16 port, DWORD flags);
QString normalizeCertificateFingerprint(const char* fingerprint, DWORD flags);
QJsonObject loadCertificateStore();
bool saveCertificateStore(const QJsonObject& root);
bool isRemoteLogoffError(UINT32 code);

bool mapQtKeyToRdp(int qtKey, quint32 nativeScanCode, UINT8* code, UINT16* flags);
UINT16 mouseFlagForButton(Qt::MouseButton button);

}  // namespace vaultrdp::protocols::rdp

