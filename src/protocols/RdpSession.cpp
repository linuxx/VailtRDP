#include "protocols/RdpSession.hpp"
#include "protocols/RdpSessionUtils.hpp"

#include <freerdp/freerdp.h>
#include <freerdp/error.h>
#include <freerdp/display.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/client_cliprdr_file.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client.h>
#include <freerdp/scancode.h>
#include <freerdp/settings.h>
#include <freerdp/settings_keys.h>
#include <freerdp/settings_types.h>
#include <freerdp/utils/cliprdr_utils.h>

#include <winpr/user.h>
#include <winpr/clipboard.h>

#include <cstdlib>
#include <cstring>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUuid>
#include <QVector>

namespace {

struct WorkerContext {
  rdpContext context;
};

QMutex gWorkerMapMutex;
QHash<rdpContext*, void*> gWorkerByContext;
QHash<CliprdrClientContext*, void*> gWorkerByCliprdr;
QHash<DispClientContext*, void*> gWorkerByDisp;

}  // namespace

namespace vaultrdp::protocols {

class RdpSessionWorker : public QObject {
  Q_OBJECT

 public:
  RdpSessionWorker(QString host, int port, std::optional<QString> username, std::optional<QString> domain,
                   std::optional<QString> password, std::optional<QString> gatewayHost, int gatewayPort,
                   std::optional<QString> gatewayUsername, std::optional<QString> gatewayDomain,
                   std::optional<QString> gatewayPassword, bool gatewayUseSameCredentials,
                   int initialWidth, int initialHeight,
                   bool enableClipboard, bool mapHomeDrive)
      : host_(std::move(host)),
        port_(port),
        username_(std::move(username)),
        domain_(std::move(domain)),
        password_(std::move(password)),
        gatewayHost_(std::move(gatewayHost)),
        gatewayPort_(gatewayPort),
        gatewayUsername_(std::move(gatewayUsername)),
        gatewayDomain_(std::move(gatewayDomain)),
        gatewayPassword_(std::move(gatewayPassword)),
        gatewayUseSameCredentials_(gatewayUseSameCredentials),
        desiredWidth_(qMax(320, initialWidth)),
        desiredHeight_(qMax(240, initialHeight)),
        enableClipboard_(enableClipboard),
        mapHomeDrive_(mapHomeDrive),
        instance_(nullptr),
        cliprdr_(nullptr),
        disp_(nullptr),
        fileCliprdr_(nullptr),
        clipboardSystem_(ClipboardCreate()),
        fileGroupDescriptorFormatId_(0),
        fileContentsFormatId_(0),
        uriListFormatId_(0),
        stopRequested_(false),
        connected_(false),
        disconnectIssued_(false),
        hasRemoteUnicodeFormat_(false),
        requestedClipboardFormatId_(0),
        remoteFileDescriptorFormatId_(0),
        remoteFileRequestRetryCount_(0),
        lastRemoteFileRequestMs_(0),
        lastRemoteFileAppliedMs_(0),
        remoteFileTransferActive_(false),
        remoteFileTransferStartMs_(0),
        lastRemoteFileProgressLogMs_(0),
        pendingClipboardFormatList_(false),
        lastClipboardFormatListSentMs_(0),
        lastLocalClipboardTextAdvertised_(),
        lastLocalClipboardUrisAdvertised_(),
        connectedSinceMs_(0),
        lastResizeApplyMs_(0),
        displayControlActivated_(false),
        pendingResize_(false),
        pendingResizeWidth_(qMax(320, initialWidth)),
        pendingResizeHeight_(qMax(240, initialHeight)) {}

  ~RdpSessionWorker() override {
    cleanup();
  }

  void enqueueKeyboardEvent(int flags, int code) {
    QMutexLocker lock(&inputMutex_);
    inputQueue_.push_back(InputCommand{InputType::Keyboard, flags, code, 0, 0, {}});
  }

  void enqueueMouseEvent(int flags, int x, int y) {
    QMutexLocker lock(&inputMutex_);
    inputQueue_.push_back(InputCommand{InputType::Mouse, flags, 0, x, y, {}});
  }

  void enqueueResizeRequest(int width, int height) {
    QMutexLocker lock(&inputMutex_);
    pendingResize_ = true;
    pendingResizeWidth_ = qMax(320, width);
    pendingResizeHeight_ = qMax(240, height);
  }

  void enqueueClipboardText(QString text) {
    if (text == localClipboardText_) {
      return;
    }
    qInfo().noquote() << "[cliprdr] enqueue local clipboard text chars=" << text.size();
    QMutexLocker lock(&inputMutex_);
    inputQueue_.push_back(InputCommand{InputType::Clipboard, 0, 0, 0, 0, std::move(text)});
  }

  void enqueueClipboardFileUris(QString uriList) {
    if (!enableClipboard_) {
      return;
    }
    if (uriList == localClipboardFileUris_) {
      return;
    }
    qInfo().noquote() << "[cliprdr] enqueue local clipboard file uri-list chars=" << uriList.size();
    QMutexLocker lock(&inputMutex_);
    inputQueue_.push_back(InputCommand{InputType::ClipboardFiles, 0, 0, 0, 0, std::move(uriList)});
  }

  bool applyInitialMonitorConfig(rdpSettings* settings) {
    if (settings == nullptr) {
      return false;
    }

    rdpMonitor monitor{};
    monitor.x = 0;
    monitor.y = 0;
    monitor.width = qMax(200, desiredWidth_);
    monitor.height = qMax(200, desiredHeight_);
    monitor.is_primary = 1;
    monitor.orig_screen = 0;
    monitor.attributes.desktopScaleFactor = 100;
    monitor.attributes.deviceScaleFactor = 100;
    monitor.attributes.orientation = 0;
    monitor.attributes.physicalWidth = static_cast<UINT32>(monitor.width);
    monitor.attributes.physicalHeight = static_cast<UINT32>(monitor.height);

    return freerdp_settings_set_monitor_def_array_sorted(settings, &monitor, 1) == TRUE;
  }

  void attachCliprdr(CliprdrClientContext* cliprdr) {
    if (cliprdr == nullptr) {
      return;
    }
    if (cliprdr_ == cliprdr) {
      return;
    }

    qInfo().noquote() << "[cliprdr] attach interface";
    cliprdr_ = cliprdr;
    {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByCliprdr.insert(cliprdr, this);
    }
    if (enableClipboard_ && clipboardSystem_ != nullptr && fileGroupDescriptorFormatId_ == 0) {
      fileGroupDescriptorFormatId_ = ClipboardRegisterFormat(clipboardSystem_, "FileGroupDescriptorW");
      fileContentsFormatId_ = ClipboardRegisterFormat(clipboardSystem_, "FileContents");
      uriListFormatId_ = ClipboardRegisterFormat(clipboardSystem_, "text/uri-list");
    }
    if (enableClipboard_ && fileCliprdr_ == nullptr) {
      fileCliprdr_ = cliprdr_file_context_new(this);
      if (fileCliprdr_ != nullptr) {
        cliprdr_file_context_set_locally_available(fileCliprdr_, TRUE);
      }
    }
    if (enableClipboard_ && fileCliprdr_ != nullptr) {
      cliprdr->custom = fileCliprdr_;
      if (!cliprdr_file_context_init(fileCliprdr_, cliprdr)) {
        qWarning().noquote() << "[cliprdr] cliprdr_file_context_init failed";
      } else {
        qInfo().noquote() << "[cliprdr] cliprdr_file_context_init ok";
      }
    }
    cliprdr->MonitorReady = &RdpSessionWorker::cliprdrMonitorReady;
    cliprdr->ServerFormatList = &RdpSessionWorker::cliprdrServerFormatList;
    cliprdr->ServerFormatListResponse = &RdpSessionWorker::cliprdrServerFormatListResponse;
    cliprdr->ServerFormatDataRequest = &RdpSessionWorker::cliprdrServerFormatDataRequest;
    cliprdr->ServerFormatDataResponse = &RdpSessionWorker::cliprdrServerFormatDataResponse;
    cliprdr->ServerCapabilities = &RdpSessionWorker::cliprdrServerCapabilities;
  }

  void attachDisp(DispClientContext* disp) {
    if (disp == nullptr) {
      return;
    }
    if (disp_ == disp) {
      return;
    }

    qInfo().noquote() << "[disp] attach interface";
    disp_ = disp;
    displayControlActivated_ = false;
    {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByDisp.insert(disp, this);
    }
    disp->custom = this;
    disp->DisplayControlCaps = &RdpSessionWorker::dispDisplayControlCaps;
  }

