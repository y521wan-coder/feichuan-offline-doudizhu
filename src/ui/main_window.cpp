#include "main_window.h"
#include "models/hand_list_model.h"
#include "models/player_status_model.h"
#include "widgets/game_status_widget.h"
#include "dialogs/settings_dialog.h"
#include "dialogs/sound_manager_dialog.h"
#include "dialogs/result_dialog.h"
#include "../core/engine/game_engine.h"
#include "../core/engine/turn_manager.h"
#include "../accessibility/accessibility_service.h"
#include "../ai/ai_player.h"
#include "../ai/simple_ai.h"
#include "../ai/standard_ai.h"
#include "../ai/heuristic_model.h"
#include "../persistence/data_paths.h"
#include "../ai/hint_service.h"
#include "../persistence/settings_repository.h"
#include "../persistence/statistics_repository.h"
#include "../persistence/data_paths.h"
#include "../app/update_service.h"
#include "../core/text/card_text_formatter.h"
#include "../core/text/game_text_formatter.h"
#include "../core/audio/card_pattern_sound_plan.h"
#include "../core/model/player.h"
#include "sound_service.h"
#include <persistence/diagnostic_trace_service.h>
#include <QApplication>
#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QEvent>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProcess>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <QDate>
#include <QDateTime>
#include <QPixmap>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QGroupBox>
#include <QStringList>
#include <QSet>
#include <QAction>
#include <QShortcut>
#include <QSignalBlocker>
#include <algorithm>
#include <array>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fpdz {

namespace {

constexpr int kFirstRunGuideRevision = 2;
const auto kStartupUpdateGuideFileName = u8"飞船AI斗地主单机版2.0更新说明.txt";
const auto kDetailedGuideFileName = u8"飞船AI斗地主单机版详细使用说明.txt";
const auto kRulesFileName = u8"飞船AI斗地主单机版玩法说明.txt";
const auto kShortcutsFileName = u8"飞船AI斗地主单机版快捷键说明.txt";
const auto kSoundGuideFileName = u8"飞船AI斗地主单机版音效分类与替换说明.txt";
const auto kChangelogFileName = u8"飞船AI斗地主单机版更新日志.txt";

QString redactedPath(QString path) {
    const QString home = QDir::homePath();
    if (!home.isEmpty()) path.replace(home, QStringLiteral("%USERPROFILE%"), Qt::CaseInsensitive);
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) path.replace(appData, QStringLiteral("%APPDATA%"), Qt::CaseInsensitive);
    return QDir::toNativeSeparators(path);
}

QString fileSha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("不可读取");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return QStringLiteral("计算失败");
    return QString::fromLatin1(hash.result().toHex().toUpper());
}

QString gamePhaseText(GamePhase phase) {
    switch (phase) {
    case GamePhase::NotStarted: return QStringLiteral("NotStarted");
    case GamePhase::Dealing: return QStringLiteral("Dealing");
    case GamePhase::Bidding: return QStringLiteral("Bidding");
    case GamePhase::RevealingBottomCards: return QStringLiteral("RevealingBottomCards");
    case GamePhase::Playing: return QStringLiteral("Playing");
    case GamePhase::Paused: return QStringLiteral("Paused");
    case GamePhase::Settling: return QStringLiteral("Settling");
    case GamePhase::Finished: return QStringLiteral("Finished");
    }
    return QStringLiteral("Unknown");
}

QJsonObject sanitizeTraceEvent(const QJsonObject& source) {
    static const QSet<QString> allowedTypes = {
        QStringLiteral("session_start"), QStringLiteral("keyboard"),
        QStringLiteral("hand_action"), QStringLiteral("play_cards"),
        QStringLiteral("phase"), QStringLiteral("focus"),
        QStringLiteral("application_exit"),
        QStringLiteral("accessibility_mode_changed"),
        QStringLiteral("screen_reader_backend_initialized"),
        QStringLiteral("speech_delivery"), QStringLiteral("speech_suppressed"),
        QStringLiteral("bottom_cards_revealed_for_bidding"),
        QStringLiteral("bottom_cards_announced"),
        QStringLiteral("bottom_cards_queried"), QStringLiteral("trace_overflow")
    };
    const QString type = source.value(QStringLiteral("type")).toString();
    if (!allowedTypes.contains(type)) return {};
    static const QStringList allowedKeys = {
        QStringLiteral("timestamp"), QStringLiteral("type"), QStringLiteral("source"),
        QStringLiteral("key"), QStringLiteral("scan_code"), QStringLiteral("ctrl"),
        QStringLiteral("shift"), QStringLiteral("alt"), QStringLiteral("key_down"),
        QStringLiteral("auto_repeat"), QStringLiteral("focus_class"),
        QStringLiteral("action"), QStringLiteral("operation"), QStringLiteral("status"),
        QStringLiteral("phase"), QStringLiteral("game_id"), QStringLiteral("success"),
        QStringLiteral("reason"), QStringLiteral("before"), QStringLiteral("after"),
        QStringLiteral("submitted_count"), QStringLiteral("player_id"),
        QStringLiteral("detail"),
        QStringLiteral("dropped_count"),
        QStringLiteral("route"), QStringLiteral("backend"), QStringLiteral("channel"),
        QStringLiteral("result"), QStringLiteral("bottom_count")
    };
    QJsonObject result;
    for (const QString& key : allowedKeys) {
        if (source.contains(key)) result.insert(key, source.value(key));
    }
    return result;
}

QString cardFourVoiceDir(PlayerId playerId, bool humanUsesFemaleVoice) {
    const int index = static_cast<int>(playerId);
    return ((index == 0 && humanUsesFemaleVoice) || index == 3) ? "girl" : "boy";
}

QString cardFourVoiceFile(PlayerId playerId, const QString& fileName,
                          bool humanUsesFemaleVoice) {
    return "card_four/" + cardFourVoiceDir(playerId, humanUsesFemaleVoice) +
           "/" + fileName + ".wav";
}

struct TierWeightsLoadResult {
    std::array<HeuristicWeights, 3> weights{
        HeuristicWeights::defaults(), HeuristicWeights::defaults(), HeuristicWeights::defaults()};
    QString warning;
};

TierWeightsLoadResult loadTierWeights() {
    TierWeightsLoadResult result;
    QFile manifestFile(DataPaths::activeTierManifestFile());
    if (manifestFile.exists()) {
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            result.warning = QStringLiteral("无法读取三级机器人模型清单，当前使用内置三级机器人。");
            return result;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        const auto root = document.object();
        if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
            root.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
            root.value(QStringLiteral("ruleFingerprint")).toString() !=
                QLatin1String(FOUR_PLAYER_STANDARD_V1_FINGERPRINT)) {
            result.warning = QStringLiteral("三级机器人模型清单格式或规则指纹无效，当前使用内置三级机器人。");
            return result;
        }
        const auto models = root.value(QStringLiteral("models")).toObject();
        const auto hashes = root.value(QStringLiteral("sha256")).toObject();
        const std::array<QString, 3> names = {
            QStringLiteral("beginner"), QStringLiteral("intermediate"), QStringLiteral("master")};
        for (size_t index = 0; index < names.size(); ++index) {
            const QString relative = models.value(names[index]).toString();
            const QString expectedHash = hashes.value(names[index]).toString().toLower();
            const QString modelsRoot = QDir::cleanPath(QDir(DataPaths::modelsDir()).absolutePath());
            const QString path = QDir::cleanPath(QDir(modelsRoot).absoluteFilePath(relative));
            QFile modelFile(path);
            if (relative.isEmpty() || !path.startsWith(modelsRoot + QLatin1Char('/'),
                                                       Qt::CaseInsensitive) ||
                !modelFile.open(QIODevice::ReadOnly)) {
                result.warning = QStringLiteral("三级机器人模型清单无效，当前使用内置三级机器人。");
                return result;
            }
            const QByteArray bytes = modelFile.readAll();
            const QString actualHash = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            QString error;
            const auto package = HeuristicModelPackage::fromJson(
                QJsonDocument::fromJson(bytes).object(), &error);
            if (!package || !package->promotionEligible ||
                package->tierLabel != names[index] || expectedHash != actualHash) {
                result.warning = QStringLiteral("三级机器人模型校验失败，当前使用内置三级机器人。原因：%1")
                                     .arg(error.isEmpty() ? QStringLiteral("文件哈希不符") : error);
                return result;
            }
            result.weights[index] = package->weights;
        }
        return result;
    }

    const QString legacyPath = DataPaths::activeMasterModelFile();
    if (QFileInfo::exists(legacyPath)) {
        QString error;
        const auto legacy = HeuristicModelPackage::loadFile(legacyPath, &error);
        if (legacy && legacy->promotionEligible) {
            result.weights[static_cast<size_t>(AiDifficulty::Master)] = legacy->weights;
        } else {
            result.warning = QStringLiteral("原大师模型校验失败，当前大师使用内置逻辑。原因：%1").arg(error);
        }
    }
    return result;
}

std::unique_ptr<AiPlayer> createAiPlayer(AiDifficulty difficulty,
                                         const HeuristicWeights& weights) {
    return std::make_unique<StandardAiPlayer>(difficulty, weights);
}

int aiDelayMilliseconds(int setting) {
    switch (setting) {
    case 0: return 0;
    case 1: return 250;
    case 3: return 1000;
    default: return 500;
    }
}

int readableAnnouncementDelayMilliseconds(const std::wstring& text) {
    const int estimated = 800 + static_cast<int>(text.size()) * 220;
    return std::clamp(estimated, 1800, 6000);
}

constexpr int playerActionNameDelayMilliseconds() {
    return 300;
}

#ifdef Q_OS_WIN
constexpr UINT kKeyboardHookMessage = WM_APP + 0x4F;
constexpr UINT kKeyboardTraceMessage = WM_APP + 0x50;
constexpr LPARAM kHookCtrlFlag = 0x01;
constexpr LPARAM kHookShiftFlag = 0x02;
constexpr LPARAM kHookAltFlag = 0x04;
constexpr LPARAM kHookKeyUpFlag = 0x08;
constexpr int kHookScanShift = 8;
constexpr int kHotkeyF11 = 0x4F11;
constexpr int kHotkeyF12 = 0x4F12;
constexpr int kHotkeyAlt1 = 0x4A01;
constexpr int kHotkeyAlt2 = 0x4A02;
constexpr int kHotkeyAlt3 = 0x4A03;
constexpr int kHotkeyAlt4 = 0x4A04;
constexpr int kHotkeyAltF = 0x4A0F;

HHOOK g_keyboardHook = nullptr;
HWND g_keyboardHookWindow = nullptr;
bool g_f1Down = false;
bool g_controlDown = false;
bool g_shiftDown = false;
bool g_altDown = false;
std::array<bool, 256> g_singleFireKeyDown{};
std::array<bool, 256> g_traceKeyDown{};
int g_openMenuCount = 0;

