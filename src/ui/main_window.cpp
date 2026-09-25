#include "main_window.h"
#include "models/hand_list_model.h"
#include "models/player_status_model.h"
#include "widgets/game_status_widget.h"
#include "widgets/card_table_widget.h"
#include "dialogs/settings_dialog.h"
#include "dialogs/shortcut_dialog.h"
#include "dialogs/sound_manager_dialog.h"
#include "dialogs/result_dialog.h"
#include "dialogs/ai_battle_settings_dialog.h"
#include "dialogs/credential_manager_dialog.h"
#include "../core/engine/game_engine.h"
#include "../core/engine/turn_manager.h"
#include "../accessibility/accessibility_service.h"
#include "../ai/ai_player.h"
#include "../ai/simple_ai.h"
#include "../ai/standard_ai.h"
#include "../ai/local_ai_decision_adapter.h"
#include "../app/ai_service_client.h"
#include "../persistence/data_paths.h"
#include "../ai/hint_service.h"
#include "../persistence/settings_repository.h"
#include "../persistence/statistics_repository.h"
#include "../persistence/ai_battle_statistics_repository.h"
#include "../persistence/data_paths.h"
#include "../core/text/card_text_formatter.h"
#include "../core/text/game_text_formatter.h"
#include "../core/audio/card_pattern_sound_plan.h"
#include "../core/rules/pattern_analyzer.h"
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
#include <QAbstractButton>
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
#include <QUuid>
#include <algorithm>
#include <array>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fpdz {

namespace {

constexpr int kFirstRunGuideRevision = 2;
const auto kStartupUpdateGuideFileName = u8"飞船斗地主更新说明.txt";
const auto kDetailedGuideFileName = u8"飞船斗地主详细使用说明.txt";
const auto kRulesFileName = u8"飞船斗地主玩法说明.txt";
const auto kShortcutsFileName = u8"飞船斗地主快捷键说明.txt";
const auto kSoundGuideFileName = u8"飞船斗地主音效分类与替换说明.txt";
const auto kChangelogFileName = u8"飞船斗地主更新日志.txt";

std::wstring formatCardSelectionGroup(Rank rank, int count) {
    // Picking up and putting down a rank group must use exactly the same
    // wording as left/right hand navigation (for example, "3张3").
    return CardTextFormatter::formatSameRankSpeech(rank, count);
}

std::wstring formatCardSelection(const std::vector<Card>& cards) {
    std::vector<std::pair<Rank, int>> groups;
    for (const auto& card : cards) {
        if (!card.isValid()) continue;
        auto group = std::find_if(groups.begin(), groups.end(), [&card](const auto& entry) {
            return entry.first == card.rank();
        });
        if (group == groups.end()) {
            groups.push_back({card.rank(), 1});
        } else {
            ++group->second;
        }
    }

    std::wstring text;
    for (const auto& [rank, count] : groups) {
        if (!text.empty()) text += L"、";
        text += formatCardSelectionGroup(rank, count);
    }
    return text;
}

// 只读判断：当前选中的全部牌是否恰好构成可以用首尾表达的牌型。
std::optional<std::wstring> conciseSelectedSequenceText(
        const std::vector<Card>& cards, int activePlayerCount) {
    if (cards.empty()) return std::nullopt;
    const CardPattern pattern = PatternAnalyzer::analyze(cards, activePlayerCount);
    switch (pattern.type) {
    case CardPatternType::Straight:
    case CardPatternType::ConsecutivePairs:
    case CardPatternType::Airplane:
        break;
    default:
        return std::nullopt;
    }
    if (!pattern.isValid()) return std::nullopt;
    return CardTextFormatter::formatPlayedCards(pattern, cards);
}

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
        QStringLiteral("ai_decision"),
        QStringLiteral("bottom_cards_revealed"),
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
        QStringLiteral("result"), QStringLiteral("bottom_count"),
        QStringLiteral("team_rule_exception"), QStringLiteral("last_played_by"),
        QStringLiteral("last_card_count"), QStringLiteral("landlord_remaining")
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
ShortcutSettings g_activeShortcuts;

int qtKeyFromVirtualKey(DWORD virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return Qt::Key_A + static_cast<int>(virtualKey - 'A');
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return Qt::Key_0 + static_cast<int>(virtualKey - '0');
    }
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
        return Qt::Key_0 + static_cast<int>(virtualKey - VK_NUMPAD0);
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return Qt::Key_F1 + static_cast<int>(virtualKey - VK_F1);
    }
    switch (virtualKey) {
    case VK_LEFT: return Qt::Key_Left;
    case VK_RIGHT: return Qt::Key_Right;
    case VK_UP: return Qt::Key_Up;
    case VK_DOWN: return Qt::Key_Down;
    case VK_HOME: return Qt::Key_Home;
    case VK_END: return Qt::Key_End;
    case VK_PRIOR: return Qt::Key_PageUp;
    case VK_NEXT: return Qt::Key_PageDown;
    case VK_INSERT: return Qt::Key_Insert;
    case VK_DELETE: return Qt::Key_Delete;
    case VK_BACK: return Qt::Key_Backspace;
    case VK_RETURN: return Qt::Key_Return;
    case VK_TAB: return Qt::Key_Tab;
    case VK_SPACE: return Qt::Key_Space;
    case VK_ESCAPE: return Qt::Key_Escape;
    case VK_CAPITAL: return Qt::Key_CapsLock;
    case VK_NUMLOCK: return Qt::Key_NumLock;
    case VK_SCROLL: return Qt::Key_ScrollLock;
    case VK_PAUSE: return Qt::Key_Pause;
    case VK_SNAPSHOT: return Qt::Key_Print;
    case VK_SUBTRACT: case VK_OEM_MINUS: return Qt::Key_Minus;
    case VK_ADD: case VK_OEM_PLUS: return Qt::Key_Plus;
    case VK_MULTIPLY: return Qt::Key_Asterisk;
    case VK_DIVIDE: case VK_OEM_2: return Qt::Key_Slash;
    case VK_OEM_COMMA: return Qt::Key_Comma;
    case VK_OEM_PERIOD: return Qt::Key_Period;
    case VK_OEM_1: return Qt::Key_Semicolon;
    case VK_OEM_3: return Qt::Key_QuoteLeft;
    case VK_OEM_4: return Qt::Key_BracketLeft;
    case VK_OEM_5: return Qt::Key_Backslash;
    case VK_OEM_6: return Qt::Key_BracketRight;
    case VK_OEM_7: return Qt::Key_Apostrophe;
    default: return 0;
    }
}

ShortcutBinding shortcutFromVirtualKey(DWORD virtualKey, bool ctrlDown,
                                        bool shiftDown, bool altDown) {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if (ctrlDown) modifiers |= Qt::ControlModifier;
    if (shiftDown) modifiers |= Qt::ShiftModifier;
    if (altDown) modifiers |= Qt::AltModifier;
    if ((virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) ||
        virtualKey == VK_ADD || virtualKey == VK_SUBTRACT ||
        virtualKey == VK_MULTIPLY || virtualKey == VK_DIVIDE) {
        modifiers |= Qt::KeypadModifier;
    }
    int key = qtKeyFromVirtualKey(virtualKey);
    if (virtualKey == VK_OEM_PLUS && !shiftDown) key = Qt::Key_Equal;
    return ShortcutSettings::fromKeyEvent(key, modifiers);
}

UINT virtualKeyFromShortcut(const ShortcutBinding& binding) {
    const int key = binding.key;
    const bool keypad = binding.modifiers.testFlag(Qt::KeypadModifier);
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return 'A' + key - Qt::Key_A;
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return keypad ? VK_NUMPAD0 + key - Qt::Key_0 : '0' + key - Qt::Key_0;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return VK_F1 + key - Qt::Key_F1;
    switch (key) {
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Return: return VK_RETURN;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_CapsLock: return VK_CAPITAL;
    case Qt::Key_NumLock: return VK_NUMLOCK;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Minus: return keypad ? VK_SUBTRACT : VK_OEM_MINUS;
    case Qt::Key_Plus: return keypad ? VK_ADD : VK_OEM_PLUS;
    case Qt::Key_Equal: return VK_OEM_PLUS;
    case Qt::Key_Asterisk: return VK_MULTIPLY;
    case Qt::Key_Slash: return keypad ? VK_DIVIDE : VK_OEM_2;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_QuoteLeft: return VK_OEM_3;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_Backslash: return VK_OEM_5;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    default: return 0;
    }
}

UINT nativeModifiersFromShortcut(const ShortcutBinding& binding) {
    UINT modifiers = MOD_NOREPEAT;
    if (binding.modifiers.testFlag(Qt::ControlModifier)) modifiers |= MOD_CONTROL;
    if (binding.modifiers.testFlag(Qt::ShiftModifier)) modifiers |= MOD_SHIFT;
    if (binding.modifiers.testFlag(Qt::AltModifier)) modifiers |= MOD_ALT;
    return modifiers;
}

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