  static BOOL loadChannels(freerdp* instance) {
    if (instance == nullptr || instance->context == nullptr || instance->context->channels == nullptr ||
        instance->context->settings == nullptr) {
      return FALSE;
    }

    qInfo().noquote() << "[cliprdr] LoadChannels begin redirectClipboard="
                      << freerdp_settings_get_bool(instance->context->settings, FreeRDP_RedirectClipboard);

    if (!freerdp_client_load_addins(instance->context->channels, instance->context->settings)) {
      qWarning().noquote() << "[cliprdr] LoadChannels freerdp_client_load_addins failed";
      return FALSE;
    }

    auto* worker = fromContext(instance->context);
    auto* cliprdr = reinterpret_cast<CliprdrClientContext*>(
        freerdp_channels_get_static_channel_interface(instance->context->channels, CLIPRDR_SVC_CHANNEL_NAME));
    auto* disp = reinterpret_cast<DispClientContext*>(
        freerdp_channels_get_static_channel_interface(instance->context->channels, DISP_CHANNEL_NAME));
    qInfo().noquote() << "[cliprdr] LoadChannels cliprdr interface="
                      << (cliprdr != nullptr ? "present" : "missing");
    qInfo().noquote() << "[disp] LoadChannels disp interface=" << (disp != nullptr ? "present" : "missing");
    if (worker != nullptr && cliprdr != nullptr) {
      worker->attachCliprdr(cliprdr);
    }
    if (worker != nullptr && disp != nullptr) {
      worker->attachDisp(disp);
    }
    return TRUE;
  }

  void tryBindCliprdrInterface() {
    if (cliprdr_ != nullptr || instance_ == nullptr || instance_->context == nullptr ||
        instance_->context->channels == nullptr) {
      return;
    }

    auto* cliprdr = reinterpret_cast<CliprdrClientContext*>(
        freerdp_channels_get_static_channel_interface(instance_->context->channels, CLIPRDR_SVC_CHANNEL_NAME));
    if (cliprdr == nullptr) {
      return;
    }

    qInfo().noquote() << "[cliprdr] lazy-bind interface acquired";
    attachCliprdr(cliprdr);
  }

  void tryBindDispInterface() {
    if (disp_ != nullptr || instance_ == nullptr || instance_->context == nullptr || instance_->context->channels == nullptr) {
      return;
    }

    auto* disp = reinterpret_cast<DispClientContext*>(
        freerdp_channels_get_static_channel_interface(instance_->context->channels, DISP_CHANNEL_NAME));
    if (disp == nullptr) {
      return;
    }

    qInfo().noquote() << "[disp] lazy-bind interface acquired";
    attachDisp(disp);
  }

  void requestStopFromAnyThread() {
    if (stopRequested_) {
      return;
    }
    stopRequested_ = true;
    qInfo().noquote() << "[rdp-worker] stop requested";
    if (instance_ != nullptr && instance_->context != nullptr) {
      freerdp_abort_connect_context(instance_->context);
    }
  }