bool isControlDown() {
    return g_controlDown ||
           (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
}

bool isShiftDown() {
    return g_shiftDown ||
           (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
}

bool isAltDown() {
    return g_altDown ||
           (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
}

bool isKeyboardHookForeground() {
    if (!g_keyboardHookWindow || !IsWindow(g_keyboardHookWindow)) return false;
    const HWND foreground = GetForegroundWindow();
    if (foreground == g_keyboardHookWindow || IsChild(g_keyboardHookWindow, foreground)) {
        return true;
    }
    if (GetAncestor(foreground, GA_ROOT) == g_keyboardHookWindow) return true;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool shouldYieldKeyboardHandlingToFocusedWidget() {
    if (g_openMenuCount > 0 || QApplication::activePopupWidget() ||
        QApplication::activeModalWidget()) {
        return true;
    }

    QWidget* focusWidget = QApplication::focusWidget();
    return focusWidget &&
           (qobject_cast<QMenuBar*>(focusWidget) || qobject_cast<QMenu*>(focusWidget));
}

bool isKeyboardHookCandidate(DWORD virtualKey, bool altDown) {
    if (altDown) {
        return virtualKey == '1' || virtualKey == '2' || virtualKey == '3' ||
               virtualKey == '4' ||
               virtualKey == 'F' || virtualKey == 'X';
    }

    switch (virtualKey) {
    case VK_F1:
    case VK_F2:
    case VK_F5:
    case VK_F11:
    case VK_F12:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_UP:
    case VK_DOWN:
    case VK_RETURN:
    case VK_SPACE:
        return true;
    default:
        return virtualKey >= '0' && virtualKey <= '3';
    }
}

bool isSingleFireKey(DWORD virtualKey) {
    return virtualKey == VK_F1 || virtualKey == VK_F2 || virtualKey == VK_F5 ||
           virtualKey == VK_F11 || virtualKey == VK_F12 ||
           virtualKey == VK_HOME || virtualKey == VK_END ||
           virtualKey == VK_UP || virtualKey == VK_DOWN ||
           virtualKey == VK_RETURN || virtualKey == VK_SPACE;
}

LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if ((key->flags & LLKHF_INJECTED) != 0) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool repeatedKeyDown = keyDown && key->vkCode < g_traceKeyDown.size() &&
        g_traceKeyDown[key->vkCode];
    if (key->vkCode < g_traceKeyDown.size()) {
        g_traceKeyDown[key->vkCode] = keyDown;
    }
    if (key->vkCode == VK_CONTROL || key->vkCode == VK_LCONTROL ||
        key->vkCode == VK_RCONTROL) {
        g_controlDown = keyDown;
    }
    if (key->vkCode == VK_SHIFT || key->vkCode == VK_LSHIFT ||
        key->vkCode == VK_RSHIFT) {
        g_shiftDown = keyDown;
    }
    if (key->vkCode == VK_MENU || key->vkCode == VK_LMENU ||
        key->vkCode == VK_RMENU) {
        g_altDown = keyDown;
    }

    // Keep the diagnostic useful without posting dozens of messages per second
    // while a physical key is held. Business handling below still preserves
    // deliberate left/right continuous navigation.
    if (isKeyboardHookForeground() && !repeatedKeyDown) {
        LPARAM traceFlags =
            (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
        if (isControlDown()) traceFlags |= kHookCtrlFlag;
        if (isShiftDown()) traceFlags |= kHookShiftFlag;
        if (isAltDown() || wParam == WM_SYSKEYDOWN || wParam == WM_SYSKEYUP) {
            traceFlags |= kHookAltFlag;
        }
        if (!keyDown) traceFlags |= kHookKeyUpFlag;
        PostMessageW(g_keyboardHookWindow, kKeyboardTraceMessage, key->vkCode, traceFlags);
    }

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (key->vkCode == VK_F1) g_f1Down = false;
        if (key->vkCode < g_singleFireKeyDown.size()) {
            g_singleFireKeyDown[key->vkCode] = false;
        }
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool altF4 = keyDown && key->vkCode == VK_F4 && isAltDown();
    if (altF4 && isKeyboardHookForeground()) {
        const LPARAM flags = kHookAltFlag |
            (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
        PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
        return 1;
    }

    const bool plainF1 = keyDown && key->vkCode == VK_F1 &&
        !isControlDown() && !isShiftDown() && !isAltDown();
    if (plainF1 && isKeyboardHookForeground()) {
        if (!g_f1Down) {
            g_f1Down = true;
            const LPARAM flags =
                (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
            PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
        }
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool plainF2 = keyDown && key->vkCode == VK_F2 &&
        !isControlDown() && !isShiftDown() && !isAltDown();
    if (plainF2 && isKeyboardHookForeground()) {
        if (!g_singleFireKeyDown[VK_F2]) {
            g_singleFireKeyDown[VK_F2] = true;
            const LPARAM flags =
                (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
            PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
        }
        return 1;
    }

    const bool plainF5 = keyDown && key->vkCode == VK_F5 &&
        !isControlDown() && !isShiftDown() && !isAltDown();
    if (plainF5 && isKeyboardHookForeground()) {
        if (!g_singleFireKeyDown[VK_F5]) {
            g_singleFireKeyDown[VK_F5] = true;
            const LPARAM flags =
                (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
            PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
        }
        return 1;
    }

    const bool plainF11 = keyDown && key->vkCode == VK_F11 &&
        !isControlDown() && !isShiftDown() && !isAltDown();
    if (plainF11 && isKeyboardHookForeground()) {
        if (!g_singleFireKeyDown[VK_F11]) {
            g_singleFireKeyDown[VK_F11] = true;
            const LPARAM flags =
                (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
            PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
        }
        return 1;
    }

    if (shouldYieldKeyboardHandlingToFocusedWidget() || !isKeyboardHookForeground() ||
        (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN)) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool ctrlDown = isControlDown();
    const bool shiftDown = isShiftDown();
    const bool altDown = isAltDown() || wParam == WM_SYSKEYDOWN;
    if ((key->vkCode == VK_RETURN || key->vkCode == VK_SPACE) &&
        (ctrlDown || shiftDown || altDown)) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }
    if (!isKeyboardHookCandidate(key->vkCode, altDown)) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (isSingleFireKey(key->vkCode) && key->vkCode < g_singleFireKeyDown.size()) {
        if (g_singleFireKeyDown[key->vkCode]) {
            return key->vkCode == VK_F1
                ? CallNextHookEx(g_keyboardHook, code, wParam, lParam)
                : 1;
        }
        g_singleFireKeyDown[key->vkCode] = true;
    }

    LPARAM flags = (static_cast<LPARAM>(key->scanCode & 0xFF) << kHookScanShift);
    if (ctrlDown) flags |= kHookCtrlFlag;
    if (shiftDown) flags |= kHookShiftFlag;
    if (altDown) flags |= kHookAltFlag;

    PostMessageW(g_keyboardHookWindow, kKeyboardHookMessage, key->vkCode, flags);
    return 1;
}
#endif

} // namespace

MainWindow::MainWindow(GameEngine& engine, AccessibilityService& accessibility,
                       DiagnosticTraceService* diagnosticTrace, QWidget* parent)
    : QMainWindow(parent), m_engine(engine), m_accessibility(accessibility),
      m_diagnosticTrace(diagnosticTrace) {
#ifdef Q_OS_WIN
    g_openMenuCount = 0;
#endif
    setWindowTitle(QString::fromUtf8(u8"飞船AI斗地主单机版"));
    setMinimumSize(800, 600);
    m_handModel = std::make_unique<HandListModel>();
    m_playerModel = std::make_unique<PlayerStatusModel>();
    
    // AI timer
    m_settingsRepo = std::make_unique<SettingsRepository>();
    m_statisticsRepo = std::make_unique<StatisticsRepository>();
    m_updateService = std::make_unique<UpdateService>(this);
    connect(m_updateService.get(), &UpdateService::checkFinished,
            this, &MainWindow::handleUpdateCheckFinished);
    connect(m_updateService.get(), &UpdateService::downloadProgress,
            this, [this](qint64 received, qint64 total) {
                if (!m_updateProgressDialog) return;
                if (total > 0) {
                    m_updateProgressDialog->setRange(0, 1000);
                    m_updateProgressDialog->setValue(
                        static_cast<int>((received * 1000) / total));
                } else {
                    m_updateProgressDialog->setRange(0, 0);
                }
            });
    connect(m_updateService.get(), &UpdateService::downloadFinished,
            this, &MainWindow::handleUpdateDownloadFinished);
    m_statisticsRepo->load(DataPaths::statisticsFile());
    loadSettings();
    const auto tierLoad = loadTierWeights();
    m_tierWeights = tierLoad.weights;
    m_modelLoadWarning = tierLoad.warning;
    if (m_diagnosticTrace) {
        m_diagnosticTrace->record(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("screen_reader_backend_initialized")},
            {QStringLiteral("route"), QString::fromStdWString(m_accessibility.routeName())},
            {QStringLiteral("backend"), QString::fromStdWString(m_accessibility.backendName())},
            {QStringLiteral("success"), true}});
    }
    applyPlayerDisplayNamesToState();
    m_sound = std::make_unique<SoundService>(this);
    m_sound->initialize();
    m_sound->setEnabled(m_settings.soundEnabled);
    m_sound->setVolume(m_settings.soundVolume);
    m_sound->setCategorySettings(m_settings.soundCategories);
    m_sound->setMusicEnabled(m_settings.backgroundMusicEnabled);
    m_sound->setMusicVolume(m_settings.backgroundMusicVolume);
    m_aiTimer = new QTimer(this);
    m_aiTimer->setInterval(aiDelayMilliseconds(m_settings.aiDelay));
    m_aiTimer->setSingleShot(true);
    connect(m_aiTimer, &QTimer::timeout, this, &MainWindow::onAiTurn);

    m_turnCountdownTimer = new QTimer(this);
    m_turnCountdownTimer->setInterval(1000);
    connect(m_turnCountdownTimer, &QTimer::timeout, this, [this]() {
        if (m_turnSecondsRemaining > 0) --m_turnSecondsRemaining;
        m_countdownLabel->setText(QString::fromStdWString(L"本轮操作剩余")
            + QString::number(m_turnSecondsRemaining) + QString::fromStdWString(L"秒"));
        if (m_turnSecondsRemaining == 0) {
            m_turnCountdownTimer->stop();
            handleAutoPassTimeout();
        }
    });

    m_trainingInstallExitTimer = new QTimer(this);
    m_trainingInstallExitTimer->setInterval(1000);
    connect(m_trainingInstallExitTimer, &QTimer::timeout,
            this, &MainWindow::checkTrainingInstallExitRequest);
    m_trainingInstallExitTimer->start();
    
    setupMenus();
    setupUi();
    setupShortcuts();
    updateBackgroundMusic();
    QFile::remove(DataPaths::autoSaveFile());
    QFile::remove(DataPaths::autoSaveBackupFile());
    qApp->installEventFilter(this);
#ifdef Q_OS_WIN
    QCoreApplication::instance()->installNativeEventFilter(this);
#endif
    installKeyboardHook();
    QTimer::singleShot(0, this, [this]() {
        if (!QApplication::activePopupWidget() &&
            (!QApplication::activeModalWidget() || QApplication::activeModalWidget() == this)) {
            setFocus(Qt::OtherFocusReason);
        }
    });
    QTimer::singleShot(300, this, [this]() {
        playSound(SoundId::GameStart);
    });
    if (!m_modelLoadWarning.isEmpty()) {
        QTimer::singleShot(700, this, [this]() {
            if (!qApp->property("fpdz.suppressStartupPrompts").toBool()) {
                QMessageBox::warning(this, QStringLiteral("机器人模型已安全回退"),
                                     m_modelLoadWarning);
            }
        });
    }
    QTimer::singleShot(0, this, [this]() {
        if (qApp->property("fpdz.suppressStartupPrompts").toBool()) return;
        if (m_settings.firstRunGuideRevision < kFirstRunGuideRevision &&
            openHelpTextFile(QString::fromUtf8(kStartupUpdateGuideFileName))) {
            m_settings.firstRunGuideShown = true;
            m_settings.firstRunGuideRevision = kFirstRunGuideRevision;
            saveSettings();
        }
        if (m_settings.automaticUpdateChecks &&
            m_settings.lastAutomaticUpdateCheckDate !=
                QDate::currentDate().toString(Qt::ISODate)) {
            QTimer::singleShot(250, this, [this]() { checkForUpdates(false); });
        }
    });
}

MainWindow::~MainWindow() {
    if (m_updateService) m_updateService->cancel();
    unregisterSystemHotkeys();
    uninstallKeyboardHook();
#ifdef Q_OS_WIN
    QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
    qApp->removeEventFilter(this);
}

std::unique_ptr<MainWindow> createMainWindow(GameEngine& engine,
                                             AccessibilityService& accessibility,
                                             DiagnosticTraceService* diagnosticTrace) {
    return std::make_unique<MainWindow>(engine, accessibility, diagnosticTrace);
}

void MainWindow::setupMenus() {
    auto* gameMenu = menuBar()->addMenu(QString::fromStdWString(L"游戏(G)"));
    m_gameMenu = gameMenu;
    gameMenu->setObjectName(QStringLiteral("gameMenu"));
    gameMenu->menuAction()->setObjectName(QStringLiteral("gameMenuAction"));
    connect(gameMenu->menuAction(), &QAction::triggered, this, [this, gameMenu]() {
        openTopLevelMenu(gameMenu);
    });
    auto trackMenu = [](QMenu* menu) {
        QObject::connect(menu, &QMenu::aboutToShow, menu, []() { ++g_openMenuCount; });
        QObject::connect(menu, &QMenu::aboutToHide, menu, []() {
            g_openMenuCount = std::max(0, g_openMenuCount - 1);
        });
    };
    trackMenu(gameMenu);
    auto* newAction = gameMenu->addAction(QString::fromStdWString(L"开战/停战/恢复(&B)"));
    newAction->setShortcut(QKeySequence(Qt::Key_F1));
    newAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(newAction, &QAction::triggered, this, &MainWindow::triggerBattleShortcut);

    auto* pauseAction = gameMenu->addAction(QString::fromStdWString(L"暂停/恢复(&P)"));
    pauseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(pauseAction, &QAction::triggered, this, [this]() {
        GameCommand cmd;
        if (m_engine.state().phase() == GamePhase::Playing) {
            cmd.type = GameCommandType::Pause;
        } else if (m_engine.state().phase() == GamePhase::Paused) {
            cmd.type = GameCommandType::Resume;
        }
        m_engine.execute(cmd);
        refreshFromState();
    });

    gameMenu->addSeparator();

    auto* quitAction = gameMenu->addAction(QString::fromStdWString(L"退出(&Q)"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* settingsMenu = menuBar()->addMenu(QString::fromStdWString(L"设置(S)"));
    m_settingsMenu = settingsMenu;
    settingsMenu->setObjectName(QStringLiteral("settingsMenu"));
    settingsMenu->menuAction()->setObjectName(QStringLiteral("settingsMenuAction"));
    connect(settingsMenu->menuAction(), &QAction::triggered, this, [this, settingsMenu]() {
        openTopLevelMenu(settingsMenu);
    });
    trackMenu(settingsMenu);
    auto* playerNamesMenu = settingsMenu->addMenu(QString::fromUtf8(u8"玩家名称设置(&N)"));
    playerNamesMenu->setObjectName(QStringLiteral("playerNamesMenu"));
    playerNamesMenu->menuAction()->setObjectName(QStringLiteral("playerNamesMenuAction"));
    connect(playerNamesMenu->menuAction(), &QAction::triggered, this,
            [this, settingsMenu, playerNamesMenu]() {
                openSubMenu(settingsMenu, playerNamesMenu);
            });
    trackMenu(playerNamesMenu);
    auto addPlayerNameAction = [this, playerNamesMenu](PlayerId playerId, const QString& text) {
        auto* action = playerNamesMenu->addAction(text);
        action->setObjectName(QStringLiteral("playerNameAction%1")
                                  .arg(static_cast<int>(playerId) + 1));
        connect(action, &QAction::triggered, this, [this, playerId]() {
            openPlayerNameDialog(playerId);
        });
    };
    addPlayerNameAction(PlayerId::Player1, u8"设置玩家一名称(&1)");
    addPlayerNameAction(PlayerId::Player2, u8"设置玩家二名称(&2)");
    addPlayerNameAction(PlayerId::Player3, u8"设置玩家三名称(&3)");
    addPlayerNameAction(PlayerId::Player4, u8"设置玩家四名称(&4)");
    playerNamesMenu->addSeparator();
    auto* resetNamesAction = playerNamesMenu->addAction(QString::fromUtf8(u8"恢复默认玩家名称(&R)"));
    connect(resetNamesAction, &QAction::triggered, this, &MainWindow::resetPlayerDisplayNames);

    settingsMenu->addSeparator();

    auto* soundEnabledAction = settingsMenu->addAction(QString::fromStdWString(L"音效开关(&E)"));
    soundEnabledAction->setCheckable(true);
    soundEnabledAction->setChecked(m_sound && m_sound->isEnabled());
    connect(soundEnabledAction, &QAction::toggled, this, [this](bool enabled) {
        if (m_sound) {
            m_sound->setEnabled(enabled);
        }
        m_settings.soundEnabled = enabled;
        saveSettings();
        announce(enabled ? L"音效已开启" : L"音效已关闭", AnnouncementCategory::System);
        if (enabled) {
            playSound(SoundId::ButtonClick);
        }
    });

    auto* soundManagerAction = settingsMenu->addAction(
        QString::fromUtf8(u8"音效管理(&M)"));
    soundManagerAction->setObjectName(QStringLiteral("soundManagerAction"));
    connect(soundManagerAction, &QAction::triggered,
            this, &MainWindow::openSoundManagerDialog);

    auto* soundTestAction = settingsMenu->addAction(QString::fromStdWString(L"测试音效(&T)"));
    connect(soundTestAction, &QAction::triggered, this, [this]() {
        playSound(SoundId::GameStart);
        announce(L"已播放测试音效", AnnouncementCategory::System);
    });

    auto* settingsAction = settingsMenu->addAction(QString::fromStdWString(L"设置选项(&O)"));
    settingsAction->setShortcut(QKeySequence(Qt::Key_F5));
    settingsAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);

    settingsMenu->addSeparator();
    auto* updateAction = settingsMenu->addAction(QString::fromUtf8(u8"手动检查更新(&U)"));
    updateAction->setObjectName(QStringLiteral("manualUpdateCheckAction"));
    connect(updateAction, &QAction::triggered, this, [this]() { checkForUpdates(true); });

    auto* helpMenu = menuBar()->addMenu(QString::fromStdWString(L"帮助(H)"));
    m_helpMenu = helpMenu;
    helpMenu->setObjectName(QStringLiteral("helpMenu"));
    helpMenu->menuAction()->setObjectName(QStringLiteral("helpMenuAction"));
    connect(helpMenu->menuAction(), &QAction::triggered, this, [this, helpMenu]() {
        openTopLevelMenu(helpMenu);
    });
    trackMenu(helpMenu);
    auto* shortcutAction = helpMenu->addAction(QString::fromStdWString(L"快捷键(&H)"));
    shortcutAction->setObjectName(QStringLiteral("shortcutHelpAction"));
    connect(shortcutAction, &QAction::triggered, this, [this]() {
        openHelpTextFile(QString::fromUtf8(kShortcutsFileName));
    });

    auto* rulesAction = helpMenu->addAction(QString::fromUtf8(u8"玩法说明(&P)"));
    rulesAction->setObjectName(QStringLiteral("rulesHelpAction"));
    connect(rulesAction, &QAction::triggered, this, [this]() {
        openHelpTextFile(QString::fromUtf8(kRulesFileName));
    });

    auto* guideAction = helpMenu->addAction(QString::fromUtf8(u8"详细使用说明(&D)"));
    guideAction->setObjectName(QStringLiteral("userGuideAction"));
    connect(guideAction, &QAction::triggered, this, [this]() {
        openHelpTextFile(QString::fromUtf8(kDetailedGuideFileName));
    });

    auto* diagnosticAction = helpMenu->addAction(QString::fromUtf8(u8"复制诊断信息(&C)"));
    diagnosticAction->setObjectName(QStringLiteral("copyDiagnosticAction"));
    connect(diagnosticAction, &QAction::triggered, this, &MainWindow::copyDiagnosticInformation);

    auto* changelogAction = helpMenu->addAction(QString::fromUtf8(u8"更新日志(&U)"));
    changelogAction->setObjectName(QStringLiteral("changelogAction"));
    connect(changelogAction, &QAction::triggered, this, [this]() {
        openHelpTextFile(QString::fromUtf8(kChangelogFileName));
    });

    auto* aboutAction = helpMenu->addAction(QString::fromStdWString(L"关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this,
            QString::fromStdWString(L"关于"),
            QString::fromUtf8(u8"飞船AI斗地主单机版 V") +
                QCoreApplication::applicationVersion() +
                QString::fromUtf8(u8"\n无障碍 Windows 单机游戏"));
    });

    helpMenu->addSeparator();
    auto* donateAction = helpMenu->addAction(QString::fromUtf8(u8"喜欢作者(&L)"));
    donateAction->setObjectName(QStringLiteral("donateAction"));
    connect(donateAction, &QAction::triggered, this, &MainWindow::showDonateDialog);
}

void MainWindow::openTopLevelMenu(QMenu* menu) {
    if (!menu) return;
    const QRect geometry = menuBar()->actionGeometry(menu->menuAction());
    const QPoint position = menuBar()->mapToGlobal(QPoint(geometry.left(), geometry.bottom()));
    menu->popup(position);
    menu->raise();
    menu->activateWindow();
    menu->setFocus(Qt::ShortcutFocusReason);
    if (!menu->actions().isEmpty()) menu->setActiveAction(menu->actions().front());
}

void MainWindow::openSubMenu(QMenu* parentMenu, QMenu* subMenu) {
    if (!parentMenu || !subMenu) return;
    const QRect geometry = parentMenu->actionGeometry(subMenu->menuAction());
    const QPoint position =
        parentMenu->mapToGlobal(QPoint(geometry.right(), geometry.top()));
    subMenu->popup(position);
    subMenu->raise();
    subMenu->activateWindow();
    subMenu->setFocus(Qt::ShortcutFocusReason);
    if (!subMenu->actions().isEmpty()) {
        subMenu->setActiveAction(subMenu->actions().front());
    }
}

bool MainWindow::isTopLevelMenuNavigationActive() const {
    return menuBar()->hasFocus() || menuBar()->activeAction() ||
           m_topLevelMenuFocusIndex >= 0;
}

void MainWindow::activateTopLevelMenuNavigation() {
    const auto actions = menuBar()->actions();
    if (actions.isEmpty()) return;
    if (m_topLevelMenuFocusIndex < 0 || m_topLevelMenuFocusIndex >= actions.size()) {
        m_topLevelMenuFocusIndex = 0;
    }
    menuBar()->setFocus(Qt::MenuBarFocusReason);
    menuBar()->setActiveAction(actions[m_topLevelMenuFocusIndex]);
}

void MainWindow::cycleTopLevelMenuFocus(bool backwards) {
    const auto actions = menuBar()->actions();
    if (actions.isEmpty()) return;

    if (m_topLevelMenuFocusIndex < 0 || m_topLevelMenuFocusIndex >= actions.size()) {
        m_topLevelMenuFocusIndex = backwards ? actions.size() - 1 : 0;
    } else {
        m_topLevelMenuFocusIndex =
            (m_topLevelMenuFocusIndex + (backwards ? -1 : 1) + actions.size()) % actions.size();
    }

    if (!menuBar()->hasFocus()) {
        menuBar()->setFocus(Qt::TabFocusReason);
    }
    menuBar()->setActiveAction(actions[m_topLevelMenuFocusIndex]);
}

void MainWindow::dismissMenusForGameAction() {
    const auto menus = findChildren<QMenu*>();
    for (auto* menu : menus) {
        menu->close();
    }
    menuBar()->setActiveAction(nullptr);
    m_topLevelMenuFocusIndex = -1;
    activateWindow();
    setFocus(Qt::ShortcutFocusReason);
}

bool MainWindow::focusNextPrevChild(bool next) {
    if (!QApplication::activePopupWidget() &&
        (!QApplication::activeModalWidget() || QApplication::activeModalWidget() == this)) {
        if (isHumanBiddingTurn()) {
            cycleBiddingControlFocus(!next);
        } else if (isTopLevelMenuNavigationActive()) {
            cycleTopLevelMenuFocus(!next);
        } else {
            setFocus(Qt::TabFocusReason);
        }
        return true;
    }
    return QMainWindow::focusNextPrevChild(next);
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);

    m_gameInfoLabel = new QLabel(QString::fromStdWString(L"按F1开战"), central);
    m_gameInfoLabel->setAccessibleName(QString::fromStdWString(L"游戏信息"));
    m_gameInfoLabel->setStyleSheet("QLabel { font-size: 14px; padding: 10px; background-color: #f0f0f0; }");
    mainLayout->addWidget(m_gameInfoLabel);

    m_bottomCardsLabel = new QLabel(central);
    m_bottomCardsLabel->setObjectName(QStringLiteral("bottomCardsLabel"));
    m_bottomCardsLabel->setWordWrap(true);
    m_bottomCardsLabel->setFocusPolicy(Qt::NoFocus);
    m_bottomCardsLabel->setVisible(false);
    mainLayout->addWidget(m_bottomCardsLabel);

    m_handView = new QListView(central);
    m_handView->setAccessibleName(QString::fromStdWString(L"手牌"));
    m_handView->setFocusPolicy(Qt::StrongFocus);
    m_handView->setModel(m_handModel.get());
    m_handView->setSelectionMode(QAbstractItemView::NoSelection);
    m_handView->setFlow(QListView::LeftToRight);
    m_handView->setWrapping(true);
    m_handView->setMinimumHeight(120);
    m_handView->setSpacing(5);
    mainLayout->addWidget(m_handView);

    m_countdownLabel = new QLabel(central);
    m_countdownLabel->setAccessibleName(QString::fromStdWString(L"本轮操作剩余时间"));
    m_countdownLabel->setVisible(false);
    mainLayout->addWidget(m_countdownLabel);

    auto* buttonLayout = new QHBoxLayout();
    m_playButton = new QPushButton(QString::fromStdWString(L"出牌(&E)"), central);
    m_playButton->setAccessibleName(QString::fromStdWString(L"出牌"));
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setEnabled(false);
    
    m_passButton = new QPushButton(QString::fromStdWString(L"过牌(&P)"), central);
    m_passButton->setAccessibleName(QString::fromStdWString(L"过牌"));
    m_passButton->setFocusPolicy(Qt::NoFocus);
    m_passButton->setEnabled(false);
    
    m_hintButton = new QPushButton(QString::fromStdWString(L"提示(&H)"), central);
    m_hintButton->setAccessibleName(QString::fromStdWString(L"出牌提示"));
    m_hintButton->setFocusPolicy(Qt::NoFocus);
    m_hintButton->setEnabled(false);
    
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::onPlayCards);
    connect(m_passButton, &QPushButton::clicked, this, &MainWindow::onPass);
    connect(m_hintButton, &QPushButton::clicked, this, &MainWindow::onHint);
    
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addWidget(m_passButton);
    buttonLayout->addWidget(m_hintButton);
    mainLayout->addLayout(buttonLayout);

    m_biddingPanel = new QWidget(central);
    m_biddingPanel->setObjectName(QStringLiteral("biddingPanel"));
    auto* biddingLayout = new QHBoxLayout(m_biddingPanel);
    for (int value = 0; value <= 3; ++value) {
        const std::wstring label = value == 0
            ? L"不叫(&0)"
            : std::to_wstring(value) + L"分(&" + std::to_wstring(value) + L")";
        auto* button = new QPushButton(QString::fromStdWString(label), m_biddingPanel);
        button->setAccessibleName(QString::fromStdWString(
            value == 0 ? L"不叫" : std::to_wstring(value) + L"分"));
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, &QPushButton::clicked, this, [this, value]() {
            onBid(value);
        });
        m_bidButtons.append(button);
        biddingLayout->addWidget(button);
    }
    m_biddingPanel->setVisible(false);
    mainLayout->addWidget(m_biddingPanel);

    m_statusLabel = new GameStatusWidget(central);
    m_statusLabel->setText(QString::fromStdWString(L"就绪"));
    m_statusLabel->setAccessibleName(QString::fromStdWString(L"就绪"));
    m_statusLabel->setAccessibleDescription({});
    statusBar()->addWidget(m_statusLabel);
    menuBar()->setFocusPolicy(Qt::StrongFocus);
}

void MainWindow::setupShortcuts() {
    setFocusPolicy(Qt::StrongFocus);
    auto addApplicationShortcut = [this](const QKeySequence& sequence, auto slot) {
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };

    addApplicationShortcut(QKeySequence(Qt::Key_F11), [this]() {
        announceCurrentTurn();
    });
    addApplicationShortcut(QKeySequence(Qt::Key_F12), [this]() {
        announceLastAction();
    });
    addApplicationShortcut(QKeySequence(Qt::ALT | Qt::Key_1), [this]() {
        dismissMenusForGameAction();
        announcePlayerAtPosition(1);
    });
    addApplicationShortcut(QKeySequence(Qt::ALT | Qt::Key_2), [this]() {
        dismissMenusForGameAction();
        announcePlayerAtPosition(2);
    });
    addApplicationShortcut(QKeySequence(Qt::ALT | Qt::Key_3), [this]() {
        dismissMenusForGameAction();
        announcePlayerAtPosition(3);
    });
    addApplicationShortcut(QKeySequence(Qt::ALT | Qt::Key_4), [this]() {
        dismissMenusForGameAction();
        announcePlayerAtPosition(4);
    });
    addApplicationShortcut(QKeySequence(Qt::Key_F2), [this]() {
        triggerBottomCardsShortcut(QStringLiteral("qt_shortcut"));
    });
    addApplicationShortcut(QKeySequence(Qt::ALT | Qt::Key_F), [this]() {
        dismissMenusForGameAction();
        announceScore();
    });
    addApplicationShortcut(QKeySequence(Qt::Key_Tab), [this]() {
        if (isHumanBiddingTurn()) {
            cycleBiddingControlFocus(false);
        } else if (isTopLevelMenuNavigationActive()) {
            cycleTopLevelMenuFocus(false);
        } else {
            setFocus(Qt::TabFocusReason);
        }
    });
    addApplicationShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Tab), [this]() {
        if (isHumanBiddingTurn()) {
            cycleBiddingControlFocus(true);
        } else if (isTopLevelMenuNavigationActive()) {
            cycleTopLevelMenuFocus(true);
        } else {
            setFocus(Qt::BacktabFocusReason);
        }
    });

}

void MainWindow::startNewGame() {
    resumeHandAccessibilityForUserAction();
    GameCommand cmd;
    cmd.type = GameCommandType::StartGame;
    auto result = m_engine.execute(cmd);
    if (result.success) {
        applyPlayerDisplayNamesToState();
        m_lastActionText.clear();
        m_lastPlayedCardsText.clear();
        refreshFromState(-1, m_engine.state().phase() == GamePhase::Playing);
        const int dealSoundDelay = playSoundsForResult(result);
        const int bottomCardsDelay = announceNewDealBottomCards(result, dealSoundDelay);

        // Start bidding with the human player or continue with AI.
        if (m_engine.state().phase() == GamePhase::Bidding) {
            if (m_engine.fullState().currentPlayer == PlayerId::Player1) {
                showBiddingControls(bottomCardsDelay == 0);
            } else {
                scheduleAiTurn(bottomCardsDelay);
            }
        }
    }
}

void MainWindow::refreshFromState(int preferredHandRow, bool restoreHandFocusSilently) {
    auto snap = m_engine.publicSnapshot();
    const auto& humanPlayer = m_engine.state().fullState().players[0];
    auto refreshHand = [this, &humanPlayer, preferredHandRow]() {
        const bool handChanged = m_handModel->setCards(humanPlayer.hand.cards());
        if (m_handModel->rowCount() > 0 &&
            (handChanged || !m_handView->currentIndex().isValid())) {
            const int targetRow = preferredHandRow >= 0
                ? std::clamp(preferredHandRow, 0, m_handModel->rowCount() - 1)
                : m_handModel->firstUnselectedRow();
            m_handView->selectionModel()->setCurrentIndex(m_handModel->index(targetRow, 0),
                                                           QItemSelectionModel::NoUpdate);
        }
    };
    if (restoreHandFocusSilently && m_handView && m_handView->selectionModel()) {
        const QSignalBlocker selectionSignalBlocker(m_handView->selectionModel());
        refreshHand();
    } else {
        refreshHand();
    }

    std::wstring info;
    switch (snap.phase) {
        case GamePhase::NotStarted: 
            info = L"按F1开战";
            m_aiTimer->stop();
            break;
        case GamePhase::Bidding:
            info = L"叫分阶段 - 轮到" + playerDisplayName(snap.currentPlayer);
            break;
        case GamePhase::Playing:
            info = L"出牌阶段 - 轮到" + playerDisplayName(snap.currentPlayer);
            info += L" | 基础分:" + std::to_wstring(snap.baseScore);
            info += L" 倍数:" + std::to_wstring(snap.currentMultiplier);
            break;
        case GamePhase::Finished: 
            info = L"牌局结束，按F1开始下一局";
            m_aiTimer->stop();
            break;
        case GamePhase::Paused:
            info = L"游戏已暂停";
            m_aiTimer->stop();
            break;
        default: 
            info = L"游戏进行中"; 
            break;
    }
    m_gameInfoLabel->setText(QString::fromStdWString(info));
    m_gameInfoLabel->setAccessibleName(QString::fromStdWString(info));

    const bool showBottomCards = snap.phase == GamePhase::Bidding &&
        snap.bottomCardsRevealed && snap.bottomCards.size() == BOTTOM_CARDS;
    if (m_bottomCardsLabel) {
        if (showBottomCards) {
            const QString bottomCardsText = QString::fromStdWString(
                L"叫分底牌：" + CardTextFormatter::formatPublicCards(snap.bottomCards));
            m_bottomCardsLabel->setText(bottomCardsText);
            m_bottomCardsLabel->setAccessibleName(bottomCardsText);
            m_bottomCardsLabel->setAccessibleDescription(bottomCardsText);
        } else {
            m_bottomCardsLabel->clear();
            m_bottomCardsLabel->setAccessibleName({});
            m_bottomCardsLabel->setAccessibleDescription({});
        }
        m_bottomCardsLabel->setVisible(showBottomCards);
    }

    bool isHumanTurn = (snap.phase == GamePhase::Playing && snap.currentPlayer == PlayerId::Player1);
    if (isHumanTurn && !m_wasHumanTurn) {
        playSound(SoundId::YourTurn);
    }
    m_wasHumanTurn = isHumanTurn;
    m_playButton->setEnabled(isHumanTurn);
    m_passButton->setEnabled(isHumanTurn);
    m_hintButton->setEnabled(isHumanTurn);

    std::wstring statusText = L"手牌:" + std::to_wstring(humanPlayer.hand.size()) + L"张";
    const QString status = QString::fromStdWString(statusText);
    m_statusLabel->setText(status);
    updateTurnCountdown(isHumanTurn);
    updateBackgroundMusic();
    updateBiddingControls();
}

void MainWindow::showBiddingControls(bool announcePrompt) {
    m_aiTimer->stop();
    updateBiddingControls();
    if (!m_biddingPanel || m_bidButtons.isEmpty()) return;
    m_biddingPanel->setVisible(true);
    m_biddingControlsVisible = true;
    for (auto* button : m_bidButtons) {
        if (button && button->isEnabled()) {
            button->setFocus(Qt::OtherFocusReason);
            break;
        }
    }
    if (announcePrompt) {
        announce(L"轮到你叫分，可按0不叫，按1、2、3叫分",
                 AnnouncementCategory::Bid, AnnouncementPriority::High);
    }
}

void MainWindow::hideBiddingControls() {
    if (m_biddingPanel) {
        m_biddingPanel->setVisible(false);
    }
    m_biddingControlsVisible = false;
}

void MainWindow::updateBiddingControls() {
    if (!m_biddingPanel) return;
    const bool show = isHumanBiddingTurn();
    m_biddingPanel->setVisible(show);
    m_biddingControlsVisible = show;

    const int currentHighest = m_engine.fullState().highestBid;
    for (int value = 0; value < m_bidButtons.size(); ++value) {
        auto* button = m_bidButtons[value];
        button->setEnabled(show && (value == 0 || value > currentHighest));
    }
}

bool MainWindow::isHumanBiddingTurn() const {
    return m_engine.state().phase() == GamePhase::Bidding &&
           m_engine.fullState().currentPlayer == PlayerId::Player1;
}

QPushButton* MainWindow::focusedBidButton() const {
    auto* focusWidget = QApplication::focusWidget();
    for (auto* button : m_bidButtons) {
        if (button && button == focusWidget) return button;
    }
    return nullptr;
}

void MainWindow::cycleBiddingControlFocus(bool backwards) {
    if (!isHumanBiddingTurn() || m_bidButtons.isEmpty()) return;

    int current = -1;
    QWidget* focusWidget = QApplication::focusWidget();
    for (int i = 0; i < m_bidButtons.size(); ++i) {
        if (m_bidButtons[i] == focusWidget) {
            current = i;
            break;
        }
    }

    const int direction = backwards ? -1 : 1;
    int index = current >= 0 ? current : (backwards ? 0 : -1);
    for (int step = 0; step < m_bidButtons.size(); ++step) {
        index = (index + direction + m_bidButtons.size()) % m_bidButtons.size();
        auto* button = m_bidButtons[index];
        if (button && button->isVisible() && button->isEnabled()) {
            button->setFocus(backwards ? Qt::BacktabFocusReason : Qt::TabFocusReason);
            return;
        }
    }
}

void MainWindow::announceBiddingPrompt() {
    announce(L"轮到你叫分，按0不叫，按1、2、3叫分",
             AnnouncementCategory::Bid, AnnouncementPriority::High);
}

void MainWindow::onAiTurn() {
    m_aiTimer->stop();
    auto phase = m_engine.state().phase();
    if (phase == GamePhase::Bidding) {
        processAiBid();
    } else if (phase == GamePhase::Playing) {
        processAiPlay();
    } else {
        m_aiTimer->stop();
    }
}

void MainWindow::processAiBid() {
    const auto currentPlayer = m_engine.fullState().currentPlayer;
    if (currentPlayer == PlayerId::Player1) {
        m_aiTimer->stop();
        showBiddingControls();
        return;
    }
    const auto difficulty = static_cast<AiDifficulty>(m_settings.aiDifficulty);
    auto ai = createAiPlayer(difficulty, m_tierWeights[static_cast<size_t>(difficulty)]);
    executeAiBidCommand(ai->decideBid(m_engine.state(), currentPlayer));
}

void MainWindow::executeAiBidCommand(const GameCommand& command) {
    auto result = m_engine.execute(command);
    if (result.success) {
        hideBiddingControls();
        rememberLastAction(result);
        const std::wstring playerName = playerDisplayName(command.playerId);
        announce(playerName, AnnouncementCategory::Bid,
                 AnnouncementPriority::Normal, false);
        const uint64_t gameId = m_engine.state().gameId();
        const uint64_t eventSequence = m_engine.fullState().eventSequence;
        const GamePhase phase = m_engine.state().phase();
        const PlayerId currentPlayer = m_engine.fullState().currentPlayer;
        QTimer::singleShot(readableAnnouncementDelayMilliseconds(playerName), this,
            [this, result, gameId, eventSequence, phase, currentPlayer]() {
                if (!playResultStillCurrent(gameId, eventSequence, phase, currentPlayer)) return;
                const bool nextIsHumanTurn = phase == GamePhase::Playing &&
                    currentPlayer == PlayerId::Player1;
                if (nextIsHumanTurn) m_wasHumanTurn = true;
                const int soundDelay = playSoundsForResult(result, nextIsHumanTurn);
                refreshFromState(-1, phase == GamePhase::Playing);
                const int bottomCardsDelay = announceNewDealBottomCards(result, soundDelay);

                if (phase == GamePhase::Bidding) {
                    if (currentPlayer != PlayerId::Player1) {
                        scheduleAiTurn(bottomCardsDelay);
                    } else {
                        m_aiTimer->stop();
                        showBiddingControls(bottomCardsDelay == 0);
                    }
                } else if (phase == GamePhase::Playing) {
                    if (currentPlayer != PlayerId::Player1) {
                        scheduleAiTurn(soundDelay);
                    } else {
                        m_aiTimer->stop();
                    }
                }
            });
    } else {
        announce(result.userMessage, AnnouncementCategory::Error, AnnouncementPriority::High);
    }
}

void MainWindow::processAiPlay() {
    const auto currentPlayer = m_engine.fullState().currentPlayer;
    if (currentPlayer == PlayerId::Player1) {
        m_aiTimer->stop();
        refreshFromState();
        return;
    }
    const auto difficulty = static_cast<AiDifficulty>(m_settings.aiDifficulty);
    auto ai = createAiPlayer(difficulty, m_tierWeights[static_cast<size_t>(difficulty)]);
    executeAiPlayCommand(ai->decidePlay(m_engine.state(), currentPlayer));
}

void MainWindow::executeAiPlayCommand(const GameCommand& command) {
    auto result = m_engine.execute(command);
    if (result.success) {
        rememberLastAction(result);
        const bool nextIsHumanTurn =
            m_engine.state().phase() == GamePhase::Playing &&
            m_engine.fullState().currentPlayer == PlayerId::Player1;
        if (nextIsHumanTurn) {
            m_wasHumanTurn = true;
        }
        refreshFromState();
        handleSuccessfulPlayResult(result, nextIsHumanTurn);
    } else {
        announce(result.userMessage, AnnouncementCategory::Error, AnnouncementPriority::High);
    }
}

void MainWindow::onPlayCards() {
    const QJsonObject traceBefore = handTraceState();
    const int preferredHandRow = m_handView && m_handView->currentIndex().isValid()
        ? m_handView->currentIndex().row()
        : -1;
    const auto selectedCards = m_handModel->selectedCards();
    if (selectedCards.empty()) {
        QJsonObject details;
        details[QStringLiteral("success")] = false;
        details[QStringLiteral("reason")] = QStringLiteral("no_selected_cards");
        traceHandAction(QStringLiteral("play_cards"), traceBefore, details);
        announce(L"请先选择要出的牌", AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        playSound(SoundId::Invalid);
        return;
    }

    GameCommand cmd;
    cmd.type = GameCommandType::PlayCards;
    cmd.playerId = PlayerId::Player1;
    for (const auto& card : selectedCards) cmd.cardIds.push_back(card.id());
    
    auto result = m_engine.execute(cmd);
    if (!result.success) {
        QJsonObject details;
        details[QStringLiteral("success")] = false;
        details[QStringLiteral("reason")] = QString::fromStdWString(result.userMessage);
        traceHandAction(QStringLiteral("play_cards"), traceBefore, details);
        const std::wstring selectedText = CardTextFormatter::formatCards(selectedCards);
        announce(L"已选" + selectedText + L"，" + result.userMessage, AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        playSound(SoundId::Invalid);
        return;
    }

    QJsonObject traceDetails;
    traceDetails[QStringLiteral("success")] = true;
    traceDetails[QStringLiteral("submitted_count")] = static_cast<int>(selectedCards.size());
    traceHandAction(QStringLiteral("play_cards"), traceBefore, traceDetails);

    rememberLastAction(result);
    suppressHandAccessibilityUntilUserAction();
    refreshFromState(preferredHandRow, true);
    handleSuccessfulPlayResult(result, false);
}

void MainWindow::onPass() {
    GameCommand cmd;
    cmd.type = GameCommandType::Pass;
    cmd.playerId = PlayerId::Player1;
    cmd.allowPassAsLeader = TurnManager::isLeader(m_engine.state());
    auto result = m_engine.execute(cmd);

    if (!result.success) {
        announce(result.userMessage, AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        playSound(SoundId::Invalid);
        return;
    }

    if (!result.events.empty()) {
        rememberLastAction(result);
    }
    refreshFromState();
    handleSuccessfulPlayResult(result, false);
}

void MainWindow::onHint() {
    const auto hint = HintService::getHint(m_engine.state(), PlayerId::Player1);
    if (hint.empty()) {
        announce(L"当前没有可以压过上一手的牌，可以按空格键过牌",
                 AnnouncementCategory::System);
        playSound(SoundId::Invalid);
        return;
    }

    m_handModel->clearSelection();
    for (int row = 0; row < m_handModel->rowCount(); ++row) {
        if (std::find(hint.begin(), hint.end(), m_handModel->cardAt(row).id()) != hint.end()) {
            m_handModel->setSelected(row, true);
        }
    }
    syncPickedCardsToView();
    playSound(SoundId::CardSelect);
    announce(L"提示牌，共" + std::to_wstring(hint.size()) + L"张，按回车出牌",
             AnnouncementCategory::CardSelection);
}

void MainWindow::onBid(int value) {
    GameCommand cmd;
    cmd.type = GameCommandType::Bid;
    cmd.playerId = PlayerId::Player1;
    cmd.bidValue = value;
    auto result = m_engine.execute(cmd);
    
    if (result.success) {
        rememberLastAction(result);
        const bool nextIsHumanTurn = m_engine.state().phase() == GamePhase::Playing &&
            m_engine.fullState().currentPlayer == PlayerId::Player1;
        if (nextIsHumanTurn) m_wasHumanTurn = true;
        const int soundDelay = playSoundsForResult(result, nextIsHumanTurn);
        
        refreshFromState(-1, m_engine.state().phase() == GamePhase::Playing);
        const int bottomCardsDelay = announceNewDealBottomCards(result, soundDelay);
        
        // Continue bidding or enter the play phase.
        if (m_engine.state().phase() == GamePhase::Bidding) {
            if (m_engine.fullState().currentPlayer != PlayerId::Player1) {
                scheduleAiTurn(bottomCardsDelay);
            } else {
                showBiddingControls(bottomCardsDelay == 0);
            }
        } else if (m_engine.state().phase() == GamePhase::Playing) {
            const uint64_t gameId = m_engine.state().gameId();
            const uint64_t eventSequence = m_engine.fullState().eventSequence;
            const PlayerId currentPlayer = m_engine.fullState().currentPlayer;
            const int transitionDelay = std::max(soundDelay, bottomCardsDelay);
            QTimer::singleShot(transitionDelay, this,
                [this, gameId, eventSequence, currentPlayer]() {
                    if (!playResultStillCurrent(gameId, eventSequence,
                                                GamePhase::Playing, currentPlayer)) return;
                    const std::wstring transitionText = L"叫分结束，开始出牌";
                    announce(transitionText, AnnouncementCategory::System,
                             AnnouncementPriority::High);
                    if (currentPlayer != PlayerId::Player1) {
                        scheduleAiTurn(readableAnnouncementDelayMilliseconds(transitionText));
                    }
                });
        }
    } else {
        announce(result.userMessage, AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        playSound(SoundId::Invalid);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
#ifdef Q_OS_WIN
    if (event->type() == QEvent::ApplicationDeactivate) {
        g_controlDown = false;
        g_shiftDown = false;
        g_altDown = false;
        g_singleFireKeyDown.fill(false);
        g_traceKeyDown.fill(false);
    }
#endif
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const auto modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
        traceKeyboardEvent(QStringLiteral("qt_event"),
                           static_cast<unsigned int>(keyEvent->key()), 0,
                           modifiers.testFlag(Qt::ControlModifier),
                           modifiers.testFlag(Qt::ShiftModifier),
                           modifiers.testFlag(Qt::AltModifier),
                           event->type() == QEvent::KeyPress,
                           keyEvent->isAutoRepeat());
        if (event->type() == QEvent::KeyPress && keyEvent->key() == Qt::Key_F4 &&
            modifiers == Qt::AltModifier) {
            m_plainAltMenuCandidate = false;
            requestApplicationExit();
            keyEvent->accept();
            return true;
        }

        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
            keyEvent->key() == Qt::Key_Alt &&
            !modifiers.testFlag(Qt::ControlModifier) &&
            !modifiers.testFlag(Qt::ShiftModifier)) {
            if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
                return QMainWindow::eventFilter(watched, event);
            }
            if (event->type() == QEvent::KeyPress && !keyEvent->isAutoRepeat()) {
                m_plainAltMenuCandidate = true;
                keyEvent->accept();
                return true;
            }
            if (event->type() == QEvent::KeyRelease) {
                if (m_plainAltMenuCandidate) activateTopLevelMenuNavigation();
                m_plainAltMenuCandidate = false;
                keyEvent->accept();
                return true;
            }
        }

        if (event->type() == QEvent::KeyPress &&
            modifiers.testFlag(Qt::AltModifier) &&
            keyEvent->key() != Qt::Key_Alt) {
            m_plainAltMenuCandidate = false;
        }
    }

    if (event->type() == QEvent::KeyPress && watched == menuBar()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const auto modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
        if (modifiers == Qt::NoModifier &&
            (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ||
             keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Down ||
             keyEvent->key() == Qt::Key_Right)) {
            QAction* action = menuBar()->activeAction();
            if (!action && !menuBar()->actions().isEmpty()) {
                action = menuBar()->actions().front();
                menuBar()->setActiveAction(action);
            }
            if (action && action->menu()) {
                openTopLevelMenu(action->menu());
                keyEvent->accept();
                return true;
            }
            if (action) {
                action->trigger();
                keyEvent->accept();
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* menu = qobject_cast<QMenu*>(watched);
        if (menu) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const auto modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
            if (modifiers == Qt::NoModifier &&
                (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ||
                 keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Right)) {
                QAction* action = menu->activeAction();
                if (action && action->menu()) {
                    openSubMenu(menu, action->menu());
                    keyEvent->accept();
                    return true;
                }
            }
        }
    }

    if (QApplication::activePopupWidget()) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
            return QMainWindow::eventFilter(watched, event);
        }
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const auto modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
        const bool altQuery = (modifiers == Qt::AltModifier) &&
            ((keyEvent->key() >= Qt::Key_1 && keyEvent->key() <= Qt::Key_4) ||
             keyEvent->key() == Qt::Key_D ||
             keyEvent->key() == Qt::Key_F);
        const bool primaryShortcut =
            keyEvent->key() == Qt::Key_F1 ||
            keyEvent->key() == Qt::Key_F5 ||
            keyEvent->key() == Qt::Key_F11 ||
            keyEvent->key() == Qt::Key_F12 ||
            keyEvent->key() == Qt::Key_Left ||
            keyEvent->key() == Qt::Key_Right ||
            keyEvent->key() == Qt::Key_Home ||
            keyEvent->key() == Qt::Key_End ||
            keyEvent->key() == Qt::Key_Up ||
            keyEvent->key() == Qt::Key_Down ||
            keyEvent->key() == Qt::Key_Return ||
            keyEvent->key() == Qt::Key_Enter ||
            (keyEvent->key() >= Qt::Key_0 && keyEvent->key() <= Qt::Key_3);
        if (altQuery || primaryShortcut) {
            keyEvent->accept();
        }
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
            return QMainWindow::eventFilter(watched, event);
        }
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (handleKeyPress(keyEvent)) {
            keyEvent->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (handleKeyPress(event)) return;
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (handleWindowsMessage(message, result)) return true;
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(eventType);
    return handleWindowsMessage(message, result);
}

bool MainWindow::handleWindowsMessage(void* message, qintptr* result) {
#ifdef Q_OS_WIN
    auto* msg = static_cast<MSG*>(message);
    if (msg && msg->message == kKeyboardTraceMessage) {
        const LPARAM flags = msg->lParam;
        traceKeyboardEvent(QStringLiteral("physical_hook"),
                           static_cast<unsigned int>(msg->wParam),
                           static_cast<unsigned int>((flags >> kHookScanShift) & 0xFF),
                           (flags & kHookCtrlFlag) != 0,
                           (flags & kHookShiftFlag) != 0,
                           (flags & kHookAltFlag) != 0,
                           (flags & kHookKeyUpFlag) == 0);
        if (result) *result = 1;
        return true;
    }

    if (msg && msg->message == kKeyboardHookMessage &&
        static_cast<unsigned int>(msg->wParam) == VK_F4 &&
        (msg->lParam & kHookAltFlag) != 0) {
        requestApplicationExit();
        if (result) *result = 1;
        return true;
    }

    if (msg && msg->message == kKeyboardHookMessage &&
        static_cast<unsigned int>(msg->wParam) == VK_F1 &&
        (msg->lParam & (kHookCtrlFlag | kHookShiftFlag | kHookAltFlag)) == 0) {
        if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
            return false;
        }
        triggerBattleShortcut();
        if (result) *result = 1;
        return true;
    }

    if (msg && msg->message == kKeyboardHookMessage &&
        static_cast<unsigned int>(msg->wParam) == VK_F11 &&
        (msg->lParam & (kHookCtrlFlag | kHookShiftFlag | kHookAltFlag)) == 0) {
        if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
            return false;
        }
        announceCurrentTurn();
        if (result) *result = 1;
        return true;
    }

    if (msg && msg->message == kKeyboardHookMessage &&
        static_cast<unsigned int>(msg->wParam) == VK_F2 &&
        (msg->lParam & (kHookCtrlFlag | kHookShiftFlag | kHookAltFlag)) == 0) {
        if (!triggerBottomCardsShortcut(QStringLiteral("physical_hook_message"))) {
            return false;
        }
        if (result) *result = 1;
        return true;
    }

    if (msg && msg->message == kKeyboardHookMessage &&
        static_cast<unsigned int>(msg->wParam) == VK_F5 &&
        (msg->lParam & (kHookCtrlFlag | kHookShiftFlag | kHookAltFlag)) == 0) {
        if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
            return false;
        }
        dismissMenusForGameAction();
        openSettingsDialog();
        if (result) *result = 1;
        return true;
    }

    if (shouldYieldKeyboardHandlingToFocusedWidget()) return false;
    if (msg && msg->message == WM_HOTKEY) {
        switch (static_cast<int>(msg->wParam)) {
        case kHotkeyF11:
            announceCurrentTurn();
            if (result) *result = 1;
            return true;
        case kHotkeyF12:
            announceLastAction();
            if (result) *result = 1;
            return true;
        case kHotkeyAlt1:
            announcePlayerAtPosition(1);
            if (result) *result = 1;
            return true;
        case kHotkeyAlt2:
            announcePlayerAtPosition(2);
            if (result) *result = 1;
            return true;
        case kHotkeyAlt3:
            announcePlayerAtPosition(3);
            if (result) *result = 1;
            return true;
        case kHotkeyAlt4:
            announcePlayerAtPosition(4);
            if (result) *result = 1;
            return true;
        case kHotkeyAltF:
            announceScore();
            if (result) *result = 1;
            return true;
        default:
            break;
        }
    }

    if (msg && msg->message == kKeyboardHookMessage) {
        const LPARAM flags = msg->lParam;
        const bool handled = handleNativeShortcut(
            static_cast<unsigned int>(msg->wParam),
            (flags & kHookCtrlFlag) != 0,
            (flags & kHookShiftFlag) != 0,
            (flags & kHookAltFlag) != 0,
            static_cast<unsigned int>((flags >> kHookScanShift) & 0xFF));
        if (handled) {
            if (result) *result = 1;
            return true;
        }
    }

    if (msg && (msg->message == WM_KEYDOWN ||
                msg->message == WM_SYSKEYDOWN ||
                msg->message == WM_SYSCHAR)) {
        if ((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
            isSingleFireKey(static_cast<DWORD>(msg->wParam)) &&
            (static_cast<quintptr>(msg->lParam) & (quintptr{1} << 30)) != 0) {
            if (result) *result = 1;
            return true;
        }
        const bool altDown = msg->message == WM_SYSKEYDOWN ||
            msg->message == WM_SYSCHAR ||
            (HIWORD(msg->lParam) & KF_ALTDOWN) != 0 ||
            (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool handled = handleNativeShortcut(
            static_cast<unsigned int>(msg->wParam),
            (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            (GetKeyState(VK_SHIFT) & 0x8000) != 0,
            altDown,
            static_cast<unsigned int>((HIWORD(msg->lParam) & 0xFF)));
        if (handled) {
            if (result) *result = 1;
            return true;
        }
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif

    return false;
}

bool MainWindow::handleNativeShortcut(
    unsigned int virtualKey,
    bool ctrlDown,
    bool shiftDown,
    bool altDown,
    unsigned int scanCode) {
#ifdef Q_OS_WIN
    if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != this) {
        return false;
    }
    if (virtualKey >= 'a' && virtualKey <= 'z') {
        virtualKey -= ('a' - 'A');
    }
    Q_UNUSED(scanCode);

    const bool noModifier = !ctrlDown && !shiftDown && !altDown;
    const auto phase = m_engine.state().phase();

    if (virtualKey == VK_F4 && altDown && !ctrlDown && !shiftDown) {
        requestApplicationExit();
        return true;
    }

    if (virtualKey == VK_F1 && noModifier) {
        triggerBattleShortcut();
        return true;
    }

    if (virtualKey == VK_F2 && noModifier) {
        return triggerBottomCardsShortcut(QStringLiteral("native_key_message"));
    }

    if (shouldYieldKeyboardHandlingToFocusedWidget()) return false;

    if (virtualKey == VK_F5 && noModifier) {
        openSettingsDialog();
        return true;
    }
    if (virtualKey == VK_F11 && noModifier) {
        announceCurrentTurn();
        return true;
    }
    if (virtualKey == VK_F12 && noModifier) {
        announceLastAction();
        return true;
    }

    if (altDown && !ctrlDown && !shiftDown) {
        if (virtualKey == 'X') {
            close();
            return true;
        }
        if (virtualKey == '1') {
            announcePlayerAtPosition(1);
            return true;
        }
        if (virtualKey == '2') {
            announcePlayerAtPosition(2);
            return true;
        }
        if (virtualKey == '3') {
            announcePlayerAtPosition(3);
            return true;
        }
        if (virtualKey == '4') {
            announcePlayerAtPosition(4);
            return true;
        }
        if (virtualKey == 'F') {
            announceScore();
            return true;
        }
        return false;
    }

    if (phase == GamePhase::Bidding &&
        m_engine.fullState().currentPlayer == PlayerId::Player1 &&
        noModifier &&
        virtualKey >= '0' && virtualKey <= '3') {
        onBid(static_cast<int>(virtualKey - '0'));
        return true;
    }
    if (isHumanBiddingTurn() && noModifier &&
        (virtualKey == VK_RETURN || virtualKey == VK_SPACE)) {
        auto* button = focusedBidButton();
        if (!button) {
            cycleBiddingControlFocus(false);
            button = focusedBidButton();
        }
        const int bidValue = m_bidButtons.indexOf(button);
        if (button && button->isEnabled() && bidValue >= 0) {
            onBid(bidValue);
            return true;
        }
        announceBiddingPrompt();
        return true;
    }

    if (m_handView && m_handModel && m_handModel->rowCount() > 0 && !altDown) {
        const bool handBrowsing =
            ((virtualKey == VK_LEFT || virtualKey == VK_RIGHT) &&
             ((shiftDown && !ctrlDown) || noModifier)) ||
            ((virtualKey == VK_HOME || virtualKey == VK_END) && noModifier);
        if (handBrowsing) resumeHandAccessibilityForUserAction();
        int row = m_handView->currentIndex().isValid() ? m_handView->currentIndex().row() : 0;
        if ((virtualKey == VK_LEFT || virtualKey == VK_RIGHT) && shiftDown && !ctrlDown) {
            const int target = virtualKey == VK_LEFT
                ? m_handModel->previousUnselectedRow(row)
                : m_handModel->nextUnselectedRow(row);
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if ((virtualKey == VK_LEFT || virtualKey == VK_RIGHT) && noModifier) {
            const int target = virtualKey == VK_LEFT
                ? m_handModel->previousBrowsableGroupStartRow(row)
                : m_handModel->nextBrowsableGroupStartRow(row);
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if (virtualKey == VK_HOME && noModifier) {
            const int target = m_handModel->firstUnselectedRow();
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if (virtualKey == VK_END && noModifier) {
            const int target = m_handModel->lastBrowsableGroupStartRow();
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }

        const bool canPickCards = phase == GamePhase::Playing;
        if (virtualKey == VK_UP && noModifier && canPickCards) {
            takeCurrentCard();
            return true;
        }
        if (virtualKey == VK_UP && ctrlDown && !shiftDown && canPickCards) {
            takeCurrentGroup();
            return true;
        }
        if (virtualKey == VK_DOWN && noModifier && canPickCards) {
            putDownNextPickedCard();
            return true;
        }
        if (virtualKey == VK_DOWN && ctrlDown && !shiftDown && canPickCards) {
            putDownAllCards();
            return true;
        }
    }

    if (virtualKey == VK_SPACE && noModifier &&
        phase == GamePhase::Playing &&
        m_engine.fullState().currentPlayer == PlayerId::Player1) {
        onPass();
        return true;
    }

    if (virtualKey == VK_RETURN && noModifier &&
        phase == GamePhase::Playing &&
        m_engine.fullState().currentPlayer == PlayerId::Player1) {
        onPlayCards();
        return true;
    }
#else
    Q_UNUSED(virtualKey);
    Q_UNUSED(ctrlDown);
    Q_UNUSED(shiftDown);
    Q_UNUSED(altDown);
    Q_UNUSED(scanCode);
#endif

    return false;
}

void MainWindow::installKeyboardHook() {
#ifdef Q_OS_WIN
    if (qApp->property("fpdz.suppressKeyboardHook").toBool()) return;
    g_keyboardHookWindow = reinterpret_cast<HWND>(winId());
    if (!g_keyboardHook) {
        g_keyboardHook = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            lowLevelKeyboardProc,
            GetModuleHandleW(nullptr),
            0);
        if (!g_keyboardHook) {
            g_keyboardHook = SetWindowsHookExW(
                WH_KEYBOARD_LL,
                lowLevelKeyboardProc,
                nullptr,
                0);
        }
        if (!g_keyboardHook) {
            std::wstring message = L"FourPlayerDoudizhu keyboard hook install failed, error ";
            message += std::to_wstring(GetLastError());
            message += L"\n";
            OutputDebugStringW(message.c_str());
        }
    }
#endif
}

void MainWindow::uninstallKeyboardHook() {
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(winId());
    if (g_keyboardHookWindow == window) {
        g_keyboardHookWindow = nullptr;
    }
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_controlDown = false;
    g_shiftDown = false;
    g_altDown = false;
    g_singleFireKeyDown.fill(false);
    g_traceKeyDown.fill(false);
#endif
}

void MainWindow::registerSystemHotkeys() {
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(winId());
    if (!window) return;

    unregisterSystemHotkeys();

    auto tryRegister = [this, window](int id, UINT modifiers, UINT virtualKey, const wchar_t* name) {
        if (RegisterHotKey(window, id, modifiers, virtualKey)) {
            m_registeredHotkeys.append(id);
            return;
        }

        std::wstring message = L"FourPlayerDoudizhu hotkey register failed: ";
        message += name;
        message += L", error ";
        message += std::to_wstring(GetLastError());
        message += L"\n";
        OutputDebugStringW(message.c_str());
    };

    tryRegister(kHotkeyF11, MOD_NOREPEAT, VK_F11, L"F11");
    tryRegister(kHotkeyF12, MOD_NOREPEAT, VK_F12, L"F12");
    tryRegister(kHotkeyAlt1, MOD_ALT | MOD_NOREPEAT, '1', L"Alt+1");
    tryRegister(kHotkeyAlt2, MOD_ALT | MOD_NOREPEAT, '2', L"Alt+2");
    tryRegister(kHotkeyAlt3, MOD_ALT | MOD_NOREPEAT, '3', L"Alt+3");
    tryRegister(kHotkeyAlt4, MOD_ALT | MOD_NOREPEAT, '4', L"Alt+4");
    tryRegister(kHotkeyAltF, MOD_ALT | MOD_NOREPEAT, 'F', L"Alt+F");
#endif
}

void MainWindow::unregisterSystemHotkeys() {
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(winId());
    if (!window) return;
    for (int id : m_registeredHotkeys) {
        UnregisterHotKey(window, id);
    }
    m_registeredHotkeys.clear();
#endif
}

bool MainWindow::handleKeyPress(QKeyEvent* event) {
    auto phase = m_engine.state().phase();
    const auto modifiers = event->modifiers();
    const auto navigationModifiers = modifiers & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Escape && navigationModifiers == Qt::NoModifier) {
        if (phase != GamePhase::NotStarted) returnToMainScreen();
        return true;
    }
    const bool singleFireKey = event->key() == Qt::Key_F1 ||
        event->key() == Qt::Key_F5 || event->key() == Qt::Key_F11 ||
        event->key() == Qt::Key_F12 || event->key() == Qt::Key_Home ||
        event->key() == Qt::Key_End || event->key() == Qt::Key_Up ||
        event->key() == Qt::Key_Down || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter;
    if (singleFireKey && event->isAutoRepeat()) return true;

    if (navigationModifiers == Qt::AltModifier) {
        if (event->key() == Qt::Key_X) {
            close();
            return true;
        }
    }

    if (event->key() == Qt::Key_F1 && navigationModifiers == Qt::NoModifier) {
        if (!event->isAutoRepeat()) {
            triggerBattleShortcut();
        }
        return true;
    }
    if (event->key() == Qt::Key_F5 && navigationModifiers == Qt::NoModifier) {
        openSettingsDialog();
        return true;
    }
    if (event->key() == Qt::Key_F2 && navigationModifiers == Qt::NoModifier) {
        return triggerBottomCardsShortcut(QStringLiteral("qt_event"));
    }
    if (event->key() == Qt::Key_F11 && navigationModifiers == Qt::NoModifier) {
        announceCurrentTurn();
        return true;
    }
    if (event->key() == Qt::Key_F12 && navigationModifiers == Qt::NoModifier) {
        announceLastAction();
        return true;
    }

    if (phase == GamePhase::Bidding && m_engine.fullState().currentPlayer == PlayerId::Player1 &&
        navigationModifiers == Qt::NoModifier &&
        event->key() >= Qt::Key_0 && event->key() <= Qt::Key_3) {
        onBid(event->key() - Qt::Key_0);
        return true;
    }
    if (isHumanBiddingTurn() && navigationModifiers == Qt::NoModifier &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
         event->key() == Qt::Key_Space)) {
        auto* button = focusedBidButton();
        if (!button) {
            cycleBiddingControlFocus(false);
            button = focusedBidButton();
        }
        const int bidValue = m_bidButtons.indexOf(button);
        if (button && button->isEnabled() && bidValue >= 0) {
            onBid(bidValue);
        } else {
            announceBiddingPrompt();
        }
        return true;
    }

    if ((modifiers & Qt::AltModifier) && !(modifiers & Qt::KeypadModifier)) {
        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_4) {
            announcePlayerAtPosition(event->key() - Qt::Key_1 + 1);
            return true;
        }
        if (event->key() == Qt::Key_F) {
            announceScore();
            return true;
        }
    }

    bool handControlDown = navigationModifiers.testFlag(Qt::ControlModifier);
#ifdef Q_OS_WIN
    if (!qApp->property("fpdz.suppressKeyboardHook").toBool()) {
        handControlDown = handControlDown || isControlDown();
    }
#endif
    bool handShiftDown = navigationModifiers.testFlag(Qt::ShiftModifier);
    bool handAltDown = navigationModifiers.testFlag(Qt::AltModifier);
#ifdef Q_OS_WIN
    if (!qApp->property("fpdz.suppressKeyboardHook").toBool()) {
        handShiftDown = handShiftDown || isShiftDown();
        handAltDown = handAltDown || isAltDown();
    }
#endif
    const bool handNoModifier = !handControlDown && !handShiftDown && !handAltDown;

    if (m_handView && m_handModel && m_handModel->rowCount() > 0) {
        const bool handBrowsing =
            ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) &&
             ((handShiftDown && !handControlDown && !handAltDown) || handNoModifier)) ||
            ((event->key() == Qt::Key_Home || event->key() == Qt::Key_End) && handNoModifier);
        if (handBrowsing) resumeHandAccessibilityForUserAction();
        int row = m_handView->currentIndex().isValid() ? m_handView->currentIndex().row() : 0;
        if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            && handShiftDown && !handControlDown && !handAltDown) {
            const int target = event->key() == Qt::Key_Left
                ? m_handModel->previousUnselectedRow(row)
                : m_handModel->nextUnselectedRow(row);
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            && handNoModifier) {
            const int target = event->key() == Qt::Key_Left
                ? m_handModel->previousBrowsableGroupStartRow(row)
                : m_handModel->nextBrowsableGroupStartRow(row);
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if (event->key() == Qt::Key_Home && handNoModifier) {
            const int target = m_handModel->firstUnselectedRow();
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if (event->key() == Qt::Key_End && handNoModifier) {
            const int target = m_handModel->lastBrowsableGroupStartRow();
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        const bool canPickCards = phase == GamePhase::Playing;
        if (event->key() == Qt::Key_Up && handControlDown && !handShiftDown &&
            !handAltDown && canPickCards) {
            takeCurrentGroup();
            return true;
        }
        if (event->key() == Qt::Key_Up && handNoModifier && canPickCards) {
            takeCurrentCard();
            return true;
        }
        if (event->key() == Qt::Key_Down && handControlDown && !handShiftDown &&
            !handAltDown && canPickCards) {
            putDownAllCards();
            return true;
        }
        if (event->key() == Qt::Key_Down && handNoModifier && canPickCards) {
            putDownNextPickedCard();
            return true;
        }
    }

    if (event->key() == Qt::Key_Space
        && navigationModifiers == Qt::NoModifier
        && phase == GamePhase::Playing
        && m_engine.fullState().currentPlayer == PlayerId::Player1) {
        onPass();
        return true;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && navigationModifiers == Qt::NoModifier
        && phase == GamePhase::Playing
        && m_engine.fullState().currentPlayer == PlayerId::Player1) {
        onPlayCards();
        return true;
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_forceExitRequested) {
        event->accept();
        return;
    }
    const auto phase = m_engine.state().phase();
    if (phase == GamePhase::Bidding || phase == GamePhase::Playing ||
        phase == GamePhase::Paused) {
        auto result = QMessageBox::question(this,
            QString::fromStdWString(L"确认退出"),
            QString::fromStdWString(L"牌局正在进行，确认要退出吗？"));
        if (result != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_aiTimer->stop();
    event->accept();
}


void MainWindow::loadSettings() {
    if (m_settingsRepo && m_settingsRepo->load(DataPaths::settingsFile())) {
        m_settings = AppSettings::fromJson(m_settingsRepo->data());
    } else {
        m_settings = AppSettings();
        m_settings.normalize();
    }
}

void MainWindow::saveSettings() {
    if (!m_settingsRepo) return;
    DataPaths::ensureDirectories();
    m_settings.normalize();
    m_settingsRepo->setData(m_settings.toJson());
    m_settingsRepo->save(DataPaths::settingsFile());
}

void MainWindow::applyPlayerDisplayNamesToState() {
    m_settings.normalize();
    auto& players = m_engine.state().fullState().players;
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        players[static_cast<size_t>(i)].name =
            m_settings.playerNames[static_cast<size_t>(i)].toStdWString();
    }
}

std::wstring MainWindow::playerDisplayName(PlayerId playerId) const {
    const int index = static_cast<int>(playerId);
    if (index >= 0 && index < PLAYER_COUNT) {
        const auto name = m_settings.playerNames[static_cast<size_t>(index)].trimmed();
        if (!name.isEmpty()) return name.toStdWString();
    }
    return playerIdDisplayName(playerId);
}

std::wstring MainWindow::formatEventForAnnouncement(const GameEvent& event) const {
    switch (event.type) {
    case GameEventType::BidRequested:
        return playerDisplayName(event.playerId) + L"，请叫分";
    case GameEventType::PlayerBid:
        return event.bidValue == 0
            ? playerDisplayName(event.playerId) + L"不叫"
            : playerDisplayName(event.playerId) + L"叫" +
                  std::to_wstring(event.bidValue) + L"分";
    case GameEventType::LandlordDetermined:
        return playerDisplayName(event.playerId) + L"成为地主！";
    case GameEventType::TurnChanged:
        return L"轮到" + playerDisplayName(event.playerId);
    case GameEventType::CardsPlayed:
        return playerDisplayName(event.playerId) + L"出了" +
               CardTextFormatter::formatPlayedCards(event.pattern, event.cards);
    case GameEventType::PlayerPassed:
        return playerDisplayName(event.playerId);
    case GameEventType::TrickReset:
        return {};
    case GameEventType::PlayerLowCards:
        return playerDisplayName(event.playerId) + L"还剩" +
               std::to_wstring(event.remainingCards) + L"张牌！";
    default:
        return event.message;
    }
}

void MainWindow::presentPlayedCards(const GameEvent& event) {
    if (event.type != GameEventType::CardsPlayed || event.cards.empty()) return;

    const std::wstring playerName = playerDisplayName(event.playerId);
    const std::wstring visibleText = playerName + L"，" +
        CardTextFormatter::formatPlayedCards(event.pattern, event.cards);
    if (m_statusLabel) {
        const QString status = QString::fromStdWString(visibleText);
        m_statusLabel->setText(status);
    }
    QTimer::singleShot(0, this, [this, playerName, playerId = event.playerId]() {
        announce(playerName, AnnouncementCategory::Play,
                  AnnouncementPriority::Normal, false);
        if (m_diagnosticTrace) {
            m_diagnosticTrace->record(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("played_cards_name_announced")},
                {QStringLiteral("game_id"), static_cast<qint64>(m_engine.state().gameId())},
                {QStringLiteral("player"), static_cast<int>(playerId)}});
        }
    });
}

bool MainWindow::playResultStillCurrent(uint64_t gameId, uint64_t eventSequence,
                                        GamePhase phase, PlayerId currentPlayer) const {
    return m_engine.state().gameId() == gameId &&
           m_engine.fullState().eventSequence == eventSequence &&
           m_engine.state().phase() == phase &&
           m_engine.fullState().currentPlayer == currentPlayer;
}

void MainWindow::handleSuccessfulPlayResult(const CommandResult& result, bool appendYourTurn) {
    if (result.events.empty()) return;

    const GameEvent& event = result.events[0];
    const std::wstring actionText = formatEventForAnnouncement(event);
    const bool isPlayerAction = event.type == GameEventType::CardsPlayed ||
                                event.type == GameEventType::PlayerPassed;
    if (!isPlayerAction) {
        if (!actionText.empty()) {
            announce(actionText,
                     event.type == GameEventType::PlayerPassed
                         ? AnnouncementCategory::Pass
                         : AnnouncementCategory::Play);
        }
        const int soundDelay = playSoundsForResult(result, appendYourTurn);
        const int actionDelay = actionText.empty()
            ? soundDelay
            : std::max(readableAnnouncementDelayMilliseconds(actionText), soundDelay);
        if (m_engine.state().phase() == GamePhase::Finished) {
            m_aiTimer->stop();
            handleFinishedResult(result, actionDelay);
        } else if (m_engine.fullState().currentPlayer != PlayerId::Player1) {
            scheduleAiTurn(actionDelay);
        } else {
            m_aiTimer->stop();
        }
        return;
    }

    if (event.type == GameEventType::CardsPlayed) {
        presentPlayedCards(event);
    } else {
        const std::wstring playerName = playerDisplayName(event.playerId);
        QTimer::singleShot(0, this, [this, playerName]() {
            announce(playerName, AnnouncementCategory::Pass,
                     AnnouncementPriority::Normal, false);
        });
    }
    const uint64_t gameId = m_engine.state().gameId();
    const uint64_t eventSequence = m_engine.fullState().eventSequence;
    const GamePhase phase = m_engine.state().phase();
    const PlayerId currentPlayer = m_engine.fullState().currentPlayer;
    const int nameDelay = playerActionNameDelayMilliseconds();

    QTimer::singleShot(nameDelay, this,
        [this, result, appendYourTurn, gameId, eventSequence, phase, currentPlayer,
         eventType = event.type, playerId = event.playerId,
         cardCount = static_cast<int>(event.cards.size())]() {
            if (!playResultStillCurrent(gameId, eventSequence, phase, currentPlayer)) {
                if (m_diagnosticTrace && eventType == GameEventType::CardsPlayed) {
                    m_diagnosticTrace->record(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("played_cards_sequence_cancelled")},
                        {QStringLiteral("game_id"), static_cast<qint64>(gameId)},
                        {QStringLiteral("event_sequence"), static_cast<qint64>(eventSequence)}});
                }
                return;
            }

            if (m_diagnosticTrace && eventType == GameEventType::CardsPlayed) {
                m_diagnosticTrace->record(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("played_cards_sound_started")},
                    {QStringLiteral("game_id"), static_cast<qint64>(gameId)},
                    {QStringLiteral("event_sequence"), static_cast<qint64>(eventSequence)},
                    {QStringLiteral("player"), static_cast<int>(playerId)},
                    {QStringLiteral("card_count"), cardCount}});
            }
            const int soundDelay = playSoundsForResult(result, appendYourTurn);
            if (phase == GamePhase::Finished) {
                m_aiTimer->stop();
                handleFinishedResult(result, soundDelay);
            } else if (currentPlayer != PlayerId::Player1) {
                scheduleAiTurn(soundDelay);
            } else {
                m_aiTimer->stop();
            }
        });
}