bool isKeyboardHookCandidate(DWORD virtualKey, bool ctrlDown,
                             bool shiftDown, bool altDown) {
    return g_activeShortcuts.actionFor(
        shortcutFromVirtualKey(virtualKey, ctrlDown, shiftDown, altDown)).has_value();
}

bool isSingleFireKey(DWORD virtualKey, bool ctrlDown = false,
                     bool shiftDown = false, bool altDown = false) {
    const auto action = g_activeShortcuts.actionFor(
        shortcutFromVirtualKey(virtualKey, ctrlDown, shiftDown, altDown));
    if (!action) return false;
    return *action != ShortcutAction::PreviousRankGroup &&
           *action != ShortcutAction::NextRankGroup &&
           *action != ShortcutAction::PreviousWholeRankGroup &&
           *action != ShortcutAction::NextWholeRankGroup;
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

    const bool ctrlDown = isControlDown();
    const bool shiftDown = isShiftDown();
    const bool altDown = isAltDown() || wParam == WM_SYSKEYDOWN;
    const auto configuredAction = g_activeShortcuts.actionFor(
        shortcutFromVirtualKey(key->vkCode, ctrlDown, shiftDown, altDown));
    const bool menuCompatibleAction = configuredAction &&
        (*configuredAction == ShortcutAction::Battle ||
         *configuredAction == ShortcutAction::BottomCards ||
         *configuredAction == ShortcutAction::OpenSettings ||
         *configuredAction == ShortcutAction::CurrentTurn);
    if ((!menuCompatibleAction && shouldYieldKeyboardHandlingToFocusedWidget()) ||
        !isKeyboardHookForeground() ||
        (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN)) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (!isKeyboardHookCandidate(key->vkCode, ctrlDown, shiftDown, altDown)) {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    if (isSingleFireKey(key->vkCode, ctrlDown, shiftDown, altDown) &&
        key->vkCode < g_singleFireKeyDown.size()) {
        if (g_singleFireKeyDown[key->vkCode]) {
            return 1;
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
                       DiagnosticTraceService* diagnosticTrace, GameMode gameMode,
                       QWidget* parent)
    : QMainWindow(parent), m_engine(engine), m_accessibility(accessibility),
      m_diagnosticTrace(diagnosticTrace), m_gameMode(gameMode) {
#ifdef Q_OS_WIN
    g_openMenuCount = 0;
#endif
    setWindowTitle(QString::fromUtf8(u8"飞船斗地主"));
    setMinimumSize(800, 600);
    m_handModel = std::make_unique<HandListModel>();
    m_playerModel = std::make_unique<PlayerStatusModel>();
    
    // AI timer
    m_settingsRepo = std::make_unique<SettingsRepository>();
    m_statisticsRepo = std::make_unique<StatisticsRepository>();
    m_statisticsRepo->load(DataPaths::statisticsFile());
    loadSettings();
    if (m_gameMode == GameMode::AiBattle) {
        m_aiBattleSettingsRepo = std::make_unique<SettingsRepository>();
        m_aiBattleStatisticsRepo = std::make_unique<AiBattleStatisticsRepository>();
        m_aiBattleStatisticsRepo->load(DataPaths::aiBattleStatisticsFile());
        loadAiBattleSettings();
        m_aiService = std::make_unique<AiServiceClient>(this);
        connect(m_aiService.get(), &AiServiceClient::messageReceived,
                this, &MainWindow::handleAiServiceMessage);
        connect(m_aiService.get(), &AiServiceClient::serviceFailed,
                this, [this](const QString& message) {
                    if (m_pendingCloudRequest) {
                        const auto request = *m_pendingCloudRequest;
                        const int latency = m_cloudWaitElapsed.isValid()
                            ? static_cast<int>(m_cloudWaitElapsed.elapsed()) : 0;
                        recordCloudRequestOutcome(request, false,
                                                  QStringLiteral("client_failure"),
                                                  QStringLiteral("ai_service_failed"),
                                                  latency);
                        showCloudFault(message);
                    }
                });
        m_aiService->start();
    }
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
    if (m_gameMode == GameMode::AiBattle) {
        QTimer::singleShot(0, this, &MainWindow::ensureAiBattleFirstRunPrompt);
    }
}

MainWindow::~MainWindow() {
    stopCloudWait();
    if (m_aiService) m_aiService->stop();
    unregisterSystemHotkeys();
    uninstallKeyboardHook();
#ifdef Q_OS_WIN
    QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
    qApp->removeEventFilter(this);
}

std::unique_ptr<MainWindow> createMainWindow(GameEngine& engine,
                                             AccessibilityService& accessibility,
                                             DiagnosticTraceService* diagnosticTrace,
                                             GameMode gameMode) {
    return std::make_unique<MainWindow>(engine, accessibility, diagnosticTrace,
                                        gameMode);
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
    m_battleAction = newAction;
    newAction->setShortcut(ShortcutSettings::keySequence(
        m_settings.shortcuts.binding(ShortcutAction::Battle)));
    newAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(newAction, &QAction::triggered, this, &MainWindow::triggerBattleShortcut);

    auto* pauseAction = gameMenu->addAction(QString::fromStdWString(L"暂停/恢复(&P)"));
    m_pauseAction = pauseAction;
    pauseAction->setShortcut(ShortcutSettings::keySequence(
        m_settings.shortcuts.binding(ShortcutAction::PauseResume)));
    connect(pauseAction, &QAction::triggered, this, [this]() {
        triggerShortcutAction(ShortcutAction::PauseResume,
                              QStringLiteral("menu_action"));
    });

    gameMenu->addSeparator();

    if (m_gameMode == GameMode::AiBattle) {
        m_retryCloudAction = gameMenu->addAction(
            QString::fromUtf8(u8"重试当前云模型回合(&R)"));
        m_retryCloudAction->setObjectName(QStringLiteral("retryCloudTurnAction"));
        m_retryCloudAction->setEnabled(false);
        connect(m_retryCloudAction, &QAction::triggered,
                this, &MainWindow::retryCloudTurn);
        auto* copyGamesAction = gameMenu->addAction(
            QString::fromUtf8(u8"复制最近50局完整对战记录(&J)"));
        copyGamesAction->setObjectName(QStringLiteral("copyAiBattleGamesAction"));
        connect(copyGamesAction, &QAction::triggered,
                this, &MainWindow::copyAiBattleGames);
    }

    auto* returnModeAction = gameMenu->addAction(
        QString::fromUtf8(u8"返回模式选择(&M)"));
    returnModeAction->setObjectName(QStringLiteral("returnToModeSelectionAction"));
    connect(returnModeAction, &QAction::triggered,
            this, &MainWindow::requestReturnToModeSelection);

    auto* quitAction = gameMenu->addAction(QString::fromStdWString(L"退出(&Q)"));
    m_quitAction = quitAction;
    quitAction->setShortcut(ShortcutSettings::keySequence(
        m_settings.shortcuts.binding(ShortcutAction::ExitApplication)));
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
    addPlayerNameAction(PlayerId::Player1, u8"设置1号玩家名称(&1)");
    addPlayerNameAction(PlayerId::Player2, u8"设置2号玩家名称(&2)");
    addPlayerNameAction(PlayerId::Player3, u8"设置3号玩家名称(&3)");
    addPlayerNameAction(PlayerId::Player4, u8"设置4号玩家名称(&4)");
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

    auto* settingsAction = settingsMenu->addAction(
        m_gameMode == GameMode::AiBattle ? QString::fromUtf8(u8"AI对战设置(&O)")
                                         : QString::fromStdWString(L"设置选项(&O)"));
    m_settingsAction = settingsAction;
    settingsAction->setShortcut(ShortcutSettings::keySequence(
        m_settings.shortcuts.binding(ShortcutAction::OpenSettings)));
    settingsAction->setShortcutContext(Qt::ApplicationShortcut);
    if (m_gameMode == GameMode::AiBattle) {
        connect(settingsAction, &QAction::triggered,
                this, &MainWindow::openAiBattleSettingsDialog);
    } else {
        connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    }

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
            QString::fromUtf8(u8"飞船斗地主 V") +
                QCoreApplication::applicationVersion() +
                QString::fromUtf8(u8"\n无障碍 Windows 单机游戏"));
    });

    helpMenu->addSeparator();
    auto* donateAction = helpMenu->addAction(QString::fromUtf8(u8"喜欢作者(&L)"));
    donateAction->setObjectName(QStringLiteral("donateAction"));
    connect(donateAction, &QAction::triggered, this, &MainWindow::showDonateDialog);

    auto* shortcutSettingsAction = menuBar()->addAction(
        QString::fromUtf8(u8"快捷键设置"));
    shortcutSettingsAction->setObjectName(QStringLiteral("shortcutSettingsMenuAction"));
    connect(shortcutSettingsAction, &QAction::triggered,
            this, &MainWindow::openShortcutDialog);
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

    m_cardTable = new CardTableWidget(central);
    mainLayout->addWidget(m_cardTable, 1);

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
    connect(m_handModel.get(), &QAbstractItemModel::dataChanged, this,
            [this]() { refreshVisualCardTable(); });
    connect(m_handModel.get(), &QAbstractItemModel::modelReset, this,
            [this]() { refreshVisualCardTable(); });

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
    applyShortcutBindings();
}