 public Q_SLOTS:
  void start() {
    stopRequested_ = false;
    disconnectIssued_ = false;

    instance_ = freerdp_new();
    if (instance_ == nullptr) {
      Q_EMIT stateChanged(SessionState::Error);
      Q_EMIT errorOccurred("freerdp_new failed");
      return;
    }

    instance_->ContextSize = sizeof(WorkerContext);
    instance_->PreConnect = &RdpSessionWorker::preConnect;
    instance_->PostConnect = &RdpSessionWorker::postConnect;
    instance_->LoadChannels = &RdpSessionWorker::loadChannels;
    instance_->VerifyCertificateEx = &RdpSessionWorker::verifyCertificateEx;
    instance_->VerifyChangedCertificateEx = &RdpSessionWorker::verifyChangedCertificateEx;

    if (!freerdp_context_new(instance_)) {
      Q_EMIT stateChanged(SessionState::Error);
      Q_EMIT errorOccurred("freerdp_context_new failed");
      cleanup();
      return;
    }

    {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByContext.insert(instance_->context, this);
    }

    rdpSettings* settings = instance_->context->settings;
    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, host_.toUtf8().constData());
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, static_cast<UINT32>(port_));
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, FALSE);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, static_cast<UINT32>(desiredWidth_));
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, static_cast<UINT32>(desiredHeight_));
    freerdp_settings_set_bool(settings, FreeRDP_UseMultimon, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_AsyncChannels, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_AsyncUpdate, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NetworkAutoDetect, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_CompressionEnabled, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_DeviceRedirection, TRUE);
    // We explicitly add one drive below, so keep auto-drive redirection off.
    freerdp_settings_set_bool(settings, FreeRDP_RedirectDrives, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, enableClipboard_ ? TRUE : FALSE);

    if (mapHomeDrive_) {
      const QString homeDir = QDir::homePath();
      if (!QDir(homeDir).exists()) {
        qWarning().noquote() << "[rdpdr] home directory missing, skip drive redirection:" << homeDir;
      } else {
        const QByteArray driveName = QByteArrayLiteral("Home");
        const QByteArray drivePath = QDir::toNativeSeparators(homeDir).toUtf8();
        const char* driveParams[] = {"drive", driveName.constData(), drivePath.constData()};
        if (!freerdp_client_add_device_channel(settings, 3, driveParams)) {
          qWarning().noquote() << "[rdpdr] failed to add redirected drive for:" << homeDir;
        } else {
          qInfo().noquote() << "[rdpdr] redirected drive ready as \\\\tsclient\\Home ->" << homeDir;
        }
      }
    } else {
      qInfo().noquote() << "[rdpdr] home drive mapping disabled by user setting";
    }

    const char* dispParams[] = {DISP_CHANNEL_NAME};
    if (!freerdp_client_add_dynamic_channel(settings, 1, dispParams)) {
      qWarning().noquote() << "[disp] failed to request dynamic display control channel";
    } else {
      qInfo().noquote() << "[disp] dynamic display control channel requested";
    }

    if (!applyInitialMonitorConfig(settings)) {
      Q_EMIT errorOccurred("Monitor configuration validation failed.");
    }

    if (instance_->context->channels != nullptr) {
      const UINT providerRc = freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
      if (providerRc != CHANNEL_RC_OK) {
        qWarning().noquote() << "[channels] addin provider registration failed rc=" << providerRc;
        Q_EMIT errorOccurred("Failed to register FreeRDP addin provider.");
      } else {
        qInfo().noquote() << "[channels] addin provider registered; static and dynamic addins enabled";
      }
    }

    if (username_.has_value() && !username_->trimmed().isEmpty()) {
      freerdp_settings_set_string(settings, FreeRDP_Username, username_->toUtf8().constData());
    }
    if (domain_.has_value() && !domain_->trimmed().isEmpty()) {
      freerdp_settings_set_string(settings, FreeRDP_Domain, domain_->toUtf8().constData());
    }
    if (password_.has_value() && !password_->isEmpty()) {
      freerdp_settings_set_string(settings, FreeRDP_Password, password_->toUtf8().constData());
    }
    if (gatewayHost_.has_value() && !gatewayHost_->trimmed().isEmpty()) {
      freerdp_settings_set_bool(settings, FreeRDP_GatewayEnabled, TRUE);
      freerdp_settings_set_uint32(settings, FreeRDP_GatewayUsageMethod, TSC_PROXY_MODE_DIRECT);
      freerdp_settings_set_uint32(settings, FreeRDP_GatewayCredentialsSource, TSC_PROXY_CREDS_MODE_USERPASS);
      // Prefer UDP for better throughput/latency when RD Gateway supports it.
      // FreeRDP will fall back to TCP/HTTP transport when UDP is unavailable.
      freerdp_settings_set_bool(settings, FreeRDP_GatewayUdpTransport, TRUE);
      freerdp_settings_set_bool(settings, FreeRDP_GatewayHttpTransport, TRUE);
      freerdp_settings_set_bool(settings, FreeRDP_GatewayRpcTransport, FALSE);
      freerdp_settings_set_string(settings, FreeRDP_GatewayHostname, gatewayHost_->toUtf8().constData());
      freerdp_settings_set_uint32(settings, FreeRDP_GatewayPort, static_cast<UINT32>(qBound(1, gatewayPort_, 65535)));
      if (gatewayUsername_.has_value() && !gatewayUsername_->trimmed().isEmpty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayUsername, gatewayUsername_->toUtf8().constData());
      }
      if (gatewayDomain_.has_value() && !gatewayDomain_->trimmed().isEmpty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayDomain, gatewayDomain_->toUtf8().constData());
      }
      if (gatewayPassword_.has_value() && !gatewayPassword_->isEmpty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayPassword, gatewayPassword_->toUtf8().constData());
      }
      const bool sameCreds = gatewayUseSameCredentials_;
      freerdp_settings_set_bool(settings, FreeRDP_GatewayUseSameCredentials, sameCreds ? TRUE : FALSE);
      qInfo().noquote() << "[rdp-gateway] enabled host=" << gatewayHost_.value()
                        << "port=" << qBound(1, gatewayPort_, 65535)
                        << "sameCreds=" << sameCreds
                        << "udpPreferred=true";
    } else {
      freerdp_settings_set_bool(settings, FreeRDP_GatewayEnabled, FALSE);
    }

    Q_EMIT stateChanged(SessionState::Connecting);

    if (!freerdp_connect(instance_)) {
      Q_EMIT stateChanged(SessionState::Error);
      Q_EMIT errorOccurred(rdp::describeLastFreerdpError(instance_->context));
      cleanup();
      return;
    }

    connected_ = true;
    connectedSinceMs_ = QDateTime::currentMSecsSinceEpoch();
    lastResizeApplyMs_ = 0;
    Q_EMIT stateChanged(SessionState::Connected);

    while (!stopRequested_ && !freerdp_shall_disconnect_context(instance_->context)) {
      tryBindCliprdrInterface();
      tryBindDispInterface();
      processInputQueue();
      flushPendingClipboardFormatList();
      logRemoteFileTransferProgress();
      if (!freerdp_check_event_handles(instance_->context)) {
        Q_EMIT stateChanged(SessionState::Error);
        Q_EMIT errorOccurred(rdp::describeLastFreerdpError(instance_->context));
        break;
      }
    }

    const UINT32 disconnectCode =
        (instance_ != nullptr && instance_->context != nullptr)
            ? freerdp_get_last_error(instance_->context)
            : FREERDP_ERROR_NONE;
    const bool remoteLogoffDetected = !stopRequested_ && rdp::isRemoteLogoffError(disconnectCode);

    cleanup();
    if (!stopRequested_) {
      if (remoteLogoffDetected) {
        Q_EMIT remoteLogoff();
      }
      Q_EMIT stateChanged(SessionState::Disconnected);
    }
  }

  Q_SIGNALS:
  void stateChanged(vaultrdp::protocols::SessionState state);
  void errorOccurred(const QString& message);
  void frameReady(const QImage& frame);
  void remoteClipboardText(const QString& text);
  void remoteClipboardFileUris(const QString& uriList);
  void remoteLogoff();

 private:
  enum class InputType {
    Keyboard,
    Mouse,
    Resize,
    Clipboard,
    ClipboardFiles,
  };

  struct InputCommand {
    InputType type;
    int flags;
    int code;
    int x;
    int y;
    QString text;
  };

  void processInputQueue() {
    if (instance_ == nullptr || instance_->context == nullptr || instance_->context->input == nullptr) {
      return;
    }

    QVector<InputCommand> batch;
    {
      QMutexLocker lock(&inputMutex_);
      if (inputQueue_.isEmpty() && !pendingResize_) {
        return;
      }
      batch = std::move(inputQueue_);
      inputQueue_.clear();
    }

    bool clipboardTextChanged = false;
    bool clipboardFilesChanged = false;
    QString latestClipboardText = localClipboardText_;
    QString latestClipboardFiles = localClipboardFileUris_;

    for (const InputCommand& cmd : batch) {
      if (cmd.type == InputType::Keyboard) {
        freerdp_input_send_keyboard_event(instance_->context->input, static_cast<UINT16>(cmd.flags),
                                          static_cast<UINT8>(cmd.code));
      } else if (cmd.type == InputType::Mouse) {
        freerdp_input_send_mouse_event(instance_->context->input, static_cast<UINT16>(cmd.flags),
                                       static_cast<UINT16>(cmd.x), static_cast<UINT16>(cmd.y));
      } else if (cmd.type == InputType::Resize) {
        QMutexLocker lock(&inputMutex_);
        pendingResize_ = true;
        pendingResizeWidth_ = qMax(320, cmd.x);
        pendingResizeHeight_ = qMax(240, cmd.y);
      } else if (cmd.type == InputType::Clipboard && connected_) {
        latestClipboardText = cmd.text;
        clipboardTextChanged = true;
      } else if (cmd.type == InputType::ClipboardFiles && connected_) {
        if (!enableClipboard_) {
          continue;
        }
        latestClipboardFiles = cmd.text;
        clipboardFilesChanged = true;
      }
    }

    if (clipboardTextChanged) {
      localClipboardText_ = latestClipboardText;
    }

    if (clipboardFilesChanged) {
      localClipboardFileUris_ = latestClipboardFiles;
      if (fileCliprdr_ != nullptr) {
        const QByteArray data = localClipboardFileUris_.toUtf8();
        if (!cliprdr_file_context_update_client_data(fileCliprdr_, data.constData(),
                                                     static_cast<size_t>(data.size()))) {
          qWarning().noquote() << "[cliprdr] cliprdr_file_context_update_client_data failed";
        } else {
          cliprdr_file_context_notify_new_client_format_list(fileCliprdr_);
        }
      }
    }

    if (clipboardTextChanged || clipboardFilesChanged) {
      requestClipboardFormatListSend();
    }

    if (connected_ && instance_ != nullptr && instance_->context != nullptr && instance_->context->settings != nullptr) {
      int targetWidth = 0;
      int targetHeight = 0;
      {
        QMutexLocker lock(&inputMutex_);
        if (pendingResize_) {
          targetWidth = pendingResizeWidth_;
          targetHeight = pendingResizeHeight_;
        }
      }

      if (targetWidth >= 200 && targetHeight >= 200) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool warmupElapsed = (now - connectedSinceMs_) >= 2000;
        const bool rateLimitElapsed = (now - lastResizeApplyMs_) >= 250;
        if (disp_ != nullptr && displayControlActivated_ && disp_->SendMonitorLayout != nullptr &&
            warmupElapsed && rateLimitElapsed) {
          desiredWidth_ = qMax(200, targetWidth);
          desiredHeight_ = qMax(200, targetHeight);

          freerdp_settings_set_uint32(instance_->context->settings, FreeRDP_DesktopWidth,
                                      static_cast<UINT32>(desiredWidth_));
          freerdp_settings_set_uint32(instance_->context->settings, FreeRDP_DesktopHeight,
                                      static_cast<UINT32>(desiredHeight_));

          DISPLAY_CONTROL_MONITOR_LAYOUT layout{};
          layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
          layout.Left = 0;
          layout.Top = 0;
          layout.Width = static_cast<UINT32>(desiredWidth_);
          layout.Height = static_cast<UINT32>(desiredHeight_);
          layout.Orientation = ORIENTATION_LANDSCAPE;
          layout.DesktopScaleFactor = 100;
          layout.DeviceScaleFactor = 100;
          layout.PhysicalWidth = static_cast<UINT32>(qRound((desiredWidth_ / 75.0) * 25.4));
          layout.PhysicalHeight = static_cast<UINT32>(qRound((desiredHeight_ / 75.0) * 25.4));

          const UINT rc = disp_->SendMonitorLayout(disp_, 1, &layout);
          lastResizeApplyMs_ = now;
          if (rc == CHANNEL_RC_OK) {
            qInfo().noquote() << "[disp] resize sent" << desiredWidth_ << "x" << desiredHeight_;
            QMutexLocker lock(&inputMutex_);
            pendingResize_ = false;
          } else {
            qWarning().noquote() << "[disp] SendMonitorLayout failed rc=" << rc << "for"
                                 << desiredWidth_ << "x" << desiredHeight_;
          }
        }
      }
    }
  }

  void requestClipboardFormatListSend() {
    pendingClipboardFormatList_ = true;
    flushPendingClipboardFormatList();
  }

  void flushPendingClipboardFormatList() {
    if (!pendingClipboardFormatList_) {
      return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - lastClipboardFormatListSentMs_) < rdp::kClipboardFormatListMinIntervalMs) {
      return;
    }
    sendClipboardFormatList();
    pendingClipboardFormatList_ = false;
    lastClipboardFormatListSentMs_ = now;
  }

  void logRemoteFileTransferProgress() {
    if (!remoteFileTransferActive_) {
      return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - lastRemoteFileProgressLogMs_) < 2000) {
      return;
    }
    lastRemoteFileProgressLogMs_ = now;
    const qint64 elapsedMs = qMax<qint64>(0, now - remoteFileTransferStartMs_);
    qInfo().noquote() << "[cliprdr] remote->local file transfer active elapsedMs=" << elapsedMs;
  }

  bool trustCertificate(const QString& host, UINT16 port, DWORD flags, const QString& fingerprint) {
    if (host.trimmed().isEmpty() || fingerprint.trimmed().isEmpty()) {
      return false;
    }
    const QString endpoint = rdp::certificateEndpointKey(host, port, flags);
    QMutexLocker lock(&certificateStoreMutex_);
    QJsonObject root = rdp::loadCertificateStore();
    const QJsonValue existingValue = root.value(endpoint);
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (existingValue.isObject()) {
      const QJsonObject existing = existingValue.toObject();
      const QString existingFingerprint = existing.value("fingerprint").toString().trimmed().toUpper();
      if (existingFingerprint == fingerprint) {
        QJsonObject updated = existing;
        updated.insert("last_seen_utc", nowIso);
        root.insert(endpoint, updated);
        rdp::saveCertificateStore(root);
        return true;
      }
      qWarning().noquote() << "[cert] fingerprint mismatch endpoint=" << endpoint;
      return false;
    }

    QJsonObject entry;
    entry.insert("fingerprint", fingerprint);
    entry.insert("first_seen_utc", nowIso);
    entry.insert("last_seen_utc", nowIso);
    root.insert(endpoint, entry);
    if (!rdp::saveCertificateStore(root)) {
      qWarning().noquote() << "[cert] failed to persist trust store for endpoint=" << endpoint;
      return false;
    }
    qWarning().noquote() << "[cert] TOFU accepted and pinned endpoint=" << endpoint;
    return true;
  }

  static DWORD verifyCertificateEx(freerdp* instance, const char* host, UINT16 port,
                                   const char* common_name, const char* subject, const char* issuer,
                                   const char* fingerprint, DWORD flags) {
    Q_UNUSED(common_name);
    Q_UNUSED(subject);
    Q_UNUSED(issuer);
    if (instance == nullptr || instance->context == nullptr) {
      return 0;
    }
    auto* worker = fromContext(instance->context);
    if (worker == nullptr) {
      return 0;
    }
    const QString hostText = (host != nullptr) ? QString::fromUtf8(host) : QString();
    const QString normalized = rdp::normalizeCertificateFingerprint(fingerprint, flags);
    return worker->trustCertificate(hostText, port, flags, normalized) ? 1 : 0;
  }

  static DWORD verifyChangedCertificateEx(freerdp* instance, const char* host, UINT16 port,
                                          const char* common_name, const char* subject,
                                          const char* issuer, const char* new_fingerprint,
                                          const char* old_subject, const char* old_issuer,
                                          const char* old_fingerprint, DWORD flags) {
    Q_UNUSED(common_name);
    Q_UNUSED(subject);
    Q_UNUSED(issuer);
    Q_UNUSED(old_subject);
    Q_UNUSED(old_issuer);
    Q_UNUSED(old_fingerprint);
    if (instance == nullptr || instance->context == nullptr) {
      return 0;
    }
    auto* worker = fromContext(instance->context);
    if (worker == nullptr) {
      return 0;
    }
    const QString hostText = (host != nullptr) ? QString::fromUtf8(host) : QString();
    const QString normalized = rdp::normalizeCertificateFingerprint(new_fingerprint, flags);
    return worker->trustCertificate(hostText, port, flags, normalized) ? 1 : 0;
  }

  static RdpSessionWorker* fromContext(rdpContext* context) {
    QMutexLocker lock(&gWorkerMapMutex);
    auto it = gWorkerByContext.find(context);
    if (it == gWorkerByContext.end()) {
      return nullptr;
    }
    return reinterpret_cast<RdpSessionWorker*>(it.value());
  }

  static BOOL preConnect(freerdp* instance) {
    Q_UNUSED(instance);
    return TRUE;
  }

  static BOOL postConnect(freerdp* instance) {
    if (instance == nullptr || instance->context == nullptr) {
      return FALSE;
    }

    if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) {
      return FALSE;
    }

    rdpUpdate* update = instance->context->update;
    if (update == nullptr) {
      return FALSE;
    }

    update->BeginPaint = &RdpSessionWorker::beginPaint;
    update->EndPaint = &RdpSessionWorker::endPaint;
    update->DesktopResize = &RdpSessionWorker::desktopResize;
    return TRUE;
  }

  static UINT cliprdrMonitorReady(CliprdrClientContext* context, const CLIPRDR_MONITOR_READY* monitorReady) {
    try {
      Q_UNUSED(monitorReady);
      auto* worker = fromCliprdr(context);
      if (worker == nullptr) {
        qWarning().noquote() << "[cliprdr] MonitorReady but worker not found";
        return CHANNEL_RC_OK;
      }
      qInfo().noquote() << "[cliprdr] MonitorReady";
      worker->sendClipboardCapabilities();
      worker->requestClipboardFormatListSend();
      return CHANNEL_RC_OK;
    }
    catch (...) {
      return CHANNEL_RC_BAD_PROC;
    }
  }

  static UINT cliprdrServerFormatList(CliprdrClientContext* context, const CLIPRDR_FORMAT_LIST* formatList) {
    try {
      auto* worker = fromCliprdr(context);
      if (worker == nullptr || context == nullptr || formatList == nullptr) {
        qWarning().noquote() << "[cliprdr] ServerFormatList invalid args";
        return CHANNEL_RC_OK;
      }
      qInfo().noquote() << "[cliprdr] ServerFormatList numFormats=" << formatList->numFormats;

    worker->hasRemoteUnicodeFormat_ = false;
    UINT32 preferredTextFormatId = 0;
    UINT32 fileDescriptorFormatId = 0;
    for (UINT32 i = 0; i < formatList->numFormats; ++i) {
      const UINT32 formatId = formatList->formats[i].formatId;
      const QString formatName =
          (formatList->formats[i].formatName != nullptr)
              ? QString::fromUtf8(formatList->formats[i].formatName)
              : QString();
      qInfo().noquote() << "[cliprdr] ServerFormatList formatId=" << formatId
                        << "name=" << (formatName.isEmpty() ? QString("(null)") : formatName);
      if (formatId == CF_UNICODETEXT) {
        worker->hasRemoteUnicodeFormat_ = true;
        preferredTextFormatId = CF_UNICODETEXT;
        break;
      }
      if ((formatId == CF_TEXT || formatId == CF_OEMTEXT) && preferredTextFormatId == 0) {
        preferredTextFormatId = formatId;
      }
      if (worker->enableClipboard_ && !formatName.isEmpty() &&
          formatName.compare("FileGroupDescriptorW", Qt::CaseInsensitive) == 0) {
        fileDescriptorFormatId = formatId;
      }
    }

    if (context->ClientFormatListResponse != nullptr) {
      CLIPRDR_FORMAT_LIST_RESPONSE response{};
      response.common.msgType = CB_FORMAT_LIST_RESPONSE;
      response.common.msgFlags = CB_RESPONSE_OK;
      response.common.dataLen = 0;
      context->ClientFormatListResponse(context, &response);
    }

      worker->remoteFileDescriptorFormatId_ = fileDescriptorFormatId;
      worker->remoteFileRequestRetryCount_ = 0;
      const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
      const bool remoteFileCooldownActive =
          (worker->lastRemoteFileRequestMs_ > 0 && (nowMs - worker->lastRemoteFileRequestMs_) < 2500) ||
          (worker->lastRemoteFileAppliedMs_ > 0 && (nowMs - worker->lastRemoteFileAppliedMs_) < 8000);

      if (worker->enableClipboard_ && fileDescriptorFormatId != 0 && worker->fileCliprdr_ != nullptr && !remoteFileCooldownActive) {
        const UINT rc = cliprdr_file_context_notify_new_server_format_list(worker->fileCliprdr_);
        qInfo().noquote() << "[cliprdr] notify_new_server_format_list rc=" << rc;
      } else if (worker->enableClipboard_ && fileDescriptorFormatId != 0 && remoteFileCooldownActive) {
        qInfo().noquote() << "[cliprdr] skipping remote file refresh during cooldown";
      }

      if (preferredTextFormatId != 0 && context->ClientFormatDataRequest != nullptr) {
        CLIPRDR_FORMAT_DATA_REQUEST req{};
        req.requestedFormatId = preferredTextFormatId;
        worker->requestedClipboardFormatId_ = preferredTextFormatId;
        qInfo().noquote() << "[cliprdr] requesting data formatId=" << preferredTextFormatId;
        context->ClientFormatDataRequest(context, &req);
      } else if (worker->enableClipboard_ && fileDescriptorFormatId != 0 && context->ClientFormatDataRequest != nullptr) {
        if (remoteFileCooldownActive) {
          qInfo().noquote() << "[cliprdr] skipping file descriptor request during cooldown";
          return CHANNEL_RC_OK;
        }
        if (worker->fileCliprdr_ != nullptr &&
            !cliprdr_file_context_has_local_support(worker->fileCliprdr_)) {
          qWarning().noquote()
              << "[cliprdr] remote file format advertised but local file clipboard support unavailable";
          return CHANNEL_RC_OK;
        }
        CLIPRDR_FORMAT_DATA_REQUEST req{};
        req.requestedFormatId = fileDescriptorFormatId;
        worker->requestedClipboardFormatId_ = fileDescriptorFormatId;
        worker->remoteFileTransferActive_ = true;
        worker->remoteFileTransferStartMs_ = nowMs;
        worker->lastRemoteFileProgressLogMs_ = 0;
        worker->lastRemoteFileRequestMs_ = nowMs;
        qInfo().noquote() << "[cliprdr] requesting file descriptor formatId=" << fileDescriptorFormatId;
        context->ClientFormatDataRequest(context, &req);
      } else if (fileDescriptorFormatId != 0 && !worker->enableClipboard_) {
        qInfo().noquote() << "[cliprdr] remote advertised file clipboard; disabled in this build (use mapped drive)";
      } else {
        qWarning().noquote() << "[cliprdr] no preferred text format available from server";
      }
      return CHANNEL_RC_OK;
    }
    catch (...) {
      return CHANNEL_RC_BAD_PROC;
    }
  }

  static UINT cliprdrServerFormatListResponse(CliprdrClientContext* context,
                                              const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse) {
    Q_UNUSED(context);
    Q_UNUSED(formatListResponse);
    return CHANNEL_RC_OK;
  }

  static UINT cliprdrServerCapabilities(CliprdrClientContext* context,
                                        const CLIPRDR_CAPABILITIES* capabilities) {
    auto* worker = fromCliprdr(context);
    if (worker == nullptr || capabilities == nullptr) {
      return CHANNEL_RC_OK;
    }

    UINT32 remoteFlags = 0;
    for (UINT16 i = 0; i < capabilities->cCapabilitiesSets; ++i) {
      const auto* set = &capabilities->capabilitySets[i];
      if (set->capabilitySetType != CB_CAPSTYPE_GENERAL) {
        continue;
      }
      if (set->capabilitySetLength < sizeof(CLIPRDR_GENERAL_CAPABILITY_SET)) {
        continue;
      }
      const auto* general = reinterpret_cast<const CLIPRDR_GENERAL_CAPABILITY_SET*>(set);
      remoteFlags = general->generalFlags;
      break;
    }

    if (worker->enableClipboard_ && worker->fileCliprdr_ != nullptr) {
      cliprdr_file_context_remote_set_flags(worker->fileCliprdr_, remoteFlags);
    }
    qInfo().noquote() << "[cliprdr] ServerCapabilities remoteFlags=0x"
                      << QString::number(remoteFlags, 16);
    return CHANNEL_RC_OK;
  }

  static UINT cliprdrServerFormatDataRequest(CliprdrClientContext* context,
                                             const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest) {
    try {
      auto* worker = fromCliprdr(context);
      if (worker == nullptr || context == nullptr || context->ClientFormatDataResponse == nullptr ||
          formatDataRequest == nullptr) {
        qWarning().noquote() << "[cliprdr] ServerFormatDataRequest invalid args";
        return CHANNEL_RC_OK;
      }
      qInfo().noquote() << "[cliprdr] ServerFormatDataRequest requestedFormatId="
                        << formatDataRequest->requestedFormatId;

    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    if (formatDataRequest->requestedFormatId == worker->fileGroupDescriptorFormatId_) {
      if (worker->clipboardSystem_ == nullptr || worker->localClipboardFileUris_.trimmed().isEmpty()) {
        response.common.msgFlags = CB_RESPONSE_FAIL;
        response.common.dataLen = 0;
        response.requestedFormatData = nullptr;
        context->ClientFormatDataResponse(context, &response);
        return CHANNEL_RC_OK;
      }

      const QByteArray uriList = worker->localClipboardFileUris_.toUtf8();
      if (worker->fileCliprdr_ != nullptr) {
        if (!cliprdr_file_context_update_client_data(worker->fileCliprdr_, uriList.constData(),
                                                     static_cast<size_t>(uriList.size()))) {
          response.common.msgFlags = CB_RESPONSE_FAIL;
          response.common.dataLen = 0;
          response.requestedFormatData = nullptr;
          context->ClientFormatDataResponse(context, &response);
          return CHANNEL_RC_OK;
        }
      }
      if (!ClipboardSetData(worker->clipboardSystem_, worker->uriListFormatId_, uriList.constData(),
                            static_cast<UINT32>(uriList.size()))) {
        response.common.msgFlags = CB_RESPONSE_FAIL;
        response.common.dataLen = 0;
        response.requestedFormatData = nullptr;
        context->ClientFormatDataResponse(context, &response);
        return CHANNEL_RC_OK;
      }

      UINT32 descriptorBytes = 0;
      void* descriptorData =
          ClipboardGetData(worker->clipboardSystem_, worker->fileGroupDescriptorFormatId_, &descriptorBytes);
      if (descriptorData == nullptr || descriptorBytes < sizeof(FILEDESCRIPTORW)) {
        std::free(descriptorData);
        response.common.msgFlags = CB_RESPONSE_FAIL;
        response.common.dataLen = 0;
        response.requestedFormatData = nullptr;
        context->ClientFormatDataResponse(context, &response);
        return CHANNEL_RC_OK;
      }

      BYTE* serialized = nullptr;
      UINT32 serializedSize = 0;
      const UINT32 remoteFlags =
          (worker->fileCliprdr_ != nullptr) ? cliprdr_file_context_remote_get_flags(worker->fileCliprdr_) : 0;
      const UINT rc = cliprdr_serialize_file_list_ex(
          remoteFlags, reinterpret_cast<const FILEDESCRIPTORW*>(descriptorData),
          descriptorBytes / static_cast<UINT32>(sizeof(FILEDESCRIPTORW)), &serialized, &serializedSize);
      std::free(descriptorData);
      if (rc != CHANNEL_RC_OK || serialized == nullptr || serializedSize == 0) {
        std::free(serialized);
        response.common.msgFlags = CB_RESPONSE_FAIL;
        response.common.dataLen = 0;
        response.requestedFormatData = nullptr;
        context->ClientFormatDataResponse(context, &response);
        return CHANNEL_RC_OK;
      }

      response.common.msgFlags = CB_RESPONSE_OK;
      response.common.dataLen = serializedSize;
      response.requestedFormatData = serialized;
      context->ClientFormatDataResponse(context, &response);
      std::free(serialized);
      qInfo().noquote() << "[cliprdr] sent FileGroupDescriptorW bytes=" << serializedSize;
      return CHANNEL_RC_OK;
    }

    if (formatDataRequest->requestedFormatId != CF_UNICODETEXT &&
        formatDataRequest->requestedFormatId != CF_TEXT &&
        formatDataRequest->requestedFormatId != CF_OEMTEXT) {
      response.common.msgFlags = CB_RESPONSE_FAIL;
      response.common.dataLen = 0;
      response.requestedFormatData = nullptr;
      context->ClientFormatDataResponse(context, &response);
      return CHANNEL_RC_OK;
    }

      const QByteArray payload = (formatDataRequest->requestedFormatId == CF_UNICODETEXT)
                                     ? worker->toClipboardUtf16(worker->localClipboardText_)
                                     : worker->toClipboardAnsi(worker->localClipboardText_);
      qInfo().noquote() << "[cliprdr] responding with bytes=" << payload.size();
      response.common.msgFlags = CB_RESPONSE_OK;
      response.common.dataLen = static_cast<UINT32>(payload.size());
      response.requestedFormatData = reinterpret_cast<const BYTE*>(payload.constData());
      context->ClientFormatDataResponse(context, &response);
      return CHANNEL_RC_OK;
    }
    catch (...) {
      return CHANNEL_RC_BAD_PROC;
    }
  }

  static UINT cliprdrServerFormatDataResponse(CliprdrClientContext* context,
                                              const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse) {
    try {
      auto* worker = fromCliprdr(context);
      if (worker == nullptr || formatDataResponse == nullptr) {
        qWarning().noquote() << "[cliprdr] ServerFormatDataResponse invalid args";
        return CHANNEL_RC_OK;
      }
      qInfo().noquote() << "[cliprdr] ServerFormatDataResponse flags=" << formatDataResponse->common.msgFlags
                        << "len=" << formatDataResponse->common.dataLen
                        << "requestedFormatId(last)=" << worker->requestedClipboardFormatId_;

        if ((formatDataResponse->common.msgFlags & CB_RESPONSE_FAIL) != 0) {
        if (worker->remoteFileDescriptorFormatId_ != 0 &&
            worker->requestedClipboardFormatId_ == worker->remoteFileDescriptorFormatId_ &&
            context->ClientFormatDataRequest != nullptr &&
            worker->remoteFileRequestRetryCount_ < 2) {
          CLIPRDR_FORMAT_DATA_REQUEST req{};
          req.requestedFormatId = worker->remoteFileDescriptorFormatId_;
          worker->requestedClipboardFormatId_ = worker->remoteFileDescriptorFormatId_;
          worker->lastRemoteFileRequestMs_ = QDateTime::currentMSecsSinceEpoch();
          ++worker->remoteFileRequestRetryCount_;
          qWarning().noquote() << "[cliprdr] file descriptor request failed; retry"
                               << worker->remoteFileRequestRetryCount_;
          context->ClientFormatDataRequest(context, &req);
        }
        worker->remoteFileTransferActive_ = false;
        return CHANNEL_RC_OK;
      }

      if (formatDataResponse->requestedFormatData == nullptr || formatDataResponse->common.dataLen < 2 ||
          formatDataResponse->common.dataLen > rdp::kMaxClipboardPayloadBytes) {
        return CHANNEL_RC_OK;
      }

      if (worker->enableClipboard_ && worker->remoteFileDescriptorFormatId_ != 0 &&
          worker->requestedClipboardFormatId_ == worker->remoteFileDescriptorFormatId_) {
        if (worker->fileCliprdr_ == nullptr || worker->clipboardSystem_ == nullptr) {
          worker->requestedClipboardFormatId_ = 0;
          return CHANNEL_RC_OK;
        }
        if (!cliprdr_file_context_has_local_support(worker->fileCliprdr_)) {
          qWarning().noquote() << "[cliprdr] ignoring remote file descriptor response; local support unavailable";
          worker->requestedClipboardFormatId_ = 0;
          return CHANNEL_RC_OK;
        }

        if (!cliprdr_file_context_update_server_data(worker->fileCliprdr_, worker->clipboardSystem_,
                                                     formatDataResponse->requestedFormatData,
                                                     formatDataResponse->common.dataLen)) {
          qWarning().noquote() << "[cliprdr] update_server_data failed";
          worker->requestedClipboardFormatId_ = 0;
          return CHANNEL_RC_OK;
        }

        // Register the received descriptor blob in the local WinPR clipboard so
        // built-in synthesizers can derive text/uri-list for Qt.
        if (!ClipboardSetData(worker->clipboardSystem_, worker->fileGroupDescriptorFormatId_,
                              formatDataResponse->requestedFormatData,
                              formatDataResponse->common.dataLen)) {
          qWarning().noquote() << "[cliprdr] ClipboardSetData(FileGroupDescriptorW) failed";
          worker->requestedClipboardFormatId_ = 0;
          return CHANNEL_RC_OK;
        }

        UINT32 uriSize = 0;
        void* uriData = ClipboardGetData(worker->clipboardSystem_, worker->uriListFormatId_, &uriSize);
        if (uriData != nullptr && uriSize > 0) {
          QString uriList = QString::fromUtf8(static_cast<const char*>(uriData), static_cast<int>(uriSize));
          while (!uriList.isEmpty() && uriList.back().unicode() == 0) {
            uriList.chop(1);
          }
          qInfo().noquote() << "[cliprdr] decoded remote file uri-list chars=" << uriList.size();
          worker->lastRemoteFileAppliedMs_ = QDateTime::currentMSecsSinceEpoch();
          Q_EMIT worker->remoteClipboardFileUris(uriList);
        }
        std::free(uriData);
        worker->remoteFileTransferActive_ = false;
        worker->requestedClipboardFormatId_ = 0;
        return CHANNEL_RC_OK;
      }

    QString remoteText;
    if (worker->requestedClipboardFormatId_ == CF_UNICODETEXT || worker->requestedClipboardFormatId_ == 0) {
      remoteText = worker->fromClipboardUtf16(formatDataResponse->requestedFormatData,
                                              formatDataResponse->common.dataLen);
    } else {
      remoteText = worker->fromClipboardAnsi(formatDataResponse->requestedFormatData,
                                             formatDataResponse->common.dataLen);
    }
    worker->requestedClipboardFormatId_ = 0;
    if (remoteText.isNull()) {
      return CHANNEL_RC_OK;
    }

      worker->localClipboardText_ = remoteText;
      worker->remoteFileTransferActive_ = false;
      qInfo().noquote() << "[cliprdr] decoded remote clipboard chars=" << remoteText.size();
      Q_EMIT worker->remoteClipboardText(remoteText);
      return CHANNEL_RC_OK;
    }
    catch (...) {
      return CHANNEL_RC_BAD_PROC;
    }
  }

  static BOOL beginPaint(rdpContext* context) {
    Q_UNUSED(context);
    return TRUE;
  }

  static BOOL endPaint(rdpContext* context) {
    auto* worker = fromContext(context);
    if (worker == nullptr || context == nullptr || context->gdi == nullptr || context->gdi->primary_buffer == nullptr) {
      return FALSE;
    }

    const int width = context->gdi->width;
    const int height = context->gdi->height;
    const int stride = static_cast<int>(context->gdi->stride);

    QImage frame(reinterpret_cast<const uchar*>(context->gdi->primary_buffer), width, height, stride,
                 QImage::Format_ARGB32);
    Q_EMIT worker->frameReady(frame.copy());
    return TRUE;
  }

  static BOOL desktopResize(rdpContext* context) {
    auto* worker = fromContext(context);
    if (worker == nullptr || context == nullptr || context->gdi == nullptr || context->gdi->primary_buffer == nullptr) {
      return FALSE;
    }

    const int width = context->gdi->width;
    const int height = context->gdi->height;
    const int stride = static_cast<int>(context->gdi->stride);

    QImage frame(reinterpret_cast<const uchar*>(context->gdi->primary_buffer), width, height, stride,
                 QImage::Format_ARGB32);
    Q_EMIT worker->frameReady(frame.copy());
    return TRUE;
  }

  static RdpSessionWorker* fromCliprdr(CliprdrClientContext* cliprdr) {
    if (cliprdr == nullptr) {
      return nullptr;
    }
    QMutexLocker lock(&gWorkerMapMutex);
    auto it = gWorkerByCliprdr.find(cliprdr);
    if (it == gWorkerByCliprdr.end()) {
      return nullptr;
    }
    return reinterpret_cast<RdpSessionWorker*>(it.value());
  }

  static UINT dispDisplayControlCaps(DispClientContext* context, UINT32 maxNumMonitors,
                                     UINT32 maxMonitorAreaFactorA, UINT32 maxMonitorAreaFactorB) {
    auto* worker = fromDisp(context);
    if (worker == nullptr) {
      return CHANNEL_RC_OK;
    }
    worker->displayControlActivated_ = true;
    qInfo().noquote() << "[disp] DisplayControlCaps maxMonitors=" << maxNumMonitors
                      << "areaFactorA=" << maxMonitorAreaFactorA
                      << "areaFactorB=" << maxMonitorAreaFactorB;
    return CHANNEL_RC_OK;
  }

  static RdpSessionWorker* fromDisp(DispClientContext* disp) {
    if (disp == nullptr) {
      return nullptr;
    }
    QMutexLocker lock(&gWorkerMapMutex);
    auto it = gWorkerByDisp.find(disp);
    if (it == gWorkerByDisp.end()) {
      return nullptr;
    }
    return reinterpret_cast<RdpSessionWorker*>(it.value());
  }

  QByteArray toClipboardUtf16(const QString& text) const {
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace("\n", "\r\n");

    QByteArray utf16;
    utf16.resize((normalized.size() + 1) * int(sizeof(char16_t)));
    std::memcpy(utf16.data(), normalized.utf16(), normalized.size() * int(sizeof(char16_t)));
    utf16[utf16.size() - 2] = 0;
    utf16[utf16.size() - 1] = 0;
    return utf16;
  }

  QByteArray toClipboardAnsi(const QString& text) const {
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace("\n", "\r\n");

    QByteArray ansi = normalized.toLocal8Bit();
    ansi.append('\0');
    return ansi;
  }

  QString fromClipboardUtf16(const BYTE* data, UINT32 size) const {
    if (data == nullptr || size < 2 || size > rdp::kMaxClipboardPayloadBytes) {
      return QString();
    }

    const int codeUnits = int(size / sizeof(char16_t));
    QString text = QString::fromUtf16(reinterpret_cast<const char16_t*>(data), codeUnits);
    while (!text.isEmpty() && text.back().unicode() == 0) {
      text.chop(1);
    }
    text.replace("\r\n", "\n");
    return text;
  }

  QString fromClipboardAnsi(const BYTE* data, UINT32 size) const {
    if (data == nullptr || size == 0 || size > rdp::kMaxClipboardPayloadBytes) {
      return QString();
    }

    int byteCount = static_cast<int>(size);
    while (byteCount > 0 && data[byteCount - 1] == 0) {
      --byteCount;
    }

    QString text = QString::fromLocal8Bit(reinterpret_cast<const char*>(data), byteCount);
    text.replace("\r\n", "\n");
    return text;
  }

  void sendClipboardFormatList() {
    tryBindCliprdrInterface();
    if (!connected_ || cliprdr_ == nullptr || cliprdr_->ClientFormatList == nullptr) {
      qWarning().noquote() << "[cliprdr] sendClipboardFormatList skipped connected="
                           << connected_ << "cliprdr=" << (cliprdr_ != nullptr);
      return;
    }

    const QString currentText = localClipboardText_;
    const QString currentUris = localClipboardFileUris_.trimmed();
    if (currentText == lastLocalClipboardTextAdvertised_ &&
        currentUris == lastLocalClipboardUrisAdvertised_) {
      return;
    }

    QVector<CLIPRDR_FORMAT> formats;
    QVector<QByteArray> names;
    formats.reserve(8);
    names.reserve(2);
    CLIPRDR_FORMAT format{};
    format.formatId = CF_UNICODETEXT;
    formats.push_back(format);
    format.formatId = CF_TEXT;
    formats.push_back(format);
    format.formatId = CF_OEMTEXT;
    formats.push_back(format);

    if (enableClipboard_ && clipboardSystem_ != nullptr && !currentUris.isEmpty() &&
        fileGroupDescriptorFormatId_ != 0 && fileContentsFormatId_ != 0) {
      names.push_back(QByteArray("FileGroupDescriptorW"));
      CLIPRDR_FORMAT fg{};
      fg.formatId = fileGroupDescriptorFormatId_;
      fg.formatName = names.back().data();
      formats.push_back(fg);

      names.push_back(QByteArray("FileContents"));
      CLIPRDR_FORMAT fc{};
      fc.formatId = fileContentsFormatId_;
      fc.formatName = names.back().data();
      formats.push_back(fc);
    }

    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.common.msgFlags = 0;
    list.common.dataLen = 0;
    list.numFormats = static_cast<UINT32>(formats.size());
    list.formats = formats.data();
    const UINT rc = cliprdr_->ClientFormatList(cliprdr_, &list);
    qInfo().noquote() << "[cliprdr] sent ClientFormatList rc=" << rc << "formats=" << list.numFormats;
    if (rc == CHANNEL_RC_OK) {
      lastLocalClipboardTextAdvertised_ = currentText;
      lastLocalClipboardUrisAdvertised_ = currentUris;
    }
  }

  void sendClipboardCapabilities() {
    if (!connected_ || cliprdr_ == nullptr || cliprdr_->ClientCapabilities == nullptr) {
      return;
    }

    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = 12;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
    if (enableClipboard_ && fileCliprdr_ != nullptr) {
      general.generalFlags |= cliprdr_file_context_current_flags(fileCliprdr_);
    }

    CLIPRDR_CAPABILITIES caps{};
    caps.common.msgType = CB_CLIP_CAPS;
    caps.common.msgFlags = 0;
    caps.common.dataLen = 0;
    caps.cCapabilitiesSets = 1;
    caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);

    const UINT rc = cliprdr_->ClientCapabilities(cliprdr_, &caps);
    qInfo().noquote() << "[cliprdr] sent ClientCapabilities rc=" << rc;
  }

  void cleanup() {
    if (fileCliprdr_ != nullptr) {
      if (cliprdr_ != nullptr) {
        cliprdr_file_context_uninit(fileCliprdr_, cliprdr_);
      }
      cliprdr_file_context_free(fileCliprdr_);
      fileCliprdr_ = nullptr;
    }

    if (clipboardSystem_ != nullptr) {
      ClipboardDestroy(clipboardSystem_);
      clipboardSystem_ = nullptr;
    }

    if (cliprdr_ != nullptr) {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByCliprdr.remove(cliprdr_);
      cliprdr_->custom = nullptr;
      cliprdr_ = nullptr;
    }
    if (disp_ != nullptr) {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByDisp.remove(disp_);
      disp_->custom = nullptr;
      disp_ = nullptr;
    }

    if (instance_ != nullptr && instance_->context != nullptr) {
      QMutexLocker lock(&gWorkerMapMutex);
      gWorkerByContext.remove(instance_->context);
    }

    if (instance_ != nullptr) {
      if (connected_ && !disconnectIssued_) {
        disconnectIssued_ = true;
        freerdp_disconnect(instance_);
        connected_ = false;
      }
      if (instance_->context != nullptr) {
        if (instance_->context->gdi != nullptr) {
          gdi_free(instance_);
        }
        freerdp_context_free(instance_);
      }
      freerdp_free(instance_);
      instance_ = nullptr;
    }

    connected_ = false;
    disconnectIssued_ = false;
    remoteFileTransferActive_ = false;
    pendingClipboardFormatList_ = false;
    displayControlActivated_ = false;
    pendingResize_ = false;
    connectedSinceMs_ = 0;
    lastResizeApplyMs_ = 0;
  }

  QString host_;
  int port_;
  std::optional<QString> username_;
  std::optional<QString> domain_;
  std::optional<QString> password_;
  std::optional<QString> gatewayHost_;
  int gatewayPort_;
  std::optional<QString> gatewayUsername_;
  std::optional<QString> gatewayDomain_;
  std::optional<QString> gatewayPassword_;
  bool gatewayUseSameCredentials_;
  int desiredWidth_;
  int desiredHeight_;
  bool enableClipboard_;
  bool mapHomeDrive_;

  freerdp* instance_;
  CliprdrClientContext* cliprdr_;
  DispClientContext* disp_;
  CliprdrFileContext* fileCliprdr_;
  wClipboard* clipboardSystem_;
  UINT32 fileGroupDescriptorFormatId_;
  UINT32 fileContentsFormatId_;
  UINT32 uriListFormatId_;
  bool stopRequested_;
  bool connected_;
  bool disconnectIssued_;
  bool hasRemoteUnicodeFormat_;
  UINT32 requestedClipboardFormatId_;
  UINT32 remoteFileDescriptorFormatId_;
  int remoteFileRequestRetryCount_;
  qint64 lastRemoteFileRequestMs_;
  qint64 lastRemoteFileAppliedMs_;
  bool remoteFileTransferActive_;
  qint64 remoteFileTransferStartMs_;
  qint64 lastRemoteFileProgressLogMs_;
  bool pendingClipboardFormatList_;
  qint64 lastClipboardFormatListSentMs_;
  QString lastLocalClipboardTextAdvertised_;
  QString lastLocalClipboardUrisAdvertised_;
  qint64 connectedSinceMs_;
  qint64 lastResizeApplyMs_;
  bool displayControlActivated_;
  bool pendingResize_;
  int pendingResizeWidth_;
  int pendingResizeHeight_;
  QString localClipboardText_;
  QString localClipboardFileUris_;
  QMutex certificateStoreMutex_;
  QMutex inputMutex_;
  QVector<InputCommand> inputQueue_;
};