bool MainWindow::openHelpTextFile(const QString& fileName) {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        applicationDir + QStringLiteral("/docs/") + fileName,
        applicationDir + QStringLiteral("/resources/docs/") + fileName
    };
    for (const QString& candidate : candidates) {
        const QFileInfo file(candidate);
        if (!file.exists() || !file.isFile() || file.size() <= 0) continue;
        qApp->setProperty("fpdz.lastHelpFilePath", file.absoluteFilePath());
        if (qApp->property("fpdz.suppressExternalHelp").toBool()) return true;
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()))) return true;
        QMessageBox::warning(
            this, QString::fromUtf8(u8"无法打开说明文件"),
            QString::fromUtf8(u8"Windows 无法打开这个 TXT 文件：\n") +
                QDir::toNativeSeparators(file.absoluteFilePath()));
        return false;
    }
    QMessageBox::warning(
        this, QString::fromUtf8(u8"说明文件不存在"),
        QString::fromUtf8(u8"没有找到 TXT 说明文件：\n") + fileName);
    return false;
}

void MainWindow::showDonateDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8(u8"喜欢作者"));
    dialog.setAccessibleName(QString::fromUtf8(u8"喜欢作者微信付款二维码"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* message = new QLabel(
        QString::fromUtf8(u8"喜欢请随心打赏一下，鼓励开发者继续完善增加软件好玩的功能。"),
        &dialog);
    message->setWordWrap(true);
    message->setAccessibleName(message->text());
    layout->addWidget(message);

    auto* imageLabel = new QLabel(&dialog);
    imageLabel->setObjectName(QStringLiteral("donateQrImage"));
    imageLabel->setAccessibleName(QString::fromUtf8(u8"微信付款二维码图片"));
    imageLabel->setAlignment(Qt::AlignCenter);
    const QPixmap original(QStringLiteral(":/images/donate.jpg"));
    if (!original.isNull()) {
        imageLabel->setPixmap(original.scaled(414, 562, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
    } else {
        imageLabel->setText(QString::fromUtf8(u8"二维码图片无法读取。"));
    }
    layout->addWidget(imageLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::checkForUpdates(bool manual) {
    if (!m_updateService) return;
    if (m_updateCheckInProgress) {
        if (manual) {
            QMessageBox::information(this, QString::fromUtf8(u8"检查更新"),
                                     QString::fromUtf8(u8"正在检查更新，请稍候。"));
        }
        return;
    }
    m_manualUpdateCheck = manual;
    m_updateCheckInProgress = true;
    if (manual) statusBar()->showMessage(QString::fromUtf8(u8"正在检查更新……"));
    m_updateService->checkForUpdates(QCoreApplication::applicationVersion());
}

void MainWindow::handleUpdateCheckFinished(const UpdateCheckResult& result) {
    const bool manual = m_manualUpdateCheck;
    m_manualUpdateCheck = false;
    m_updateCheckInProgress = false;
    statusBar()->clearMessage();

    if (!result.success) {
        if (manual) {
            QMessageBox::warning(this, QString::fromUtf8(u8"检查更新失败"),
                                 result.errorMessage);
        }
        return;
    }

    if (!manual) {
        m_settings.lastAutomaticUpdateCheckDate =
            QDate::currentDate().toString(Qt::ISODate);
        saveSettings();
    }
    if (!result.updateAvailable) {
        if (manual) {
            QMessageBox::information(this, QString::fromUtf8(u8"检查更新"),
                QString::fromUtf8(u8"当前已是最新版本：") +
                    QCoreApplication::applicationVersion());
        }
        return;
    }
    if (!manual && m_settings.ignoredUpdateVersion == result.latestVersion) return;

    QMessageBox box(QMessageBox::Information,
                    QString::fromUtf8(u8"发现新版本 ") + result.latestVersion,
                    result.releaseNotes.trimmed().isEmpty()
                        ? QString::fromUtf8(u8"发现新的稳定版本，是否立即更新？")
                        : result.releaseNotes,
                    QMessageBox::NoButton, this);
    box.setAccessibleName(QString::fromUtf8(u8"软件更新提示"));
    auto* updateButton = box.addButton(QString::fromUtf8(u8"立即更新"),
                                       QMessageBox::AcceptRole);
    auto* ignoreButton = box.addButton(QString::fromUtf8(u8"忽略此版本"),
                                       QMessageBox::ActionRole);
    auto* disableButton = box.addButton(QString::fromUtf8(u8"以后不再提醒"),
                                        QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == updateButton) {
        startUpdateDownload(result);
    } else if (box.clickedButton() == ignoreButton) {
        m_settings.ignoredUpdateVersion = result.latestVersion;
        saveSettings();
    } else if (box.clickedButton() == disableButton) {
        m_settings.automaticUpdateChecks = false;
        saveSettings();
    }
}

void MainWindow::startUpdateDownload(const UpdateCheckResult& update) {
    if (!m_updateService || m_updateProgressDialog) return;
    m_updateProgressDialog = new QProgressDialog(
        QString::fromUtf8(u8"正在下载安装包并校验 SHA-256……"),
        QString::fromUtf8(u8"取消"), 0, 0, this);
    m_updateProgressDialog->setWindowTitle(QString::fromUtf8(u8"软件更新"));
    m_updateProgressDialog->setAccessibleName(QString::fromUtf8(u8"更新下载进度"));
    m_updateProgressDialog->setWindowModality(Qt::WindowModal);
    m_updateProgressDialog->setMinimumDuration(0);
    connect(m_updateProgressDialog, &QProgressDialog::canceled,
            m_updateService.get(), &UpdateService::cancel);
    m_updateProgressDialog->show();
    m_updateService->downloadAndVerify(update);
}

void MainWindow::handleUpdateDownloadFinished(const QString& installerPath,
                                              const QString& errorMessage) {
    if (m_updateProgressDialog) {
        m_updateProgressDialog->close();
        m_updateProgressDialog->deleteLater();
        m_updateProgressDialog = nullptr;
    }
    if (!errorMessage.isEmpty()) {
        QMessageBox::critical(this, QString::fromUtf8(u8"更新失败"), errorMessage);
        return;
    }
    const QFileInfo installer(installerPath);
    if (!installer.exists() || installer.suffix().compare(QStringLiteral("exe"),
                                                           Qt::CaseInsensitive) != 0) {
        QMessageBox::critical(this, QString::fromUtf8(u8"更新失败"),
                              QString::fromUtf8(u8"安装包不存在或不是 EXE 文件。"));
        return;
    }
    if (!QProcess::startDetached(installer.absoluteFilePath(),
                                 {QStringLiteral("/CLOSEAPPLICATIONS"),
                                  QStringLiteral("/RESTARTAPPLICATIONS")})) {
        QMessageBox::critical(this, QString::fromUtf8(u8"更新失败"),
                              QString::fromUtf8(u8"无法启动更新安装包。"));
        return;
    }
    m_forceExitRequested = true;
    QCoreApplication::quit();
}

void MainWindow::openSettingsDialog() {
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted) return;

    m_settings = dialog.settings();
    saveSettings();
    m_aiTimer->setInterval(aiDelayMilliseconds(m_settings.aiDelay));
    if (m_sound) {
        m_sound->setEnabled(m_settings.soundEnabled);
        m_sound->setVolume(m_settings.soundVolume);
        m_sound->setCategorySettings(m_settings.soundCategories);
        m_sound->setMusicEnabled(m_settings.backgroundMusicEnabled);
        m_sound->setMusicVolume(m_settings.backgroundMusicVolume);
        updateBackgroundMusic();
    }
    updateTurnCountdown(m_engine.state().phase() == GamePhase::Playing &&
                        m_engine.fullState().currentPlayer == PlayerId::Player1);
    announce(L"设置已保存", AnnouncementCategory::System);
    QTimer::singleShot(0, this, [this]() {
        if (isHumanBiddingTurn()) {
            showBiddingControls();
        } else if (m_handView && m_handModel && m_handModel->rowCount() > 0) {
            m_handView->setFocus(Qt::OtherFocusReason);
        } else {
            setFocus(Qt::OtherFocusReason);
        }
    });
}

void MainWindow::openSoundManagerDialog() {
    if (!m_sound) return;
    const auto phase = m_engine.state().phase();
    const bool fileChangesAllowed =
        phase != GamePhase::Bidding && phase != GamePhase::Playing;
    SoundManagerDialog dialog(m_settings, *m_sound, fileChangesAllowed, this);
    connect(&dialog, &SoundManagerDialog::settingsChanged, this, [this]() {
        saveSettings();
        updateBackgroundMusic();
    });
    connect(&dialog, &SoundManagerDialog::backgroundMusicRestoreRequested,
            this, &MainWindow::updateBackgroundMusic);
    connect(&dialog, &SoundManagerDialog::openSoundDocumentationRequested,
            this, [this]() {
                openHelpTextFile(QString::fromUtf8(kSoundGuideFileName));
            });
    dialog.exec();
    saveSettings();
    m_sound->setCategorySettings(m_settings.soundCategories);
    m_sound->setMusicEnabled(m_settings.backgroundMusicEnabled);
    updateBackgroundMusic();
}

void MainWindow::openPlayerNameDialog(PlayerId playerId) {
    const int index = static_cast<int>(playerId);
    if (index < 0 || index >= PLAYER_COUNT) return;

    const QString currentName = QString::fromStdWString(playerDisplayName(playerId));
    const QString seatName = QString::fromStdWString(
        playerIdDisplayName(static_cast<PlayerId>(index)));
    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8(u8"设置玩家名称"));
    dialog.setAccessibleName(QString::fromUtf8(u8"设置玩家名称"));
    dialog.setAccessibleDescription(
        QString::fromUtf8(u8"输入当前座位以后要播报的名称"));

    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(
        QString::fromUtf8(u8"请输入") + seatName + QString::fromUtf8(u8"的新名称"),
        &dialog);
    label->setAccessibleName(label->text());
    layout->addWidget(label);

    auto* edit = new QLineEdit(&dialog);
    edit->setObjectName(QStringLiteral("playerNameEdit"));
    edit->setText(currentName);
    edit->setAccessibleName(seatName + QString::fromUtf8(u8"名称"));
    edit->setAccessibleDescription(
        QString::fromUtf8(u8"设置后，读屏播报和界面都会使用这个名称"));
    label->setBuddy(edit);
    layout->addWidget(edit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8(u8"确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8(u8"取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    QTimer::singleShot(0, edit, [edit]() {
        edit->setFocus(Qt::OtherFocusReason);
        edit->selectAll();
    });

    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = edit->text().trimmed();
    if (name.isEmpty()) {
        announce(L"玩家名称不能为空", AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        return;
    }

    m_settings.playerNames[static_cast<size_t>(index)] = name;
    m_settings.normalize();
    applyPlayerDisplayNamesToState();
    saveSettings();
    refreshFromState();
    announce(L"玩家名称已保存：" + name.toStdWString(), AnnouncementCategory::System);
}

void MainWindow::resetPlayerDisplayNames() {
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        m_settings.playerNames[static_cast<size_t>(i)] =
            QString::fromStdWString(playerIdDisplayName(static_cast<PlayerId>(i)));
    }
    applyPlayerDisplayNamesToState();
    saveSettings();
    refreshFromState();
    announce(L"玩家名称已恢复默认", AnnouncementCategory::System);
}

void MainWindow::scheduleAiTurn(int minimumDelayMilliseconds) {
    if (!m_aiTimer) return;
    if (m_engine.state().phase() != GamePhase::Bidding &&
        m_engine.state().phase() != GamePhase::Playing) {
        m_aiTimer->stop();
        return;
    }
    if (m_engine.fullState().currentPlayer == PlayerId::Player1) {
        m_aiTimer->stop();
        return;
    }
    m_aiTimer->start(std::max(aiDelayMilliseconds(m_settings.aiDelay), minimumDelayMilliseconds));
}

void MainWindow::handleAutoPassTimeout() {
    if (m_engine.state().phase() != GamePhase::Playing ||
        m_engine.fullState().currentPlayer != PlayerId::Player1) {
        return;
    }

    GameCommand cmd;
    cmd.type = GameCommandType::Pass;
    cmd.playerId = PlayerId::Player1;
    cmd.allowPassAsLeader = true;
    auto result = m_engine.execute(cmd);

    if (!result.success) {
        announce(L"本轮操作时间到，请尽快行动", AnnouncementCategory::Turn,
                 AnnouncementPriority::High);
        playSound(SoundId::Invalid);
        return;
    }

    rememberLastAction(result);
    announce(playerDisplayName(PlayerId::Player1) + L"，超时，已自动过牌",
             AnnouncementCategory::Pass,
             AnnouncementPriority::High);
    playSoundsForResult(result);
    refreshFromState();
    scheduleAiTurn();
}

void MainWindow::traceKeyboardEvent(const QString& source, unsigned int key,
                                    unsigned int scanCode, bool ctrlDown,
                                    bool shiftDown, bool altDown, bool keyDown,
                                    bool autoRepeat) {
    if (!m_diagnosticTrace) return;

    const auto* lineEdit = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    const bool sensitive = lineEdit &&
        (lineEdit->echoMode() == QLineEdit::Password ||
         lineEdit->echoMode() == QLineEdit::PasswordEchoOnEdit);

    QString keyName;
    if (sensitive) {
        keyName = QStringLiteral("sensitive_input");
    } else if (key >= 'A' && key <= 'Z') {
        keyName = QChar(static_cast<char16_t>(key));
    } else if (key >= '0' && key <= '9') {
        keyName = QChar(static_cast<char16_t>(key));
    } else {
        switch (key) {
        case Qt::Key_Left: case VK_LEFT: keyName = QStringLiteral("Left"); break;
        case Qt::Key_Right: case VK_RIGHT: keyName = QStringLiteral("Right"); break;
        case Qt::Key_Up: case VK_UP: keyName = QStringLiteral("Up"); break;
        case Qt::Key_Down: case VK_DOWN: keyName = QStringLiteral("Down"); break;
        case Qt::Key_Return: case Qt::Key_Enter: case VK_RETURN:
            keyName = QStringLiteral("Enter"); break;
        case Qt::Key_Control: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            keyName = QStringLiteral("Control"); break;
        case Qt::Key_Shift: case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            keyName = QStringLiteral("Shift"); break;
        case Qt::Key_Alt: case VK_MENU: case VK_LMENU: case VK_RMENU:
            keyName = QStringLiteral("Alt"); break;
        case Qt::Key_F1: case VK_F1: keyName = QStringLiteral("F1"); break;
        case Qt::Key_F2: case VK_F2: keyName = QStringLiteral("F2"); break;
        case Qt::Key_F4: case VK_F4: keyName = QStringLiteral("F4"); break;
        case Qt::Key_F5: case VK_F5: keyName = QStringLiteral("F5"); break;
        case Qt::Key_F11: case VK_F11: keyName = QStringLiteral("F11"); break;
        case Qt::Key_F12: case VK_F12: keyName = QStringLiteral("F12"); break;
        case Qt::Key_Home: case VK_HOME: keyName = QStringLiteral("Home"); break;
        case Qt::Key_End: case VK_END: keyName = QStringLiteral("End"); break;
        case Qt::Key_Space: keyName = QStringLiteral("Space"); break;
        default: keyName = QStringLiteral("Key_%1").arg(key); break;
        }
    }

    QJsonObject event;
    event[QStringLiteral("type")] = QStringLiteral("keyboard");
    event[QStringLiteral("source")] = source;
    event[QStringLiteral("key_name")] = keyName;
    if (!sensitive) event[QStringLiteral("key_code")] = static_cast<int>(key);
    event[QStringLiteral("scan_code")] = static_cast<int>(scanCode);
    event[QStringLiteral("key_down")] = keyDown;
    event[QStringLiteral("auto_repeat")] = autoRepeat;
    event[QStringLiteral("ctrl")] = ctrlDown;
    event[QStringLiteral("shift")] = shiftDown;
    event[QStringLiteral("alt")] = altDown;
    event[QStringLiteral("sensitive_redacted")] = sensitive;
    if (QWidget* focus = QApplication::focusWidget()) {
        event[QStringLiteral("focus_class")] = QString::fromLatin1(focus->metaObject()->className());
        event[QStringLiteral("focus_object")] = focus->objectName();
    }
    event[QStringLiteral("game_id")] = static_cast<qint64>(m_engine.state().gameId());
    event[QStringLiteral("phase")] = static_cast<int>(m_engine.state().phase());
    event[QStringLiteral("current_player")] =
        static_cast<int>(m_engine.fullState().currentPlayer);
    m_diagnosticTrace->record(event);
}

QJsonObject MainWindow::handTraceState() const {
    QJsonObject state;
    if (!m_handModel) return state;

    state[QStringLiteral("hand_count")] = m_handModel->rowCount();
    state[QStringLiteral("selected_count")] = m_handModel->selectedCount();
    QJsonArray selected;
    for (const auto& card : m_handModel->selectedCards()) {
        QJsonObject item;
        item[QStringLiteral("card_id")] = static_cast<int>(card.id());
        item[QStringLiteral("rank")] =
            QString::fromStdWString(CardTextFormatter::formatRankSpeech(card.rank()));
        selected.append(item);
    }
    state[QStringLiteral("selected_cards")] = selected;

    if (m_handView && m_handView->currentIndex().isValid()) {
        const int row = m_handView->currentIndex().row();
        const Card card = m_handModel->cardAt(row);
        state[QStringLiteral("cursor_row")] = row;
        state[QStringLiteral("cursor_card_id")] = static_cast<int>(card.id());
        state[QStringLiteral("cursor_rank")] =
            QString::fromStdWString(CardTextFormatter::formatRankSpeech(card.rank()));
        state[QStringLiteral("cursor_rank_count")] = m_handModel->countOfRank(card.rank());
    }
    return state;
}

void MainWindow::traceHandAction(const QString& action, const QJsonObject& before,
                                 const QJsonObject& details) {
    if (!m_diagnosticTrace) return;
    QJsonObject event = details;
    event[QStringLiteral("type")] = QStringLiteral("hand_action");
    event[QStringLiteral("action")] = action;
    event[QStringLiteral("before")] = before;
    event[QStringLiteral("after")] = handTraceState();
    event[QStringLiteral("game_id")] = static_cast<qint64>(m_engine.state().gameId());
    event[QStringLiteral("phase")] = static_cast<int>(m_engine.state().phase());
    m_diagnosticTrace->record(event);
}

void MainWindow::requestApplicationExit() {
    m_forceExitRequested = true;
    if (m_diagnosticTrace) {
        QJsonObject event;
        event[QStringLiteral("type")] = QStringLiteral("application_exit");
        event[QStringLiteral("source")] = QStringLiteral("Alt+F4");
        m_diagnosticTrace->record(event);
        m_diagnosticTrace->flush();
    }
    QTimer::singleShot(0, qApp, &QApplication::closeAllWindows);
}

void MainWindow::moveHandCursorTo(int row) {
    if (m_handView && m_handModel && m_handView->selectionModel()) {
        auto index = m_handModel->index(row, 0);
        if (index.isValid()) {
            const QSignalBlocker selectionSignalBlocker(m_handView->selectionModel());
            m_handView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
            m_handView->scrollTo(index, QAbstractItemView::EnsureVisible);
            m_handView->viewport()->update();
            announceCurrentCard();
        }
    }
}

void MainWindow::checkTrainingInstallExitRequest() {
    const QString path = DataPaths::modelInstallExitRequestFile();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;
    QJsonParseError parseError;
    const auto request = QJsonDocument::fromJson(file.readAll(), &parseError).object();
    file.close();
    const QDateTime requestedAt = QDateTime::fromString(
        request.value(QStringLiteral("requestedAtUtc")).toString(), Qt::ISODate);
    const bool valid = parseError.error == QJsonParseError::NoError &&
        request.value(QStringLiteral("schemaVersion")).toInt() == 1 &&
        request.value(QStringLiteral("request")).toString() ==
            QStringLiteral("install_tier_models") &&
        requestedAt.isValid() && requestedAt.secsTo(QDateTime::currentDateTimeUtc()) >= 0 &&
        requestedAt.secsTo(QDateTime::currentDateTimeUtc()) <= 120;
    QFile::remove(path);
    if (!valid) return;

    const auto phase = m_engine.state().phase();
    if (phase == GamePhase::Bidding || phase == GamePhase::Playing ||
        phase == GamePhase::Paused) {
        GameCommand abandon;
        abandon.type = GameCommandType::AbandonGame;
        m_engine.execute(abandon);
    }
    if (m_aiTimer) m_aiTimer->stop();
    if (m_turnCountdownTimer) m_turnCountdownTimer->stop();
    if (m_sound) m_sound->stopAll();
    saveSettings();
    m_forceExitRequested = true;
    QTimer::singleShot(0, qApp, &QApplication::closeAllWindows);
}

void MainWindow::announceCurrentCard() {
    if (!m_handView || !m_handModel) return;
    const auto currentIndex = m_handView->currentIndex();
    if (!currentIndex.isValid()) return;

    const QString text = m_handModel->data(currentIndex, Qt::AccessibleTextRole).toString();
    if (!text.isEmpty()) {
        announce(text.toStdWString(), AnnouncementCategory::CardNavigation,
                 AnnouncementPriority::Low);
    }
}

void MainWindow::suppressHandAccessibilityUntilUserAction() {
    m_handAccessibilitySuppressedUntilUserAction = true;
    if (m_handModel) m_handModel->setAccessibilityTextSuppressed(true);
}

void MainWindow::resumeHandAccessibilityForUserAction() {
    if (!m_handAccessibilitySuppressedUntilUserAction) return;
    m_handAccessibilitySuppressedUntilUserAction = false;
    if (m_handModel) m_handModel->setAccessibilityTextSuppressed(false);
}

QString MainWindow::buildDiagnosticReport() const {
    QString report;
    QTextStream out(&report);
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString exePath = QCoreApplication::applicationFilePath();
    const auto snapshot = m_engine.state().publicSnapshot();
    const QWidget* focus = QApplication::focusWidget();

    out << "飞船AI斗地主单机版诊断报告\n";
    out << "报告格式版本: 6\n";
    out << "生成时间: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n";
    out << "应用版本: " << QCoreApplication::applicationVersion() << "\n";
    out << "Qt编译版本: " << QT_VERSION_STR << "\n";
    out << "Qt运行版本: " << qVersion() << "\n";
    out << "Windows: " << QSysInfo::prettyProductName() << "\n";
    out << "系统架构: " << QSysInfo::currentCpuArchitecture() << "\n";
    out << "构建架构: " << QSysInfo::buildCpuArchitecture() << "\n";
    out << "程序路径: " << redactedPath(exePath) << "\n";
    out << "程序SHA-256: " << fileSha256(exePath) << "\n";
    out << "无障碍接口: Qt/UIA 标准接口\n";
    out << "当前朗读路线: " << QString::fromStdWString(m_accessibility.routeName()) << "\n";
    out << "具体读屏软件: 未探测，由系统无障碍客户端决定\n";
    out << "最近朗读投递时间: " << m_lastSpeechDeliveryTime << "\n";
    out << "最近朗读投递通道: " << m_lastSpeechDeliveryChannel << "\n";
    out << "最近朗读投递结果: " << m_lastSpeechDeliveryResult << "\n";
    out << "读屏状态:\n" << QString::fromStdWString(m_accessibility.backendStatus()) << "\n";
    out << "Qt无障碍已激活: " << (QAccessible::isActive() ? "是" : "否") << "\n";

    out << "\n[关键组件]\n";
    const QStringList components = {
        QStringLiteral("FourPlayerDoudizhu.exe"), QStringLiteral("Qt6Core.dll"),
        QStringLiteral("Qt6Gui.dll"), QStringLiteral("Qt6Widgets.dll"),
        QStringLiteral("platforms/qwindows.dll")
    };
    for (const QString& relative : components) {
        const QString path = QDir(appDir).filePath(relative);
        const QFileInfo info(path);
        out << relative << ": exists=" << (info.exists() ? "是" : "否")
            << ", size=" << (info.exists() ? QString::number(info.size()) : QStringLiteral("0"));
        if (info.exists()) out << ", sha256=" << fileSha256(path);
        out << "\n";
    }
    out << "\n[焦点与手牌]\n";
    out << "活动窗口: " << (QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "无") << "\n";
    out << "焦点控件: " << (focus ? focus->metaObject()->className() : "无") << "\n";
    out << "焦点对象名: " << (focus ? focus->objectName() : QString()) << "\n";
    out << "焦点AccessibleName: " << (focus ? focus->accessibleName() : QString()) << "\n";
    out << "手牌控件有焦点: " << (m_handView && m_handView->hasFocus() ? "是" : "否") << "\n";
    out << "手牌状态: " << QString::fromUtf8(QJsonDocument(handTraceState()).toJson(QJsonDocument::Compact)) << "\n";
    if (m_handView && m_handModel && m_handView->currentIndex().isValid()) {
        out << "当前可访问文本: "
            << m_handModel->data(m_handView->currentIndex(), Qt::AccessibleTextRole).toString() << "\n";
    }

    out << "\n[牌局公开状态]\n";
    out << "game_id: " << snapshot.gameId << "\n";
    out << "phase: " << gamePhaseText(snapshot.phase) << "\n";
    out << "current_player: " << (static_cast<int>(snapshot.currentPlayer) + 1) << "\n";
    out << "base_score: " << snapshot.baseScore << "\n";
    out << "multiplier: " << snapshot.currentMultiplier << "\n";
    out << "bottom_revealed: " << (snapshot.bottomCardsRevealed ? "是" : "否") << "\n";
    out << "bottom_count: " << snapshot.bottomCards.size() << "\n";
    out << "bottom_auto_announced_this_round: "
        << (m_bottomCardsAnnouncedGameId == snapshot.gameId ? "是" : "否") << "\n";
    out << "last_bottom_cards_query_time: " << m_lastBottomCardsQueryTime << "\n";
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        const auto& player = snapshot.players[static_cast<size_t>(index)];
        out << "player_" << (index + 1) << "_remaining: " << player.remainingCards
            << ", role=" << static_cast<int>(player.role)
            << ", bid=" << player.bidScore << "\n";
    }
    const auto& humanCards = m_engine.fullState().players[0].hand.cards();
    out << "human_hand: " << QString::fromStdWString(CardTextFormatter::formatCards(humanCards)) << "\n";

    out << "\n[安全设置摘要]\n";
    out << "ai_difficulty: " << m_settings.aiDifficulty << "\n";
    out << "ai_delay: " << m_settings.aiDelay << "\n";
    out << "sort_mode: " << m_settings.sortMode << "\n";
    out << "auto_pass: " << (m_settings.autoPassEnabled ? "开" : "关") << "\n";
    out << "sound_enabled: " << (m_settings.soundEnabled ? "开" : "关") << "\n";
    out << "settings_exists: " << (QFileInfo::exists(DataPaths::settingsFile()) ? "是" : "否") << "\n";

    out << "\n[最近诊断时间线]\n";
    if (m_diagnosticTrace) {
        m_diagnosticTrace->flush();
        QFile trace(m_diagnosticTrace->traceFilePath());
        if (trace.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> lines = trace.readAll().split('\n');
            const int start = std::max(0, static_cast<int>(lines.size()) - 201);
            for (int index = start; index < lines.size(); ++index) {
                const QJsonDocument document = QJsonDocument::fromJson(lines[index]);
                if (!document.isObject()) continue;
                const QJsonObject safe = sanitizeTraceEvent(document.object());
                if (!safe.isEmpty()) out << QJsonDocument(safe).toJson(QJsonDocument::Compact) << "\n";
            }
        } else {
            out << "无法读取诊断日志\n";
        }
    } else {
        out << "诊断服务未启用\n";
    }
    if (report.size() > 1024 * 1024) report.truncate(1024 * 1024);
    return report;
}

void MainWindow::copyDiagnosticInformation() {
    const QString report = buildDiagnosticReport();
    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard || report.isEmpty()) {
        announce(L"复制诊断信息失败", AnnouncementCategory::Error, AnnouncementPriority::High);
        return;
    }
    clipboard->setText(report, QClipboard::Clipboard);
    const std::wstring message = L"诊断信息已复制，共" + std::to_wstring(report.size()) + L"个字符";
    announce(message, AnnouncementCategory::System, AnnouncementPriority::High);
}

void MainWindow::takeCurrentCard() {
    const QJsonObject traceBefore = handTraceState();
    auto currentIndex = m_handView->currentIndex();
    if (!currentIndex.isValid()) return;

    suppressHandAccessibilityUntilUserAction();
    int row = currentIndex.row();
    if (m_handModel->isSelected(row)) {
        const int nextRow = m_handModel->nextUnselectedRow(row);
        if (nextRow >= 0) row = nextRow;
    }
    if (!m_handModel->isSelected(row)) {
        m_handModel->selectSingle(row);
        playSound(SoundId::CardSelect);
    }

    const auto card = m_handModel->cardAt(row);
    const auto targetIndex = m_handModel->index(row, 0);
    {
        const QSignalBlocker blocker(m_handView->selectionModel());
        m_handView->selectionModel()->setCurrentIndex(
            targetIndex, QItemSelectionModel::NoUpdate);
    }
    m_handView->scrollTo(targetIndex, QAbstractItemView::EnsureVisible);
    m_handView->viewport()->update();
    QJsonObject details;
    details[QStringLiteral("card_id")] = static_cast<int>(card.id());
    details[QStringLiteral("target_row")] = row;
    details[QStringLiteral("rank")] =
        QString::fromStdWString(CardTextFormatter::formatRankSpeech(card.rank()));
    traceHandAction(QStringLiteral("take_single"), traceBefore, details);
}

void MainWindow::takeCurrentGroup() {
    const QJsonObject traceBefore = handTraceState();
    auto currentIndex = m_handView->currentIndex();
    if (!currentIndex.isValid()) return;

    int targetRow = m_handModel->groupStartRow(currentIndex.row());
    if (targetRow < 0) return;

    suppressHandAccessibilityUntilUserAction();
    auto result = m_handModel->selectGroup(targetRow);
    if (!result.valid) {
        return;
    }
    if (result.newlySelectedCount == 0) {
        const int nextRow = m_handModel->nextGroupStartRow(targetRow);
        if (nextRow != targetRow) {
            targetRow = nextRow;
            result = m_handModel->selectGroup(targetRow);
        }
    }

    const auto targetIndex = m_handModel->index(targetRow, 0);
    {
        const QSignalBlocker blocker(m_handView->selectionModel());
        m_handView->selectionModel()->setCurrentIndex(
            targetIndex, QItemSelectionModel::NoUpdate);
    }
    m_handView->scrollTo(targetIndex, QAbstractItemView::EnsureVisible);
    m_handView->viewport()->update();
    if (result.newlySelectedCount == 0) {
        QJsonObject details;
        details[QStringLiteral("rank")] =
            QString::fromStdWString(CardTextFormatter::formatRankSpeech(result.rank));
        details[QStringLiteral("group_count")] = result.groupCount;
        details[QStringLiteral("newly_selected_count")] = 0;
        traceHandAction(QStringLiteral("take_group"), traceBefore, details);
        return;
    }
    playSound(SoundId::CardSelect);
    QJsonObject details;
    details[QStringLiteral("rank")] =
        QString::fromStdWString(CardTextFormatter::formatRankSpeech(result.rank));
    details[QStringLiteral("group_count")] = result.groupCount;
    details[QStringLiteral("newly_selected_count")] = result.newlySelectedCount;
    traceHandAction(QStringLiteral("take_group"), traceBefore, details);
}

void MainWindow::putDownNextPickedCard() {
    const QJsonObject traceBefore = handTraceState();
    if (!m_handModel) return;

    std::optional<Card> card;
    const auto currentIndex = m_handView ? m_handView->currentIndex() : QModelIndex{};
    suppressHandAccessibilityUntilUserAction();
    if (currentIndex.isValid() && m_handModel->isSelected(currentIndex.row())) {
        card = m_handModel->cardAt(currentIndex.row());
        m_handModel->setSelected(currentIndex.row(), false);
    } else {
        card = m_handModel->deselectNextPickedCard();
    }
    if (!card.has_value()) {
        traceHandAction(QStringLiteral("put_down_single"), traceBefore,
                        QJsonObject{{QStringLiteral("success"), false}});
        return;
    }

    playSound(SoundId::CardDeselect);
    QJsonObject details;
    details[QStringLiteral("success")] = true;
    details[QStringLiteral("card_id")] = static_cast<int>(card->id());
    details[QStringLiteral("rank")] =
        QString::fromStdWString(CardTextFormatter::formatRankSpeech(card->rank()));
    traceHandAction(QStringLiteral("put_down_single"), traceBefore, details);
}

void MainWindow::putDownAllCards() {
    const QJsonObject traceBefore = handTraceState();
    if (!m_handModel) return;
    const int selectedCount = m_handModel->selectedCount();
    suppressHandAccessibilityUntilUserAction();
    m_handModel->clearSelection();
    if (selectedCount > 0) {
        playSound(SoundId::CardDeselect);
    }
    QJsonObject details;
    details[QStringLiteral("released_count")] = selectedCount;
    traceHandAction(QStringLiteral("put_down_all"), traceBefore, details);
}

void MainWindow::syncPickedCardsToView() {
    // Sync is automatic through model
}

void MainWindow::triggerBattleShortcut() {
    if (m_resultDialog) return;
    const auto menus = findChildren<QMenu*>();
    const bool hasOpenMenu = std::any_of(menus.cbegin(), menus.cend(),
        [](const QMenu* menu) { return menu && menu->isVisible(); });
    if (hasOpenMenu || isTopLevelMenuNavigationActive()) {
        dismissMenusForGameAction();
    }
    constexpr qint64 duplicateWindowMilliseconds = 150;
    if (m_battleShortcutTimer.isValid() &&
        m_battleShortcutTimer.elapsed() < duplicateWindowMilliseconds) {
        return;
    }
    m_battleShortcutTimer.restart();
    toggleBattleState();
}

bool MainWindow::triggerBottomCardsShortcut(const QString& source) {
    const auto phase = m_engine.state().phase();
    QWidget* focus = QApplication::focusWidget();
    const bool blockedByModal = QApplication::activeModalWidget() &&
        QApplication::activeModalWidget() != this;

    if (m_diagnosticTrace) {
        QJsonObject event{
            {QStringLiteral("type"), QStringLiteral("bottom_cards_shortcut_received")},
            {QStringLiteral("source"), source},
            {QStringLiteral("game_id"), static_cast<qint64>(m_engine.state().gameId())},
            {QStringLiteral("phase"), gamePhaseText(phase)},
            {QStringLiteral("result"), blockedByModal
                 ? QStringLiteral("blocked_modal")
                 : QStringLiteral("dispatched")}
        };
        if (focus) {
            event[QStringLiteral("focus_class")] =
                QString::fromLatin1(focus->metaObject()->className());
        }
        m_diagnosticTrace->record(event);
    }

    if (blockedByModal) return false;
    dismissMenusForGameAction();
    announceBottomCards();
    return true;
}

void MainWindow::toggleBattleState() {
    const auto phase = m_engine.state().phase();
    if (phase == GamePhase::NotStarted || phase == GamePhase::Finished) {
        startNewGame();
        return;
    }

    CommandResult result;
    if (phase == GamePhase::Playing || phase == GamePhase::Bidding) {
        GameCommand cmd;
        cmd.type = GameCommandType::Pause;
        result = m_engine.execute(cmd);
    } else if (phase == GamePhase::Paused) {
        GameCommand cmd;
        cmd.type = GameCommandType::Resume;
        result = m_engine.execute(cmd);
    } else {
        announce(L"当前阶段不能停战", AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        return;
    }

    if (!result.success) {
        announce(result.userMessage, AnnouncementCategory::Error,
                 AnnouncementPriority::High);
        return;
    }

    if (!result.events.empty()) {
        announce(formatEventForAnnouncement(result.events.front()), AnnouncementCategory::System);
    }
    refreshFromState();
}

void MainWindow::announcePlayerAtPosition(int position) {
    if (position < 1 || position > 4) return;

    const auto& state = m_engine.fullState();
    PlayerId pid = static_cast<PlayerId>(position - 1);
    const auto& player = state.players[static_cast<size_t>(pid)];

    std::wstring text = playerDisplayName(pid);
    if (player.isHuman || pid == PlayerId::Player1) text += L"，自己";
    text += L"，剩余" +
                        std::to_wstring(player.remainingCards()) + L"张牌";

    if (player.role == Role::Landlord) {
        text += L"，地主";
    } else if (player.role == Role::Farmer) {
        text += L"，农民";
    } else {
        text += L"，身份未定";
    }

    announce(text, AnnouncementCategory::System);
}

void MainWindow::announceBottomCards() {
    const auto snapshot = m_engine.publicSnapshot();
    if (!snapshot.bottomCardsRevealed || snapshot.bottomCards.size() != BOTTOM_CARDS) {
        announce(L"当前没有底牌", AnnouncementCategory::System);
        return;
    }

    std::wstring text = L"底牌：";
    text += CardTextFormatter::formatPublicCards(snapshot.bottomCards);
    m_lastBottomCardsQueryTime = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    if (m_diagnosticTrace) {
        m_diagnosticTrace->record(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("bottom_cards_queried")},
            {QStringLiteral("game_id"), static_cast<qint64>(snapshot.gameId)},
            {QStringLiteral("phase"), gamePhaseText(snapshot.phase)},
            {QStringLiteral("bottom_count"), static_cast<int>(snapshot.bottomCards.size())},
            {QStringLiteral("route"), QString::fromStdWString(m_accessibility.routeName())},
            {QStringLiteral("result"), QStringLiteral("delivered")}});
    }
    announce(text, AnnouncementCategory::System);
}

int MainWindow::announceNewDealBottomCards(const CommandResult& result,
                                           int initialDelayMilliseconds) {
    const bool containsDeal = std::any_of(result.events.begin(), result.events.end(),
        [](const GameEvent& event) { return event.type == GameEventType::CardsDealt; });
    const auto snapshot = m_engine.publicSnapshot();
    if (!containsDeal || snapshot.phase != GamePhase::Bidding ||
        !snapshot.bottomCardsRevealed || snapshot.bottomCards.size() != BOTTOM_CARDS ||
        m_bottomCardsAnnouncedGameId == snapshot.gameId) {
        return 0;
    }

    m_bottomCardsAnnouncedGameId = snapshot.gameId;
    std::wstring text = L"游戏开始，已经发牌。底牌：";
    text += CardTextFormatter::formatPublicCards(snapshot.bottomCards);
    if (snapshot.currentPlayer == PlayerId::Player1) {
        text += L"。现在轮到你叫分，可按0不叫，按1、2、3叫分";
    } else {
        text += L"。现在由" + playerDisplayName(snapshot.currentPlayer) + L"叫分";
    }
    if (m_diagnosticTrace) {
        const QJsonObject details{
            {QStringLiteral("game_id"), static_cast<qint64>(snapshot.gameId)},
            {QStringLiteral("phase"), gamePhaseText(snapshot.phase)},
            {QStringLiteral("bottom_count"), static_cast<int>(snapshot.bottomCards.size())},
            {QStringLiteral("route"), QString::fromStdWString(m_accessibility.routeName())},
            {QStringLiteral("result"), QStringLiteral("delivered")}};
        QJsonObject revealed = details;
        revealed[QStringLiteral("type")] = QStringLiteral("bottom_cards_revealed_for_bidding");
        m_diagnosticTrace->record(revealed);
        QJsonObject announced = details;
        announced[QStringLiteral("type")] = QStringLiteral("bottom_cards_announced");
        m_diagnosticTrace->record(announced);
    }
    const int initialDelay = std::max(0, initialDelayMilliseconds);
    auto deliver = [this, text]() {
        announce(text, AnnouncementCategory::System, AnnouncementPriority::High);
    };
    if (initialDelay > 0) {
        QTimer::singleShot(initialDelay, this, std::move(deliver));
    } else {
        deliver();
    }
    return initialDelay + readableAnnouncementDelayMilliseconds(text);
}

void MainWindow::announceScore() {
    const auto& state = m_engine.fullState();
    const int64_t multiplier = state.roundResult.valid
        ? state.roundResult.finalMultiplier
        : state.currentMultiplier;
    std::wstring text = L"基础分：" + std::to_wstring(state.baseScore) +
                        L"，当前倍数：" + std::to_wstring(multiplier);
    announce(text, AnnouncementCategory::System);
}

void MainWindow::announceCurrentTurn() {
    const auto& state = m_engine.fullState();
    const auto phase = m_engine.state().phase();
    if (phase != GamePhase::Bidding && phase != GamePhase::Playing) return;
    announce(playerDisplayName(state.currentPlayer), AnnouncementCategory::Turn);
}

void MainWindow::announceLastAction() {
    const auto& state = m_engine.fullState();
    if (!state.lastPlayedCards.empty()) {
        const auto pattern = PatternAnalyzer::analyze(state.lastPlayedCards);
        std::wstring text = playerDisplayName(state.lastPlayedBy) + L"，";
        text += CardTextFormatter::formatPlayedCards(pattern, state.lastPlayedCards);
        announce(text, AnnouncementCategory::System);
    } else if (!m_lastPlayedCardsText.empty()) {
        announce(m_lastPlayedCardsText, AnnouncementCategory::System);
    } else {
        announce(L"空", AnnouncementCategory::System);
    }
}

void MainWindow::rememberLastAction(const CommandResult& result) {
    for (const auto& event : result.events) {
        if (event.type == GameEventType::CardsPlayed && !event.cards.empty()) {
            m_lastPlayedCardsText = playerDisplayName(event.playerId) + L"，" +
                CardTextFormatter::formatPlayedCards(event.pattern, event.cards);
        }
    }

    if (!result.events.empty()) {
        m_lastActionText = formatEventForAnnouncement(result.events[0]);
    }
}

void MainWindow::handleFinishedResult(const CommandResult& result, int announcementDelayMilliseconds) {
    const auto finishEvent = std::find_if(result.events.begin(), result.events.end(),
        [](const GameEvent& event) { return event.type == GameEventType::GameFinished; });
    if (finishEvent == result.events.end()) return;

    const auto& round = m_engine.fullState().roundResult;
    if (announcementDelayMilliseconds > 0) {
        QTimer::singleShot(announcementDelayMilliseconds, this, [this]() {
            if (m_engine.state().phase() == GamePhase::Finished) {
                showRoundResult();
            }
        });
    } else {
        showRoundResult();
    }

    if (m_statisticsRepo) {
        DataPaths::ensureDirectories();
        m_statisticsRepo->recordRound(round, m_engine.fullState().players[0].role);
        m_statisticsRepo->save(DataPaths::statisticsFile());
    }
}

void MainWindow::showRoundResult() {
    if (m_resultDialog || !m_engine.fullState().roundResult.valid) return;
    const auto& state = m_engine.fullState();
    const auto& round = state.roundResult;

    PlayerId landlord = PlayerId::Player1;
    for (const auto& player : state.players) {
        if (player.role == Role::Landlord) {
            landlord = player.id;
            break;
        }
    }

    QStringList items;
    items << QString::fromUtf8(round.landlordWon ? u8"胜方：地主" : u8"胜方：农民");
    items << QString::fromUtf8(u8"地主：") + QString::fromStdWString(playerDisplayName(landlord));
    items << QString::fromUtf8(u8"基础分：") + QString::number(state.baseScore);
    items << QString::fromUtf8(u8"最终倍数：") + QString::number(round.finalMultiplier);
    items << QString::fromUtf8(u8"特殊结算：") +
        QString::fromUtf8(round.spring ? u8"春天" : round.antiSpring ? u8"反春" : u8"无");
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        const auto playerId = static_cast<PlayerId>(index);
        items << QString::fromStdWString(playerDisplayName(playerId)) +
            QString::fromUtf8(u8"本局得分：") +
            QString::number(round.scoreChanges[static_cast<size_t>(index)]);
    }

    m_resultDialog = new ResultDialog(items, this);
    connect(m_resultDialog, &QDialog::finished, this, [this](int) {
        m_resultDialog = nullptr;
        returnToMainScreen();
    });
    m_resultDialog->show();
    m_resultDialog->raise();
    m_resultDialog->activateWindow();
}

void MainWindow::returnToMainScreen() {
    if (m_aiTimer) m_aiTimer->stop();
    if (m_turnCountdownTimer) m_turnCountdownTimer->stop();
    if (m_countdownLabel) m_countdownLabel->setVisible(false);
    hideBiddingControls();
    m_lastActionText.clear();
    m_lastPlayedCardsText.clear();
    m_wasHumanTurn = false;
    m_engine.state() = GameState{};
    applyPlayerDisplayNamesToState();
    if (m_handModel) m_handModel->clearSelection();
    QFile::remove(DataPaths::autoSaveFile());
    QFile::remove(DataPaths::autoSaveBackupFile());
    refreshFromState();
}

void MainWindow::updateTurnCountdown(bool humanTurn) {
    if (humanTurn && m_settings.autoPassEnabled) {
        m_turnSecondsRemaining = m_settings.autoPassSeconds;
        m_turnCountdownTimer->start();
        m_countdownLabel->setVisible(true);
        m_countdownLabel->setText(QString::fromStdWString(L"本轮操作剩余")
            + QString::number(m_turnSecondsRemaining) + QString::fromStdWString(L"秒"));
    } else {
        m_turnCountdownTimer->stop();
        m_countdownLabel->setVisible(false);
    }
}

int MainWindow::playSoundsForResult(const CommandResult& result, bool appendYourTurn) {
    bool playedDeal = false;
    bool playedAction = false;
    const bool hasCardPlay = std::any_of(result.events.begin(), result.events.end(),
        [](const GameEvent& event) { return event.type == GameEventType::CardsPlayed; });
    QVector<SoundRequest> requests;
    auto appendSound = [&requests](const QString& path, SoundCategory category) {
        requests.push_back({path, category});
    };

    for (const auto& event : result.events) {
        switch (event.type) {
        case GameEventType::GameStarted:
            break;
        case GameEventType::CardsDealt:
            if (!playedDeal) {
                appendSound(SoundService::soundFileName(SoundId::DealCards),
                            SoundCategory::StartupDeal);
                playedDeal = true;
            }
            break;
        case GameEventType::PlayerBid:
            if (event.bidValue >= 1 && event.bidValue <= 3) {
                appendSound(cardFourVoiceFile(event.playerId,
                            "jiao" + QString::number(event.bidValue),
                            m_settings.humanVoice == PlayerVoice::Female),
                            SoundCategory::BiddingLandlord);
            } else {
                appendSound(cardFourVoiceFile(event.playerId, "bujiao",
                            m_settings.humanVoice == PlayerVoice::Female),
                            SoundCategory::BiddingLandlord);
            }
            break;
        case GameEventType::LandlordDetermined:
            appendSound(SoundService::soundFileName(SoundId::Landlord),
                        SoundCategory::BiddingLandlord);
            break;
        case GameEventType::CardsPlayed:
            if (!playedAction) {
                const auto plan = buildCardPatternSoundPlan(
                    event, m_settings.humanVoice == PlayerVoice::Female);
                for (const auto& file : plan.voiceFiles) {
                    appendSound(QString::fromStdString(file), SoundCategory::CardPattern);
                }
                if (!plan.effectFile.empty()) {
                    appendSound(QString::fromStdString(plan.effectFile),
                                SoundCategory::CardPattern);
                }
                playedAction = true;
            }
            break;
        case GameEventType::PlayerPassed:
            appendSound(cardFourVoiceFile(event.playerId,
                "pass" + QString::number((event.sequence % 4) + 1),
                m_settings.humanVoice == PlayerVoice::Female), SoundCategory::Pass);
            break;
        case GameEventType::MultiplierChanged:
            if (!hasCardPlay) {
                appendSound(SoundService::soundFileName(SoundId::Multiplier),
                            SoundCategory::Multiplier);
            }
            break;
        case GameEventType::PlayerLowCards:
            appendSound(cardFourVoiceFile(event.playerId,
                event.remainingCards <= 1 ? "baojing1" : "baojing2",
                m_settings.humanVoice == PlayerVoice::Female), SoundCategory::LowCards);
            break;
        case GameEventType::InvalidAction:
            appendSound(SoundService::soundFileName(SoundId::Invalid),
                        SoundCategory::InvalidAction);
            break;
        case GameEventType::GameFinished: {
            const auto& state = m_engine.fullState();
            const auto winnerIndex = static_cast<size_t>(event.playerId);
            const Role winnerRole = state.players[winnerIndex].role;
            const Role humanRole = state.players[0].role;
            appendSound(SoundService::soundFileName(
                winnerRole == humanRole ? SoundId::Win : SoundId::Lose),
                SoundCategory::GameResult);
            break;
        }
        default:
            break;
        }
    }
    if (appendYourTurn) {
        appendSound(SoundService::soundFileName(SoundId::YourTurn),
                    SoundCategory::YourTurn);
    }
    return m_sound ? m_sound->playFiles(requests) : 0;
}

void MainWindow::playSound(SoundId id) {
    if (m_sound) {
        m_sound->play(id);
    }
}

void MainWindow::playSoundFile(const QString& relativePath, SoundCategory category) {
    if (m_sound) {
        m_sound->playFile(relativePath, category);
    }
}

void MainWindow::updateBackgroundMusic() {
    if (!m_sound) return;
    if (!m_settings.backgroundMusicEnabled) {
        m_sound->stopMusic();
        return;
    }

    BackgroundMusicMode mode = m_settings.backgroundMusicMode;
    if (mode == BackgroundMusicMode::Automatic) {
        switch (m_engine.state().phase()) {
        case GamePhase::Bidding:
            mode = BackgroundMusicMode::Normal;
            break;
        case GamePhase::Playing:
            mode = BackgroundMusicMode::Intense;
            break;
        default:
            mode = BackgroundMusicMode::Background;
            break;
        }
    }

    QString file;
    switch (mode) {
    case BackgroundMusicMode::Normal:
        file = QStringLiteral("music/normal.wav");
        break;
    case BackgroundMusicMode::Intense:
        file = QStringLiteral("music/intense.wav");
        break;
    case BackgroundMusicMode::Background:
    case BackgroundMusicMode::Automatic:
        file = QStringLiteral("music/background.wav");
        break;
    }
    m_sound->playMusic(file);
}

void MainWindow::announce(const std::wstring& text, AnnouncementCategory category,
                          AnnouncementPriority priority, bool updateStatus) {
    m_lastSpeechDeliveryTime = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    m_lastSpeechDeliveryChannel = QStringLiteral("qt_focus_event");
    m_lastSpeechDeliveryResult = QStringLiteral("传统Qt/UIA焦点事件未提交");
    if (m_statusLabel && !text.empty()) {
        const QString qText = QString::fromStdWString(text);
        if (updateStatus) {
            m_statusLabel->setText(qText);
        }
    }

    Announcement ann;
    ann.text = text;
    ann.category = category;
    ann.priority = priority;
    const bool submitted = m_accessibility.announce(ann, m_statusLabel);
    m_lastSpeechDeliveryResult = submitted
        ? QStringLiteral("传统Qt/UIA焦点事件已提交，实际朗读由读屏软件决定")
        : QStringLiteral("传统Qt/UIA焦点事件已去重或目标不可用");
    if (m_diagnosticTrace) {
        m_diagnosticTrace->record(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("speech_delivery")},
            {QStringLiteral("game_id"), static_cast<qint64>(m_engine.state().gameId())},
            {QStringLiteral("phase"), gamePhaseText(m_engine.state().phase())},
            {QStringLiteral("route"), QString::fromStdWString(m_accessibility.routeName())},
            {QStringLiteral("backend"), QString::fromStdWString(m_accessibility.backendName())},
            {QStringLiteral("channel"), m_lastSpeechDeliveryChannel},
            {QStringLiteral("result"), m_lastSpeechDeliveryResult}});
    }
}

} // namespace fpdz