void MainWindow::applyShortcutBindings() {
    if (m_battleAction) {
        m_battleAction->setShortcut(ShortcutSettings::keySequence(
            m_settings.shortcuts.binding(ShortcutAction::Battle)));
    }
    if (m_pauseAction) {
        m_pauseAction->setShortcut(ShortcutSettings::keySequence(
            m_settings.shortcuts.binding(ShortcutAction::PauseResume)));
    }
    if (m_quitAction) {
        m_quitAction->setShortcut(ShortcutSettings::keySequence(
            m_settings.shortcuts.binding(ShortcutAction::ExitApplication)));
    }
    if (m_settingsAction) {
        m_settingsAction->setShortcut(ShortcutSettings::keySequence(
            m_settings.shortcuts.binding(ShortcutAction::OpenSettings)));
    }

    for (auto* shortcut : std::as_const(m_applicationShortcuts)) {
        delete shortcut;
    }
    m_applicationShortcuts.clear();
    const std::array<ShortcutAction, 4> compatibilityActions = {
        ShortcutAction::BottomCards, ShortcutAction::CurrentTurn,
        ShortcutAction::LastAction, ShortcutAction::Score};
    for (ShortcutAction action : compatibilityActions) {
        auto* shortcut = new QShortcut(
            ShortcutSettings::keySequence(m_settings.shortcuts.binding(action)), this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action]() {
            triggerShortcutAction(action, QStringLiteral("qt_shortcut"));
        });
        m_applicationShortcuts.append(shortcut);
    }
#ifdef Q_OS_WIN
    g_activeShortcuts = m_settings.shortcuts;
#endif
    registerSystemHotkeys();
}