RdpSession::RdpSession(QString host, int port, std::optional<QString> username, std::optional<QString> domain,
                       std::optional<QString> password, std::optional<QString> gatewayHost, int gatewayPort,
                       std::optional<QString> gatewayUsername, std::optional<QString> gatewayDomain,
                       std::optional<QString> gatewayPassword, bool gatewayUseSameCredentials,
                       int initialWidth, int initialHeight,
                       bool enableClipboard, bool mapHomeDrive, QObject* parent)
    : ISession(parent),
      host_(std::move(host)),
      port_(port),
      username_(std::move(username)),
      domain_(std::move(domain)),
      password_(std::move(password)),
      gatewayHost_(std::move(gatewayHost)),
      gatewayPort_(gatewayPort),
      gatewayUsername_(std::move(gatewayUsername)),
      gatewayDomain_(std::move(gatewayDomain)),
      gatewayPassword_(std::move(gatewayPassword)),
      gatewayUseSameCredentials_(gatewayUseSameCredentials),
      initialWidth_(qMax(320, initialWidth)),
      initialHeight_(qMax(240, initialHeight)),
      enableClipboard_(enableClipboard),
      mapHomeDrive_(mapHomeDrive),
      sessionTag_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      state_(SessionState::Initialized),
      shutdownStarted_(false),
      stopIssued_(false),
      worker_(nullptr) {
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] created host=" << host_ << "port=" << port_;
}

RdpSession::~RdpSession() {
  if (shutdownStarted_) {
    return;
  }
  shutdownStarted_ = true;
  shutdownWorkerThread();
}

void RdpSession::connectSession() {
  if (shutdownStarted_) {
    return;
  }
  if (state_ == SessionState::Connected || state_ == SessionState::Connecting) {
    return;
  }
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] connect requested";

  ensureWorkerThread();
  if (worker_ == nullptr) {
    setState(SessionState::Error);
    Q_EMIT errorOccurred("Failed to create RDP worker");
    return;
  }

  QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
}

void RdpSession::disconnectSession() {
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] disconnect requested";
  requestWorkerStop(false);
  setState(SessionState::Disconnected);
}

void RdpSession::resizeSession(int width, int height) {
  if (worker_ == nullptr) {
    return;
  }
  worker_->enqueueResizeRequest(width, height);
}

bool RdpSession::isConnected() const {
  return state_ == SessionState::Connected;
}

SessionState RdpSession::state() const {
  return state_;
}

void RdpSession::sendKeyInput(int qtKey, quint32 nativeScanCode, bool pressed) {
  if (worker_ == nullptr) {
    return;
  }

  UINT8 code = 0;
  UINT16 flags = 0;
  if (!rdp::mapQtKeyToRdp(qtKey, nativeScanCode, &code, &flags)) {
    if (qtKey == Qt::Key_T || qtKey == Qt::Key_G || qtKey == Qt::Key_B || qtKey == Qt::Key_Y) {
      qWarning().noquote() << "[kbd-rdp] map failed key=" << qtKey << "nativeScan=" << nativeScanCode
                           << "pressed=" << pressed;
    }
    return;
  }

  if (!pressed) {
    flags |= KBD_FLAGS_RELEASE;
  }

  if (qtKey == Qt::Key_T || qtKey == Qt::Key_G || qtKey == Qt::Key_B || qtKey == Qt::Key_Y ||
      qtKey == Qt::Key_Meta || qtKey == Qt::Key_Super_L || qtKey == Qt::Key_Super_R) {
    qInfo().noquote() << "[kbd-rdp] send key=" << qtKey << "scanCode=" << code << "flags=0x"
                      << QString::number(flags, 16) << "pressed=" << pressed;
  }

  worker_->enqueueKeyboardEvent(int(flags), int(code));
}