void MainWindow::startNewGame() {
    if (m_gameMode == GameMode::AiBattle) {
        if (!validateAiBattleStart() || !confirmAiBattlePrivacy()) return;
        m_settings.playerCount = m_aiBattleSettings.playerCount;
    }
    resumeHandAccessibilityForUserAction();
    GameCommand cmd;
    cmd.type = GameCommandType::StartGame;
    cmd.playerCount = m_settings.playerCount;
    auto result = m_engine.execute(cmd);
    if (result.success) {
        if (m_gameMode == GameMode::AiBattle && m_aiBattleStatisticsRepo) {
            DataPaths::ensureDirectories();
            m_aiBattleStatisticsRepo->recordGameStarted(
                DataPaths::aiBattleLogsDir(), m_engine.fullState(),
                m_aiBattleSettings);
        }
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

    const bool showBottomCards = snap.bottomCardsRevealed &&
        snap.bottomCards.size() ==
            static_cast<size_t>(bottomCardsForPlayerCount(snap.activePlayerCount));
    if (m_bottomCardsLabel) {
        if (showBottomCards) {
            const QString bottomCardsText = QString::fromStdWString(
                L"公开底牌：" + CardTextFormatter::formatPublicCards(snap.bottomCards));
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
    m_passButton->setEnabled(isHumanTurn && !landlordMustLeadFirstTurn());
    m_hintButton->setEnabled(isHumanTurn);

    std::wstring statusText = L"手牌:" + std::to_wstring(humanPlayer.hand.size()) + L"张";
    const QString status = QString::fromStdWString(statusText);
    m_statusLabel->setText(status);
    updateTurnCountdown(isHumanTurn);
    updateBackgroundMusic();
    updateBiddingControls();
    refreshVisualCardTable();
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
    if (m_gameMode == GameMode::AiBattle &&
        m_aiBattleSettings.seats[static_cast<std::size_t>(currentPlayer)].kind ==
            SeatControllerKind::CloudAi) {
        requestCloudDecision();
        return;
    }
    const auto difficulty = m_gameMode == GameMode::AiBattle
        ? m_aiBattleSettings.seats[static_cast<std::size_t>(currentPlayer)].localDifficulty
        : static_cast<AiDifficulty>(m_settings.aiDifficulty);
    LocalAiDecisionAdapter ai;
    executeAiBidCommand(ai.requestBid(makeAiObservation(m_engine.state(), currentPlayer),
                                      difficulty));
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
    if (m_gameMode == GameMode::AiBattle &&
        m_aiBattleSettings.seats[static_cast<std::size_t>(currentPlayer)].kind ==
            SeatControllerKind::CloudAi) {
        requestCloudDecision();
        return;
    }
    const auto difficulty = m_gameMode == GameMode::AiBattle
        ? m_aiBattleSettings.seats[static_cast<std::size_t>(currentPlayer)].localDifficulty
        : static_cast<AiDifficulty>(m_settings.aiDifficulty);
    LocalAiDecisionAdapter ai;
    executeAiPlayCommand(ai.requestPlay(makeAiObservation(m_engine.state(), currentPlayer),
                                        difficulty));
}

void MainWindow::executeAiPlayCommand(const GameCommand& command) {
    if (m_diagnosticTrace && !command.aiDecisionReason.empty()) {
        const auto snapshot = m_engine.publicSnapshot();
        int landlordRemaining = -1;
        for (const auto& player : snapshot.players) {
            if (player.role == Role::Landlord) {
                landlordRemaining = player.remainingCards;
                break;
            }
        }
        m_diagnosticTrace->record(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("ai_decision")},
            {QStringLiteral("game_id"), static_cast<qint64>(snapshot.gameId)},
            {QStringLiteral("phase"), gamePhaseText(snapshot.phase)},
            {QStringLiteral("player_id"), static_cast<int>(command.playerId)},
            {QStringLiteral("reason"), QString::fromStdString(command.aiDecisionReason)},
            {QStringLiteral("team_rule_exception"), command.aiTeamRuleException},
            {QStringLiteral("last_played_by"), static_cast<int>(snapshot.lastPlayedBy)},
            {QStringLiteral("last_card_count"),
             static_cast<int>(snapshot.lastPlayedCards.size())},
            {QStringLiteral("landlord_remaining"), landlordRemaining}});
    }
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
    cmd.allowPassAsLeader = TurnManager::isLeader(m_engine.state()) &&
        !landlordMustLeadFirstTurn();
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
        announce(L"当前没有可以压过上一手的牌，可以按Control加回车键过牌",
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
            const PlayerId currentPlayer = m_engine.fullState().currentPlayer;
            const int transitionDelay = std::max(soundDelay, bottomCardsDelay);
            if (currentPlayer != PlayerId::Player1) {
                const uint64_t gameId = m_engine.state().gameId();
                const uint64_t eventSequence = m_engine.fullState().eventSequence;
                QTimer::singleShot(transitionDelay, this,
                    [this, gameId, eventSequence, currentPlayer]() {
                        if (!playResultStillCurrent(gameId, eventSequence,
                                                    GamePhase::Playing,
                                                    currentPlayer)) return;
                        const std::wstring transitionText = L"叫分结束，开始出牌";
                        announce(transitionText, AnnouncementCategory::System,
                                 AnnouncementPriority::High);
                        scheduleAiTurn(readableAnnouncementDelayMilliseconds(transitionText));
                    });
            }
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
        const auto binding = ShortcutSettings::fromKeyEvent(
            keyEvent->key(), keyEvent->modifiers());
        if (m_settings.shortcuts.actionFor(binding)) {
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

    if (msg && msg->message == kKeyboardHookMessage) {
        const LPARAM flags = msg->lParam;
        const auto action = m_settings.shortcuts.actionFor(shortcutFromVirtualKey(
            static_cast<unsigned int>(msg->wParam),
            (flags & kHookCtrlFlag) != 0,
            (flags & kHookShiftFlag) != 0,
            (flags & kHookAltFlag) != 0));
        const bool menuCompatibleAction = action &&
            (*action == ShortcutAction::Battle ||
             *action == ShortcutAction::BottomCards ||
             *action == ShortcutAction::OpenSettings ||
             *action == ShortcutAction::CurrentTurn);
        if (menuCompatibleAction) {
            if (QApplication::activeModalWidget() &&
                QApplication::activeModalWidget() != this) {
                return false;
            }
            if (triggerShortcutAction(*action, QStringLiteral("physical_hook_message"))) {
                if (result) *result = 1;
                return true;
            }
        }
    }

    if (shouldYieldKeyboardHandlingToFocusedWidget()) return false;
    if (msg && msg->message == WM_HOTKEY) {
        switch (static_cast<int>(msg->wParam)) {
        case kHotkeyF11:
            triggerShortcutAction(ShortcutAction::CurrentTurn,
                                  QStringLiteral("windows_hotkey"));
            if (result) *result = 1;
            return true;
        case kHotkeyF12:
            triggerShortcutAction(ShortcutAction::LastAction,
                                  QStringLiteral("windows_hotkey"));
            if (result) *result = 1;
            return true;
        case kHotkeyAltF:
            triggerShortcutAction(ShortcutAction::Score,
                                  QStringLiteral("windows_hotkey"));
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
        const bool altDown = msg->message == WM_SYSKEYDOWN ||
            msg->message == WM_SYSCHAR ||
            (HIWORD(msg->lParam) & KF_ALTDOWN) != 0 ||
            (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if ((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
            isSingleFireKey(static_cast<DWORD>(msg->wParam),
                            ctrlDown, shiftDown, altDown) &&
            (static_cast<quintptr>(msg->lParam) & (quintptr{1} << 30)) != 0) {
            if (result) *result = 1;
            return true;
        }
        const bool handled = handleNativeShortcut(
            static_cast<unsigned int>(msg->wParam),
            ctrlDown,
            shiftDown,
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

    // Shift+Left/Right used to duplicate rank-group browsing. Keep the
    // combination reserved as an explicit no-op so legacy settings and every
    // Windows input path cannot bring the removed behavior back.
    if ((virtualKey == VK_LEFT || virtualKey == VK_RIGHT) &&
        shiftDown && !ctrlDown && !altDown) {
        return true;
    }

    if (virtualKey == VK_F4 && altDown && !ctrlDown && !shiftDown) {
        requestApplicationExit();
        return true;
    }

    const auto configuredAction = m_settings.shortcuts.actionFor(
        shortcutFromVirtualKey(virtualKey, ctrlDown, shiftDown, altDown));
    if (!configuredAction) return false;
    return triggerShortcutAction(*configuredAction,
                                 QStringLiteral("native_key_message"));

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
    if (noModifier && virtualKey >= '1' && virtualKey <= '4') {
        announcePlayerAtPosition(static_cast<int>(virtualKey - '1') + 1);
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
            ((virtualKey == VK_LEFT || virtualKey == VK_RIGHT) && noModifier) ||
            ((virtualKey == VK_HOME || virtualKey == VK_END) && noModifier);
        if (handBrowsing) resumeHandAccessibilityForUserAction();
        int row = m_handView->currentIndex().isValid() ? m_handView->currentIndex().row() : 0;
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
        return true;
    }

    if (virtualKey == VK_RETURN && ctrlDown && !shiftDown && !altDown &&
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
            std::wstring message = L"FeichuanOfflineDoudizhu keyboard hook install failed, error ";
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

        std::wstring message = L"FeichuanOfflineDoudizhu hotkey register failed: ";
        message += name;
        message += L", error ";
        message += std::to_wstring(GetLastError());
        message += L"\n";
        OutputDebugStringW(message.c_str());
    };

    const auto registerConfigured = [&](int id, ShortcutAction action, const wchar_t* name) {
        const auto binding = m_settings.shortcuts.binding(action);
        const UINT virtualKey = virtualKeyFromShortcut(binding);
        if (virtualKey != 0) {
            tryRegister(id, nativeModifiersFromShortcut(binding), virtualKey, name);
        }
    };
    registerConfigured(kHotkeyF11, ShortcutAction::CurrentTurn, L"current turn");
    registerConfigured(kHotkeyF12, ShortcutAction::LastAction, L"last action");
    registerConfigured(kHotkeyAltF, ShortcutAction::Score, L"score");
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

bool MainWindow::triggerShortcutAction(ShortcutAction action, const QString& source) {
    const auto phase = m_engine.state().phase();
    switch (action) {
    case ShortcutAction::Battle:
        triggerBattleShortcut();
        return true;
    case ShortcutAction::BottomCards:
        return triggerBottomCardsShortcut(
            source.isEmpty() ? QStringLiteral("configured_shortcut") : source);
    case ShortcutAction::OpenSettings:
        dismissMenusForGameAction();
        if (m_gameMode == GameMode::AiBattle) {
            openAiBattleSettingsDialog();
        } else {
            openSettingsDialog();
        }
        return true;
    case ShortcutAction::PauseResume: {
        GameCommand cmd;
        if (phase == GamePhase::Playing) {
            cmd.type = GameCommandType::Pause;
        } else if (phase == GamePhase::Paused) {
            cmd.type = GameCommandType::Resume;
        } else {
            return true;
        }
        const auto result = m_engine.execute(cmd);
        refreshFromState();
        if (result.success && cmd.type == GameCommandType::Resume &&
            m_engine.fullState().currentPlayer != PlayerId::Player1) {
            scheduleAiTurn();
        }
        return true;
    }
    case ShortcutAction::ExitApplication:
    case ShortcutAction::QuickExitApplication:
        close();
        return true;
    case ShortcutAction::CurrentTurn:
        announceCurrentTurn();
        return true;
    case ShortcutAction::LastAction:
        announceLastAction();
        return true;
    case ShortcutAction::Score:
        dismissMenusForGameAction();
        announceScore();
        return true;
    case ShortcutAction::ReturnToMainScreen:
        if (phase != GamePhase::NotStarted) returnToMainScreen();
        return true;
    case ShortcutAction::PreviousRankGroup:
    case ShortcutAction::NextRankGroup:
    case ShortcutAction::PreviousWholeRankGroup:
    case ShortcutAction::NextWholeRankGroup:
    case ShortcutAction::FirstRankGroup:
    case ShortcutAction::LastRankGroup: {
        if (!m_handView || !m_handModel || m_handModel->rowCount() <= 0) return false;
        resumeHandAccessibilityForUserAction();
        const int row = m_handView->currentIndex().isValid()
            ? m_handView->currentIndex().row() : 0;
        int target = -1;
        if (action == ShortcutAction::PreviousRankGroup) {
            target = m_handModel->previousBrowsableGroupStartRow(row);
        } else if (action == ShortcutAction::NextRankGroup) {
            target = m_handModel->nextBrowsableGroupStartRow(row);
        } else if (action == ShortcutAction::PreviousWholeRankGroup) {
            target = m_handModel->previousBrowsableMultiCardGroupStartRow(row);
        } else if (action == ShortcutAction::NextWholeRankGroup) {
            target = m_handModel->nextBrowsableMultiCardGroupStartRow(row);
        } else if (action == ShortcutAction::FirstRankGroup) {
            target = m_handModel->firstUnselectedRow();
        } else {
            target = m_handModel->lastBrowsableGroupStartRow();
        }
        if (target >= 0) moveHandCursorTo(target);
        return true;
    }
    case ShortcutAction::PickCard:
        if (phase != GamePhase::Playing || !m_handModel || m_handModel->rowCount() <= 0) {
            return false;
        }
        takeCurrentCard();
        return true;
    case ShortcutAction::PickRankGroup:
        if (phase != GamePhase::Playing || !m_handModel || m_handModel->rowCount() <= 0) {
            return false;
        }
        takeCurrentGroup();
        return true;
    case ShortcutAction::PutDownCard:
        if (phase != GamePhase::Playing || !m_handModel || m_handModel->rowCount() <= 0) {
            return false;
        }
        putDownNextPickedCard();
        return true;
    case ShortcutAction::PutDownAllCards:
        if (phase != GamePhase::Playing || !m_handModel || m_handModel->rowCount() <= 0) {
            return false;
        }
        putDownAllCards();
        return true;
    case ShortcutAction::PlayCards:
        if (isHumanBiddingTurn()) {
            auto* button = focusedBidButton();
            if (!button) {
                cycleBiddingControlFocus(false);
                button = focusedBidButton();
            }
            const int bidValue = m_bidButtons.indexOf(button);
            if (button && button->isEnabled() && bidValue >= 0) onBid(bidValue);
            else announceBiddingPrompt();
            return true;
        }
        if (phase == GamePhase::Playing &&
            m_engine.fullState().currentPlayer == PlayerId::Player1) {
            onPlayCards();
            return true;
        }
        return false;
    case ShortcutAction::Pass:
        if (phase == GamePhase::Playing &&
            m_engine.fullState().currentPlayer == PlayerId::Player1) {
            onPass();
            return true;
        }
        return false;
    case ShortcutAction::BidZero:
        if (isHumanBiddingTurn()) {
            onBid(0);
            return true;
        }
        return false;
    case ShortcutAction::PlayerOneOrBidOne:
    case ShortcutAction::PlayerTwoOrBidTwo:
    case ShortcutAction::PlayerThreeOrBidThree:
    case ShortcutAction::PlayerFour: {
        const int position = action == ShortcutAction::PlayerOneOrBidOne ? 1
            : action == ShortcutAction::PlayerTwoOrBidTwo ? 2
            : action == ShortcutAction::PlayerThreeOrBidThree ? 3 : 4;
        if (position <= 3 && isHumanBiddingTurn()) onBid(position);
        else announcePlayerAtPosition(position);
        return true;
    }
    case ShortcutAction::Count:
        break;
    }
    return false;
}

bool MainWindow::handleKeyPress(QKeyEvent* event) {
    auto phase = m_engine.state().phase();
    const auto modifiers = event->modifiers();
    const auto navigationModifiers = modifiers & ~Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) &&
        navigationModifiers == Qt::ShiftModifier) {
        return true;
    }
    const auto configuredAction = m_settings.shortcuts.actionFor(
        ShortcutSettings::fromKeyEvent(event->key(), modifiers));
    if (configuredAction) {
        const bool repeatable = *configuredAction == ShortcutAction::PreviousRankGroup ||
            *configuredAction == ShortcutAction::NextRankGroup ||
            *configuredAction == ShortcutAction::PreviousWholeRankGroup ||
            *configuredAction == ShortcutAction::NextWholeRankGroup;
        if (event->isAutoRepeat() && !repeatable) return true;
        return triggerShortcutAction(*configuredAction, QStringLiteral("qt_event"));
    }
    if (isHumanBiddingTurn() && navigationModifiers == Qt::NoModifier &&
        event->key() == Qt::Key_Space) {
        auto* button = focusedBidButton();
        if (!button) {
            cycleBiddingControlFocus(false);
            button = focusedBidButton();
        }
        const int bidValue = m_bidButtons.indexOf(button);
        if (button && button->isEnabled() && bidValue >= 0) onBid(bidValue);
        else announceBiddingPrompt();
        return true;
    }
    if (event->key() == Qt::Key_Space && navigationModifiers == Qt::NoModifier &&
        phase == GamePhase::Playing &&
        m_engine.fullState().currentPlayer == PlayerId::Player1) {
        return true;
    }
    return false;

    if (event->key() == Qt::Key_Escape && navigationModifiers == Qt::NoModifier) {
        if (phase != GamePhase::NotStarted) returnToMainScreen();
        return true;
    }
    const bool singleFireKey = event->key() == Qt::Key_F1 ||
        event->key() == Qt::Key_F5 || event->key() == Qt::Key_F11 ||
        event->key() == Qt::Key_F12 || event->key() == Qt::Key_Home ||
        event->key() == Qt::Key_End || event->key() == Qt::Key_Up ||
        event->key() == Qt::Key_Down || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_4);
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
    if (!(modifiers & Qt::KeypadModifier) && navigationModifiers == Qt::NoModifier &&
        event->key() >= Qt::Key_1 && event->key() <= Qt::Key_4) {
        announcePlayerAtPosition(event->key() - Qt::Key_1 + 1);
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
             handNoModifier) ||
            ((event->key() == Qt::Key_Home || event->key() == Qt::Key_End) && handNoModifier);
        if (handBrowsing) resumeHandAccessibilityForUserAction();
        int row = m_handView->currentIndex().isValid() ? m_handView->currentIndex().row() : 0;
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
        return true;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && navigationModifiers == Qt::ControlModifier
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
        QMessageBox message(QMessageBox::Question,
                            QString::fromStdWString(L"确认退出"),
                            QString::fromStdWString(L"牌局正在进行，确认要退出吗？"),
                            QMessageBox::Yes | QMessageBox::No, this);
        message.button(QMessageBox::Yes)->setText(QString::fromStdWString(L"是"));
        message.button(QMessageBox::No)->setText(QString::fromStdWString(L"否"));
        message.setDefaultButton(QMessageBox::No);
        message.setEscapeButton(QMessageBox::No);
        const auto result = static_cast<QMessageBox::StandardButton>(message.exec());
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

std::wstring MainWindow::playedCardsPlayerDisplayName(PlayerId playerId) const {
    const auto& players = m_engine.fullState().players;
    const auto player = std::find_if(players.begin(), players.end(),
        [playerId](const PlayerState& candidate) { return candidate.id == playerId; });
    if (player != players.end() && player->role == Role::Landlord) {
        return L"地主";
    }
    return playerDisplayName(playerId);
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
        return playedCardsPlayerDisplayName(event.playerId) + L"出了" +
               CardTextFormatter::formatPlayedCards(event.pattern, event.cards, m_engine.fullState().activePlayerCount);
    case GameEventType::PlayerPassed:
        return playedCardsPlayerDisplayName(event.playerId);
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

    const std::wstring playerName = playedCardsPlayerDisplayName(event.playerId);
    const std::wstring visibleText = playerName + L"，" +
        CardTextFormatter::formatPlayedCards(event.pattern, event.cards, m_engine.fullState().activePlayerCount);
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
        QTimer::singleShot(0, this, [this, actionText]() {
            announce(actionText, AnnouncementCategory::Pass,
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
    const QString updaterPath = QDir(QCoreApplication::applicationDirPath()).filePath(
        QString::fromUtf8(u8"飞船斗地主更新器.exe"));
    const QStringList arguments{
        manual ? QStringLiteral("--manual") : QStringLiteral("--automatic"),
        QStringLiteral("--current-version"), QCoreApplication::applicationVersion()};
    if (!QFileInfo::exists(updaterPath) ||
        !QProcess::startDetached(updaterPath, arguments)) {
        if (manual) {
            QMessageBox::warning(this, QString::fromUtf8(u8"无法启动更新器"),
                QString::fromUtf8(u8"未找到或无法启动“飞船斗地主更新器.exe”。游戏可继续离线运行。"));
        }
        return;
    }
    if (!manual) {
        m_settings.lastAutomaticUpdateCheckDate =
            QDate::currentDate().toString(Qt::ISODate);
        saveSettings();
    }
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
    const bool modeChangesNextRound =
        (m_engine.state().phase() == GamePhase::Bidding ||
         m_engine.state().phase() == GamePhase::Playing ||
         m_engine.state().phase() == GamePhase::Paused) &&
        m_settings.playerCount != m_engine.fullState().activePlayerCount;
    announce(modeChangesNextRound ? L"设置已保存，游戏人数将在下一局生效"
                                  : L"设置已保存",
             AnnouncementCategory::System);
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

void MainWindow::loadAiBattleSettings() {
    if (m_aiBattleSettingsRepo &&
        m_aiBattleSettingsRepo->load(DataPaths::aiBattleSettingsFile())) {
        const QJsonObject saved = m_aiBattleSettingsRepo->data();
        m_aiBattleSettings = AiBattleSettings::fromJson(saved);
        if (!saved.contains(QStringLiteral("autoPassEnabled"))) {
            m_aiBattleSettings.autoPassEnabled = m_settings.autoPassEnabled;
        }
        if (!saved.contains(QStringLiteral("autoPassSeconds"))) {
            m_aiBattleSettings.autoPassSeconds = m_settings.autoPassSeconds;
        }
    } else {
        m_aiBattleSettings = AiBattleSettings{};
        m_aiBattleSettings.autoPassEnabled = m_settings.autoPassEnabled;
        m_aiBattleSettings.autoPassSeconds = m_settings.autoPassSeconds;
    }
    m_aiBattleSettings.normalize();
}

void MainWindow::saveAiBattleSettings() {
    if (!m_aiBattleSettingsRepo) return;
    DataPaths::ensureDirectories();
    m_aiBattleSettings.normalize();
    m_aiBattleSettingsRepo->setData(m_aiBattleSettings.toJson());
    m_aiBattleSettingsRepo->save(DataPaths::aiBattleSettingsFile());
}

void MainWindow::ensureAiBattleFirstRunPrompt() {
    if (m_gameMode != GameMode::AiBattle || !m_aiService) return;
    if (qApp->property("fpdz.suppressStartupPrompts").toBool()) return;
    const auto response = m_aiService->requestSync({
        {QStringLiteral("type"), QStringLiteral("credentials_list")}});
    if (!response.value("ok").toBool() ||
        !response.value("credentials").toArray().isEmpty()) return;
    announce(L"尚未配置云模型", AnnouncementCategory::System,
             AnnouncementPriority::High);
    QMessageBox message(QMessageBox::Information,
                        QString::fromUtf8(u8"尚未配置云模型"),
                        QString::fromUtf8(u8"AI对战至少需要一个云模型认证。现在只打开认证管理，不会自动联网，也不会创建空认证。"),
                        QMessageBox::Ok | QMessageBox::Cancel, this);
    message.button(QMessageBox::Ok)->setText(QString::fromUtf8(u8"打开认证管理"));
    message.button(QMessageBox::Cancel)->setText(QString::fromUtf8(u8"稍后设置"));
    if (message.exec() == QMessageBox::Ok) {
        CredentialManagerDialog dialog(*m_aiService, {}, this);
        dialog.exec();
    }
}

void MainWindow::openAiBattleSettingsDialog() {
    if (!m_aiService || !m_aiService->isRunning()) {
        QMessageBox::warning(this, QString::fromUtf8(u8"AI服务不可用"),
                             m_aiService ? m_aiService->lastError()
                                         : QString::fromUtf8(u8"AI服务尚未启动"));
        return;
    }
    const auto phase = m_engine.state().phase();
    const bool inProgress = phase == GamePhase::Bidding || phase == GamePhase::Playing ||
                            phase == GamePhase::Paused;
    AiBattleSettingsDialog dialog(m_aiBattleSettings, *m_aiService, inProgress, this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_aiBattleSettings = dialog.settings();
    saveAiBattleSettings();
    updateTurnCountdown(m_engine.state().phase() == GamePhase::Playing &&
                        m_engine.fullState().currentPlayer == PlayerId::Player1);
    announce(L"AI对战设置已保存", AnnouncementCategory::System);
}

bool MainWindow::validateAiBattleStart() {
    QString reason;
    if (!m_aiBattleSettings.validForStart(&reason)) {
        QMessageBox message(QMessageBox::Information,
                            QString::fromUtf8(u8"AI对战尚未配置完成"), reason,
                            QMessageBox::Ok | QMessageBox::Cancel, this);
        message.button(QMessageBox::Ok)->setText(QString::fromUtf8(u8"打开AI设置"));
        message.button(QMessageBox::Cancel)->setText(QString::fromUtf8(u8"取消"));
        if (message.exec() == QMessageBox::Ok) openAiBattleSettingsDialog();
        return false;
    }
    if (!m_aiService || !m_aiService->isRunning()) {
        QMessageBox::warning(this, QString::fromUtf8(u8"AI服务不可用"),
                             m_aiService ? m_aiService->lastError()
                                         : QString::fromUtf8(u8"AI服务尚未启动"));
        return false;
    }
    return true;
}

bool MainWindow::confirmAiBattlePrivacy() {
    if (!m_aiService) return false;
    const auto response = m_aiService->requestSync({
        {QStringLiteral("type"), QStringLiteral("credentials_list")}});
    if (!response.value("ok").toBool()) return false;
    const auto credentials = response.value("credentials").toArray();
    for (int seatIndex = 1; seatIndex < m_aiBattleSettings.playerCount; ++seatIndex) {
        const auto& seat = m_aiBattleSettings.seats[static_cast<std::size_t>(seatIndex)];
        if (seat.kind != SeatControllerKind::CloudAi) continue;
        QJsonObject credential;
        for (const auto& value : credentials) {
            if (value.toObject().value("id").toString() == seat.credentialId) {
                credential = value.toObject();
                break;
            }
        }
        const QString host = QUrl(credential.value("request_url").toString()).host().toLower();
        if (host.isEmpty()) return false;
        if (m_aiBattleSettings.privacyConsents.value(seat.credentialId).toString() == host) continue;
        QMessageBox message(QMessageBox::Warning,
                            QString::fromUtf8(u8"确认发送整桌牌局信息"),
                            QString::fromUtf8(u8"认证“%1”将向主机 %2 发送真人手牌、其他机器人手牌、隐藏底牌、二人模式盖牌和牌局历史。是否同意？")
                                .arg(credential.value("name").toString(), host),
                            QMessageBox::Yes | QMessageBox::No, this);
        message.button(QMessageBox::Yes)->setText(QString::fromUtf8(u8"同意"));
        message.button(QMessageBox::No)->setText(QString::fromUtf8(u8"不同意"));
        message.setDefaultButton(QMessageBox::No);
        if (message.exec() != QMessageBox::Yes) return false;
        m_aiBattleSettings.privacyConsents[seat.credentialId] = host;
    }
    saveAiBattleSettings();
    return true;
}

void MainWindow::requestCloudDecision() {
    if (!m_aiService || m_pendingCloudRequest) return;
    const auto phase = m_engine.state().phase();
    if (phase != GamePhase::Bidding && phase != GamePhase::Playing) return;
    const PlayerId player = m_engine.fullState().currentPlayer;
    if (player == PlayerId::Player1) return;
    const auto& controller =
        m_aiBattleSettings.seats[static_cast<std::size_t>(player)];
    m_cloudTurnPaused = false;
    if (m_retryCloudAction) m_retryCloudAction->setEnabled(false);
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingCloudRequest = AiActionCatalog::create(
        m_engine.state(), player, controller, requestId);
    m_pendingCloudRequest->strategyPrompt = m_aiBattleSettings.strategyPrompt;
    const QByteArray encoded = QJsonDocument(m_pendingCloudRequest->toServiceJson())
                                   .toJson(QJsonDocument::Compact);
    if (m_aiBattleStatisticsRepo) {
        DataPaths::ensureDirectories();
        m_aiBattleStatisticsRepo->recordDecisionStarted(
            DataPaths::aiBattleLogsDir(), *m_pendingCloudRequest, encoded.size());
    }
    if (encoded.size() > AI_MAX_MESSAGE_BYTES) {
        recordCloudRequestOutcome(*m_pendingCloudRequest, false,
                                  QStringLiteral("request_rejected"),
                                  QStringLiteral("prompt_too_large"), 0);
        showCloudFault(QString::fromUtf8(u8"本回合模型请求超过128 KiB"));
        return;
    }
    // AI 思考过程不朗读也不显示：直接等结果，出牌时再正常播报。
    m_cloudWaitElapsed.restart();
    if (!m_aiService->requestDecision(*m_pendingCloudRequest)) {
        recordCloudRequestOutcome(*m_pendingCloudRequest, false,
                                  QStringLiteral("client_failure"),
                                  QStringLiteral("ipc_send_failed"), 0);
        showCloudFault(m_aiService->lastError());
    }
}

void MainWindow::handleAiServiceMessage(const QJsonObject& message) {
    if (message.value("type").toString() == QStringLiteral("decision_result")) {
        handleCloudDecisionResponse(message);
    }
}

void MainWindow::handleCloudDecisionResponse(const QJsonObject& message) {
    if (!m_pendingCloudRequest) return;
    const AiDecisionResponse response = AiDecisionResponse::fromServiceJson(message);
    const auto request = *m_pendingCloudRequest;
    if (response.requestId != request.requestId) return;
    stopCloudWait();
    if (!response.matches(request, m_engine.state())) {
        recordCloudRequestOutcome(request, false,
                                  QStringLiteral("response_rejected"),
                                  QStringLiteral("response_mismatch"),
                                  response.latencyMilliseconds,
                                  response.inputTokens, response.outputTokens,
                                  response.actionId);
        m_pendingCloudRequest.reset();
        showCloudFault(QString::fromUtf8(u8"已丢弃错位或过期的模型响应"));
        return;
    }
    if (!response.success) {
        recordCloudRequestOutcome(request, false,
                                  QStringLiteral("service_error"),
                                  response.errorCode, response.latencyMilliseconds,
                                  response.inputTokens, response.outputTokens);
        m_pendingCloudRequest.reset();
        showCloudFault(response.safeMessage.isEmpty()
            ? QString::fromUtf8(u8"云模型请求失败") : response.safeMessage);
        return;
    }
    const AiLegalAction* action = AiActionCatalog::find(request, response.actionId);
    if (!action) {
        recordCloudRequestOutcome(request, false,
                                  QStringLiteral("response_rejected"),
                                  QStringLiteral("unknown_action_id"),
                                  response.latencyMilliseconds,
                                  response.inputTokens, response.outputTokens,
                                  response.actionId);
        m_pendingCloudRequest.reset();
        showCloudFault(QString::fromUtf8(u8"模型返回了不存在的动作编号"));
        return;
    }
    const GameCommand command = action->command;
    m_pendingCloudRequest.reset();
    if (request.phase == GamePhase::Bidding) executeAiBidCommand(command);
    else executeAiPlayCommand(command);
    // executeAi* performs the final GameEngine::execute legality check. A rejected
    // command leaves the turn unchanged and is surfaced as a model fault below.
    if (m_engine.state().gameId() == request.gameId &&
        m_engine.fullState().eventSequence == request.eventSequence) {
        recordCloudRequestOutcome(request, false,
                                  QStringLiteral("engine_rejected"),
                                  QStringLiteral("engine_rejected"),
                                  response.latencyMilliseconds,
                                  response.inputTokens, response.outputTokens,
                                  response.actionId);
        showCloudFault(QString::fromUtf8(u8"本地引擎拒绝了模型动作"));
    } else {
        recordCloudRequestOutcome(request, true, QStringLiteral("accepted"), {},
                                  response.latencyMilliseconds,
                                  response.inputTokens, response.outputTokens,
                                  response.actionId);
    }
}

void MainWindow::recordCloudRequestOutcome(
    const AiDecisionRequest& request, bool success, const QString& outcome,
    const QString& errorCode, int latencyMilliseconds, qint64 inputTokens,
    qint64 outputTokens, int actionId) {
    if (!m_aiBattleStatisticsRepo) return;
    DataPaths::ensureDirectories();
    m_aiBattleStatisticsRepo->recordRequest(
        static_cast<int>(request.playerId), request.controller, success,
        errorCode, latencyMilliseconds, inputTokens, outputTokens);
    m_aiBattleStatisticsRepo->save(DataPaths::aiBattleStatisticsFile());
    m_aiBattleStatisticsRepo->recordDecisionFinished(
        DataPaths::aiBattleLogsDir(), request, success, outcome, errorCode,
        latencyMilliseconds, inputTokens, outputTokens, actionId,
        success && !m_engine.fullState().actionHistory.empty()
            ? static_cast<qint64>(m_engine.fullState().actionHistory.back().sequence) : -1);
    // The winning cloud move may finish the round before its request outcome is
    // recorded. Rewrite the same detailed file with that final decision included.
    if (m_engine.state().phase() == GamePhase::Finished &&
        m_engine.state().gameId() == request.gameId) {
        m_aiBattleStatisticsRepo->saveDetailedGame(
            DataPaths::aiBattleReplaysDir() + QStringLiteral("/detailed"),
            m_engine.fullState().roundResult, m_engine.fullState(), m_aiBattleSettings);
    }
}

void MainWindow::showCloudFault(const QString& safeMessage) {
    stopCloudWait();
    m_pendingCloudRequest.reset();
    m_cloudTurnPaused = true;
    if (m_retryCloudAction) m_retryCloudAction->setEnabled(true);
    const PlayerId player = m_engine.fullState().currentPlayer;
    const auto& controller = m_aiBattleSettings.seats[static_cast<std::size_t>(player)];
    announce(playerDisplayName(player) + L"，" + controller.credentialName.toStdWString() +
                 L"，" + controller.model.toStdWString() + L"，" +
                 safeMessage.toStdWString(),
             AnnouncementCategory::Error, AnnouncementPriority::High);
    QMessageBox message(QMessageBox::Critical,
                        QString::fromUtf8(u8"云模型回合已暂停"),
                        QString::fromUtf8(u8"认证：%1\n模型：%2\n%3")
                            .arg(controller.credentialName, controller.model, safeMessage),
                        QMessageBox::NoButton, this);
    auto* retry = message.addButton(QString::fromUtf8(u8"重试"), QMessageBox::AcceptRole);
    auto* settings = message.addButton(QString::fromUtf8(u8"打开AI设置"), QMessageBox::ActionRole);
    auto* back = message.addButton(QString::fromUtf8(u8"返回模式选择"), QMessageBox::DestructiveRole);
    auto* keepPaused = message.addButton(QString::fromUtf8(u8"继续暂停"), QMessageBox::RejectRole);
    keepPaused->hide();
    message.setEscapeButton(keepPaused);
    message.exec();
    if (message.clickedButton() == retry) {
        retryCloudTurn();
    } else if (message.clickedButton() == settings) {
        openAiBattleSettingsDialog();
    } else if (message.clickedButton() == back) {
        requestReturnToModeSelection();
    }
}

void MainWindow::retryCloudTurn() {
    if (!m_cloudTurnPaused || m_pendingCloudRequest) return;
    const auto phase = m_engine.state().phase();
    const PlayerId player = m_engine.fullState().currentPlayer;
    if ((phase != GamePhase::Bidding && phase != GamePhase::Playing) ||
        player == PlayerId::Player1 ||
        m_aiBattleSettings.seats[static_cast<std::size_t>(player)].kind !=
            SeatControllerKind::CloudAi) {
        return;
    }
    m_cloudTurnPaused = false;
    if (m_retryCloudAction) m_retryCloudAction->setEnabled(false);
    if (phase == GamePhase::Bidding) processAiBid();
    else processAiPlay();
}

void MainWindow::stopCloudWait() {
    if (m_countdownLabel && m_engine.fullState().currentPlayer != PlayerId::Player1) {
        m_countdownLabel->setVisible(false);
    }
}

void MainWindow::openShortcutDialog() {
    ShortcutDialog dialog(m_settings.shortcuts, this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_settings.shortcuts = dialog.settings();
    m_settings.shortcuts.normalize();
    saveSettings();
    applyShortcutBindings();
    announce(L"快捷键设置已保存", AnnouncementCategory::System);
    QTimer::singleShot(0, this, [this]() { setFocus(Qt::OtherFocusReason); });
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
    edit->setAccessibleName(seatName + QString::fromUtf8(u8"的新名称"));
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
    if (landlordMustLeadFirstTurn()) {
        announce(L"地主首轮必须出牌，请选择要出的牌", AnnouncementCategory::Turn,
                 AnnouncementPriority::High);
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

void MainWindow::requestReturnToModeSelection() {
    const auto phase = m_engine.state().phase();
    if (phase == GamePhase::Bidding || phase == GamePhase::Playing ||
        phase == GamePhase::Paused) {
        QMessageBox message(QMessageBox::Question,
                            QString::fromUtf8(u8"确认返回模式选择"),
                            QString::fromUtf8(u8"牌局正在进行，返回将放弃本局，是否继续？"),
                            QMessageBox::Yes | QMessageBox::No, this);
        message.button(QMessageBox::Yes)->setText(QString::fromUtf8(u8"是"));
        message.button(QMessageBox::No)->setText(QString::fromUtf8(u8"否"));
        message.setDefaultButton(QMessageBox::No);
        message.setEscapeButton(QMessageBox::No);
        if (message.exec() != QMessageBox::Yes) return;
    }
    if (m_aiTimer) m_aiTimer->stop();
    if (m_turnCountdownTimer) m_turnCountdownTimer->stop();
    if (m_pendingCloudRequest && m_aiService) {
        m_aiService->cancel(m_pendingCloudRequest->requestId);
        m_pendingCloudRequest.reset();
    }
    stopCloudWait();
    emit returnToModeSelectionRequested();
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

    out << "飞船斗地主诊断报告\n";
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
        QString::fromUtf8(u8"飞船斗地主.exe"), QStringLiteral("Qt6Core.dll"),
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
    out << "active_player_count: " << snapshot.activePlayerCount << "\n";
    out << "phase: " << gamePhaseText(snapshot.phase) << "\n";
    out << "current_player: " << (static_cast<int>(snapshot.currentPlayer) + 1) << "\n";
    out << "base_score: " << snapshot.baseScore << "\n";
    out << "multiplier: " << snapshot.currentMultiplier << "\n";
    out << "bottom_revealed: " << (snapshot.bottomCardsRevealed ? "是" : "否") << "\n";
    out << "bottom_count: " << snapshot.bottomCards.size() << "\n";
    out << "bottom_auto_announced_this_round: "
        << (m_bottomCardsAnnouncedGameId == snapshot.gameId ? "是" : "否") << "\n";
    out << "last_bottom_cards_query_time: " << m_lastBottomCardsQueryTime << "\n";
    for (int index = 0; index < snapshot.activePlayerCount; ++index) {
        const auto& player = snapshot.players[static_cast<size_t>(index)];
        out << "player_" << (index + 1) << "_remaining: " << player.remainingCards
            << ", role=" << static_cast<int>(player.role)
            << ", bid=" << player.bidScore << "\n";
    }
    const auto& humanCards = m_engine.fullState().players[0].hand.cards();
    out << "human_hand: " << QString::fromStdWString(CardTextFormatter::formatCards(humanCards)) << "\n";

    out << "\n[安全设置摘要]\n";
    out << "ai_difficulty: " << m_settings.aiDifficulty << "\n";
    out << "configured_player_count: " << m_settings.playerCount << "\n";
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
    bool selected = false;
    if (!m_handModel->isSelected(row)) {
        selected = m_handModel->selectSingle(row);
        playSound(SoundId::CardSelect);
    }
    if (selected) {
        m_handModel->completeEndpointSelection(
            m_engine.publicSnapshot().activePlayerCount, true);
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
    if (selected) {
        const auto conciseText = conciseSelectedSequenceText(
            m_handModel->selectedCards(),
            m_engine.publicSnapshot().activePlayerCount);
        announce(conciseText.has_value()
                     ? *conciseText
                     : CardTextFormatter::formatRankSpeech(card.rank()),
                 AnnouncementCategory::CardSelection,
                 AnnouncementPriority::Normal, false);
    }
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
    // 整组入口不凭单张首尾补顺子，但仍允许各两张的首尾补连对。
    m_handModel->completeEndpointSelection(
        m_engine.publicSnapshot().activePlayerCount, false);
    playSound(SoundId::CardSelect);
    const auto conciseGroupText = conciseSelectedSequenceText(
        m_handModel->selectedCards(),
        m_engine.publicSnapshot().activePlayerCount);
    announce(conciseGroupText.has_value()
                 ? *conciseGroupText
                 : formatCardSelectionGroup(result.rank, result.groupCount),
             AnnouncementCategory::CardSelection,
             AnnouncementPriority::Normal, false);
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
    announce(CardTextFormatter::formatRankSpeech(card->rank()),
             AnnouncementCategory::CardSelection,
             AnnouncementPriority::Normal, false);
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
    const auto selectedCards = m_handModel->selectedCards();
    const int selectedCount = m_handModel->selectedCount();
    suppressHandAccessibilityUntilUserAction();
    m_handModel->clearSelection();
    if (selectedCount > 0) {
        playSound(SoundId::CardDeselect);
        const auto concisePutDownText = conciseSelectedSequenceText(
            selectedCards, m_engine.publicSnapshot().activePlayerCount);
        announce(concisePutDownText.has_value() ? *concisePutDownText
                                                : formatCardSelection(selectedCards),
                 AnnouncementCategory::CardSelection,
                 AnnouncementPriority::Normal, false);
    }
    QJsonObject details;
    details[QStringLiteral("released_count")] = selectedCount;
    traceHandAction(QStringLiteral("put_down_all"), traceBefore, details);
}

void MainWindow::syncPickedCardsToView() {
    // Sync is automatic through model
}

void MainWindow::refreshVisualCardTable() {
    if (!m_cardTable || !m_handModel) return;
    m_cardTable->setTableState(m_engine.publicSnapshot(),
                               m_engine.fullState().players[0].hand.cards(),
                               m_handModel->selectedCardIds());
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
        if (m_pendingCloudRequest && m_aiService) {
            m_aiService->cancel(m_pendingCloudRequest->requestId);
            m_pendingCloudRequest.reset();
            stopCloudWait();
        }
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
    if (phase == GamePhase::Paused &&
        m_engine.fullState().currentPlayer != PlayerId::Player1) {
        scheduleAiTurn();
    }
}

void MainWindow::announcePlayerAtPosition(int position) {
    if (position < 1 || position > 4) return;

    const auto& state = m_engine.fullState();
    if (position > state.activePlayerCount && state.gameId != 0) {
        const std::wstring modeName = state.activePlayerCount == TWO_PLAYER_COUNT
            ? L"二人" : L"三人";
        announce(L"当前" + modeName + L"模式没有" +
                     playerIdDisplayName(static_cast<PlayerId>(position - 1)),
                 AnnouncementCategory::System);
        return;
    }
    PlayerId pid = static_cast<PlayerId>(position - 1);
    const auto& player = state.players[static_cast<size_t>(pid)];

    if (player.role == Role::Landlord) {
        announce(L"地主，还剩" + std::to_wstring(player.remainingCards()) + L"张牌",
                 AnnouncementCategory::System);
        return;
    }

    std::wstring text = playerDisplayName(pid);
    if (player.isHuman || pid == PlayerId::Player1) text += L"，自己";
    text += L"，剩余" + std::to_wstring(player.remainingCards()) + L"张牌";
    text += player.role == Role::Farmer ? L"，农民" : L"，身份未定";

    announce(text, AnnouncementCategory::System);
}

void MainWindow::copyAiBattleGames() {
    if (!m_aiBattleStatisticsRepo) return;
    const QByteArray bytes = m_aiBattleStatisticsRepo->detailedGamesForCopy(
        DataPaths::aiBattleReplaysDir() + QStringLiteral("/detailed"));
    if (bytes.isEmpty()) {
        announce(L"还没有已完成的AI对战牌局记录", AnnouncementCategory::System);
        return;
    }
    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) {
        announce(L"复制对战记录失败", AnnouncementCategory::Error, AnnouncementPriority::High);
        return;
    }
    clipboard->setText(QString::fromUtf8(bytes), QClipboard::Clipboard);
    announce(L"最近50局完整AI对战记录已复制", AnnouncementCategory::System,
             AnnouncementPriority::High);
}

void MainWindow::announceBottomCards() {
    const auto snapshot = m_engine.publicSnapshot();
    if (!snapshot.bottomCardsRevealed || snapshot.bottomCards.size() !=
        static_cast<size_t>(bottomCardsForPlayerCount(snapshot.activePlayerCount))) {
        if (snapshot.phase == GamePhase::Bidding) {
            announce(L"底牌尚未公开", AnnouncementCategory::System);
        } else {
            announce(L"当前没有公开底牌", AnnouncementCategory::System);
        }
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
    const bool containsReveal = std::any_of(result.events.begin(), result.events.end(),
        [](const GameEvent& event) {
            return event.type == GameEventType::BottomCardsRevealed;
        });
    const auto snapshot = m_engine.publicSnapshot();
    if (!containsReveal || snapshot.phase != GamePhase::Playing ||
        !snapshot.bottomCardsRevealed || snapshot.bottomCards.size() !=
            static_cast<size_t>(bottomCardsForPlayerCount(snapshot.activePlayerCount)) ||
        m_bottomCardsAnnouncedGameId == snapshot.gameId) {
        return 0;
    }

    m_bottomCardsAnnouncedGameId = snapshot.gameId;
    std::wstring text = L"地主已经确定。公开底牌：";
    text += CardTextFormatter::formatPublicCards(snapshot.bottomCards);
    text += L"。地主先出牌";
    if (m_diagnosticTrace) {
        const QJsonObject details{
            {QStringLiteral("game_id"), static_cast<qint64>(snapshot.gameId)},
            {QStringLiteral("phase"), gamePhaseText(snapshot.phase)},
            {QStringLiteral("bottom_count"), static_cast<int>(snapshot.bottomCards.size())},
            {QStringLiteral("route"), QString::fromStdWString(m_accessibility.routeName())},
            {QStringLiteral("result"), QStringLiteral("delivered")}};
        QJsonObject revealed = details;
        revealed[QStringLiteral("type")] = QStringLiteral("bottom_cards_revealed");
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
        const auto pattern = PatternAnalyzer::analyze(
            state.lastPlayedCards, state.activePlayerCount);
        std::wstring text = playedCardsPlayerDisplayName(state.lastPlayedBy) + L"，";
        text += CardTextFormatter::formatPlayedCards(pattern, state.lastPlayedCards, state.activePlayerCount);
        announce(text, AnnouncementCategory::System);
    } else if (!m_lastPlayedCardsText.empty()) {
        announce(m_lastPlayedCardsText, AnnouncementCategory::System);
    } else {
        announce(L"空", AnnouncementCategory::System);
    }
}

void MainWindow::rememberLastAction(const CommandResult& result) {
    if (m_gameMode == GameMode::AiBattle && m_aiBattleStatisticsRepo) {
        m_aiBattleStatisticsRepo->recordPublicAction(m_engine.fullState());
    }
    for (const auto& event : result.events) {
        if (event.type == GameEventType::CardsPlayed && !event.cards.empty()) {
            m_lastPlayedCardsText = playedCardsPlayerDisplayName(event.playerId) + L"，" +
                CardTextFormatter::formatPlayedCards(event.pattern, event.cards, m_engine.fullState().activePlayerCount);
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

    if (m_gameMode == GameMode::AiBattle && m_aiBattleStatisticsRepo) {
        DataPaths::ensureDirectories();
        m_aiBattleStatisticsRepo->recordRound(
            round, m_engine.fullState(), m_aiBattleSettings);
        m_aiBattleStatisticsRepo->save(DataPaths::aiBattleStatisticsFile());
        m_aiBattleStatisticsRepo->recordRoundFinished(
            DataPaths::aiBattleLogsDir(), round, m_engine.fullState(),
            m_aiBattleSettings);
        m_aiBattleStatisticsRepo->saveReplaySummary(
            DataPaths::aiBattleReplaysDir(), round, m_engine.fullState(),
            m_aiBattleSettings);
        m_aiBattleStatisticsRepo->saveDetailedGame(
            DataPaths::aiBattleReplaysDir() + QStringLiteral("/detailed"),
            round, m_engine.fullState(), m_aiBattleSettings);
    } else if (m_statisticsRepo) {
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
    for (int index = 0; index < state.activePlayerCount; ++index) {
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
    if (m_pendingCloudRequest && m_aiService) {
        m_aiService->cancel(m_pendingCloudRequest->requestId);
        m_pendingCloudRequest.reset();
    }
    m_cloudTurnPaused = false;
    if (m_retryCloudAction) m_retryCloudAction->setEnabled(false);
    stopCloudWait();
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

bool MainWindow::landlordMustLeadFirstTurn() const {
    const bool enabled = m_gameMode == GameMode::AiBattle
        ? m_aiBattleSettings.landlordMustLeadFirstTurn
        : m_settings.landlordMustLeadFirstTurn;
    if (!enabled || m_engine.state().phase() != GamePhase::Playing) return false;
    const auto& state = m_engine.fullState();
    if (state.currentPlayer != PlayerId::Player1 ||
        state.players[0].role != Role::Landlord ||
        !TurnManager::isLeader(m_engine.state())) {
        return false;
    }
    return std::none_of(state.actionHistory.begin(), state.actionHistory.end(),
        [](const PublicActionRecord& action) {
            return action.type == PublicActionType::Play ||
                   action.type == PublicActionType::Pass;
        });
}

void MainWindow::updateTurnCountdown(bool humanTurn) {
    const bool autoPassEnabled = m_gameMode == GameMode::AiBattle
        ? m_aiBattleSettings.autoPassEnabled : m_settings.autoPassEnabled;
    const int autoPassSeconds = m_gameMode == GameMode::AiBattle
        ? m_aiBattleSettings.autoPassSeconds : m_settings.autoPassSeconds;
    if (humanTurn && autoPassEnabled && !landlordMustLeadFirstTurn()) {
        m_turnSecondsRemaining = autoPassSeconds;
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
    m_lastSpeechDeliveryChannel = QStringLiteral("screen_reader_router");
    m_lastSpeechDeliveryResult = QStringLiteral("读屏路由尚未提交");
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
        ? QStringLiteral("已提交到当前读屏官方接口或Qt/UIA兜底")
        : QStringLiteral("朗读已去重或目标不可用");
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