void RdpSession::sendMouseMove(int x, int y) {
  if (worker_ == nullptr) {
    return;
  }
  worker_->enqueueMouseEvent(int(PTR_FLAGS_MOVE), x, y);
}

void RdpSession::sendMouseButton(Qt::MouseButton button, bool pressed, int x, int y) {
  if (worker_ == nullptr) {
    return;
  }

  UINT16 flags = rdp::mouseFlagForButton(button);
  if (flags == 0) {
    return;
  }
  if (pressed) {
    flags |= PTR_FLAGS_DOWN;
  }

  worker_->enqueueMouseEvent(int(flags), x, y);
}

void RdpSession::sendWheel(Qt::Orientation orientation, int delta, int x, int y) {
  if (worker_ == nullptr) {
    return;
  }

  UINT16 flags = (orientation == Qt::Horizontal) ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
  if (delta < 0) {
    flags |= PTR_FLAGS_WHEEL_NEGATIVE;
    delta = -delta;
  }
  flags |= static_cast<UINT16>(delta) & WheelRotationMask;

  worker_->enqueueMouseEvent(int(flags), x, y);
}

void RdpSession::setLocalClipboardText(const QString& text) {
  if (worker_ == nullptr) {
    return;
  }
  worker_->enqueueClipboardText(text);
}

void RdpSession::setLocalClipboardFileUris(const QString& uriList) {
  if (worker_ == nullptr) {
    return;
  }
  worker_->enqueueClipboardFileUris(uriList);
}

void RdpSession::setState(SessionState newState) {
  if (state_ == newState) {
    return;
  }
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] state" << static_cast<int>(state_) << "->"
                    << static_cast<int>(newState);
  state_ = newState;
  Q_EMIT stateChanged(state_);
}

void RdpSession::ensureWorkerThread() {
  if (worker_ != nullptr) {
    return;
  }
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] creating worker thread";

  worker_ = new RdpSessionWorker(host_, port_, username_, domain_, password_, gatewayHost_, gatewayPort_,
                                 gatewayUsername_, gatewayDomain_, gatewayPassword_,
                                 gatewayUseSameCredentials_,
                                 initialWidth_, initialHeight_, enableClipboard_, mapHomeDrive_);
  worker_->moveToThread(&workerThread_);
  stopIssued_ = false;

  connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&workerThread_, &QThread::finished, this, [this]() {
    qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] worker thread finished";
    worker_ = nullptr;
    stopIssued_ = false;
  });
  connect(worker_, &RdpSessionWorker::stateChanged, this, [this](SessionState s) { setState(s); });
  connect(worker_, &RdpSessionWorker::errorOccurred, this, &RdpSession::errorOccurred);
  connect(worker_, &RdpSessionWorker::frameReady, this, &RdpSession::frameUpdated);
  connect(worker_, &RdpSessionWorker::remoteClipboardText, this, &RdpSession::remoteClipboardText);
  connect(worker_, &RdpSessionWorker::remoteClipboardFileUris, this, &RdpSession::remoteClipboardFileUris);
  connect(worker_, &RdpSessionWorker::remoteLogoff, this, &RdpSession::remoteLogoff);

  if (!workerThread_.isRunning()) {
    workerThread_.start();
    qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] worker thread started";
  }
}

bool RdpSession::requestWorkerStop(bool blockUntilStopped) {
  if (worker_ == nullptr || stopIssued_) {
    return false;
  }

  stopIssued_ = true;
  if (!workerThread_.isRunning()) {
    qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] worker thread already stopped; skipping stop invoke";
    return false;
  }
  worker_->requestStopFromAnyThread();
  Q_UNUSED(blockUntilStopped);
  return true;
}

bool RdpSession::isOnWorkerThread() const {
  return QThread::currentThread() == &workerThread_;
}

void RdpSession::shutdownWorkerThread() {
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] shutdown begin";
  requestWorkerStop(true);

  if (workerThread_.isRunning()) {
    workerThread_.quit();

    QElapsedTimer timer;
    timer.start();
    while (workerThread_.isRunning() && timer.elapsed() < 5000) {
      workerThread_.wait(50);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    if (workerThread_.isRunning()) {
      qWarning().noquote() << "[rdp-session id=" + sessionTag_ + "] worker did not stop in 5s; waiting longer";
      workerThread_.requestInterruption();
      workerThread_.quit();
      QElapsedTimer timer2;
      timer2.start();
      while (workerThread_.isRunning() && timer2.elapsed() < 10000) {
        workerThread_.wait(50);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      }
      if (workerThread_.isRunning()) {
        qCritical().noquote() << "[rdp-session id=" + sessionTag_ + "] worker still running after graceful waits";
        workerThread_.terminate();
        workerThread_.wait(3000);
      }
    }
  }
  worker_ = nullptr;
  qInfo().noquote() << "[rdp-session id=" + sessionTag_ + "] shutdown complete";
}

}  // namespace vaultrdp::protocols

#include "RdpSession.moc"
