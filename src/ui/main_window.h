#pragma once
#include <QAbstractNativeEventFilter>
#include <QMainWindow>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QWidget>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QTimer>
#include <QElapsedTimer>
#include <QAction>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>
#include "sound_service.h"
#include "../app/app_settings.h"
#include "../core/engine/game_command.h"
#include "../core/engine/game_event.h"
#include "../core/engine/game_state.h"
#include "../accessibility/announcement.h"
#include "../app/game_mode.h"
#include "../ai/ai_decision_request.h"
class QShortcut;
namespace fpdz {
class GameEngine;
class HandListModel;
class PlayerStatusModel;
class AccessibilityService;
class SettingsRepository;
class StatisticsRepository;
class SoundService;
class DiagnosticTraceService;
class ResultDialog;
class CardTableWidget;
class ShortcutDialog;
class AiServiceClient;
class AiBattleStatisticsRepository;
class MainWindow : public QMainWindow, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit MainWindow(GameEngine& engine, AccessibilityService& accessibility,
                        DiagnosticTraceService* diagnosticTrace = nullptr,
                        GameMode gameMode = GameMode::Offline,
                        QWidget* parent = nullptr);
    ~MainWindow() override;
    void startNewGame();
    void refreshFromState(int preferredHandRow = -1,
                          bool restoreHandFocusSilently = false);
signals:
    void returnToModeSelectionRequested();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent* event) override;
private slots:
    void onAiTurn();
private:
    void setupMenus();
    void openTopLevelMenu(QMenu* menu);
    void openSubMenu(QMenu* parentMenu, QMenu* subMenu);
    bool isTopLevelMenuNavigationActive() const;
    void activateTopLevelMenuNavigation();
    void cycleTopLevelMenuFocus(bool backwards);
    void dismissMenusForGameAction();
    void setupUi();
    void setupShortcuts();
    void applyShortcutBindings();
    bool triggerShortcutAction(ShortcutAction action, const QString& source = {});
    void onPlayCards();
    void onPass();
    void onHint();
    void onBid(int value);
    void showBiddingControls(bool announcePrompt = true);
    void hideBiddingControls();
    void updateBiddingControls();
    bool isHumanBiddingTurn() const;
    QPushButton* focusedBidButton() const;
    void cycleBiddingControlFocus(bool backwards);
    void announceBiddingPrompt();
    void processAiBid();
    void processAiPlay();
    void loadSettings();
    void saveSettings();
    void openSettingsDialog();
    void openAiBattleSettingsDialog();
    void ensureAiBattleFirstRunPrompt();
    bool validateAiBattleStart();
    bool confirmAiBattlePrivacy();
    void loadAiBattleSettings();
    void saveAiBattleSettings();
    void requestCloudDecision();
    void handleAiServiceMessage(const QJsonObject& message);
    void handleCloudDecisionResponse(const QJsonObject& message);
    void recordCloudRequestOutcome(const AiDecisionRequest& request,
                                   bool success, const QString& outcome,
                                   const QString& errorCode, int latencyMilliseconds,
                                   qint64 inputTokens = -1, qint64 outputTokens = -1,
                                   int actionId = -1);
    void showCloudFault(const QString& safeMessage);
    void retryCloudTurn();
    void stopCloudWait();
    void openShortcutDialog();
    void openSoundManagerDialog();
    bool openHelpTextFile(const QString& fileName);
    void showDonateDialog();
    void checkForUpdates(bool manual);
    void openPlayerNameDialog(PlayerId playerId);
    void resetPlayerDisplayNames();
    void scheduleAiTurn(int minimumDelayMilliseconds = 0);
    void executeAiBidCommand(const GameCommand& command);
    void executeAiPlayCommand(const GameCommand& command);
    void handleAutoPassTimeout();
    bool handleKeyPress(QKeyEvent* event);
    bool handleWindowsMessage(void* message, qintptr* result);
    bool handleNativeShortcut(unsigned int virtualKey, bool ctrlDown, bool shiftDown,
                              bool altDown, unsigned int scanCode);
    void traceKeyboardEvent(const QString& source, unsigned int key, unsigned int scanCode,
                            bool ctrlDown, bool shiftDown, bool altDown,
                            bool keyDown, bool autoRepeat = false);
    QJsonObject handTraceState() const;
    void traceHandAction(const QString& action, const QJsonObject& before,
                         const QJsonObject& details = {});
    void requestApplicationExit();
    void requestReturnToModeSelection();
    void installKeyboardHook();
    void uninstallKeyboardHook();
    void registerSystemHotkeys();
    void unregisterSystemHotkeys();
    void moveHandCursorTo(int row);
    void suppressHandAccessibilityUntilUserAction();
    void resumeHandAccessibilityForUserAction();
    void announceCurrentCard();
    void takeCurrentCard();
    void takeCurrentGroup();
    void putDownNextPickedCard();
    void putDownAllCards();
    void syncPickedCardsToView();
    void refreshVisualCardTable();
    void triggerBattleShortcut();
    bool triggerBottomCardsShortcut(const QString& source);
    void toggleBattleState();
    void announcePlayerAtPosition(int position);
    void announceBottomCards();
    int announceNewDealBottomCards(const CommandResult& result,
                                   int initialDelayMilliseconds = 0);
    void announceScore();
    void announceCurrentTurn();
    void announceLastAction();
    void copyDiagnosticInformation();
    void copyAiBattleGames();
    QString buildDiagnosticReport() const;
    void applyPlayerDisplayNamesToState();
    std::wstring playerDisplayName(PlayerId playerId) const;
    std::wstring playedCardsPlayerDisplayName(PlayerId playerId) const;
    std::wstring formatEventForAnnouncement(const GameEvent& event) const;
    void presentPlayedCards(const GameEvent& event);
    void handleSuccessfulPlayResult(const CommandResult& result, bool appendYourTurn);
    bool playResultStillCurrent(uint64_t gameId, uint64_t eventSequence,
                                GamePhase phase, PlayerId currentPlayer) const;
    void rememberLastAction(const CommandResult& result);
    void handleFinishedResult(const CommandResult& result, int announcementDelayMilliseconds = 0);
    void showRoundResult();
    void returnToMainScreen();
    bool landlordMustLeadFirstTurn() const;
    void updateTurnCountdown(bool humanTurn);
    int playSoundsForResult(const CommandResult& result, bool appendYourTurn = false);
    void playSound(SoundId id);
    void playSoundFile(const QString& relativePath, SoundCategory category);
    void updateBackgroundMusic();
    void announce(const std::wstring& text, AnnouncementCategory category,
                  AnnouncementPriority priority = AnnouncementPriority::Normal,
                  bool updateStatus = true);
    GameEngine& m_engine;
    AccessibilityService& m_accessibility;
    DiagnosticTraceService* m_diagnosticTrace = nullptr;
    GameMode m_gameMode = GameMode::Offline;
    std::unique_ptr<AiServiceClient> m_aiService;
    std::unique_ptr<SettingsRepository> m_aiBattleSettingsRepo;
    std::unique_ptr<AiBattleStatisticsRepository> m_aiBattleStatisticsRepo;
    AiBattleSettings m_aiBattleSettings;
    std::optional<AiDecisionRequest> m_pendingCloudRequest;
    QElapsedTimer m_cloudWaitElapsed;
    bool m_cloudTurnPaused = false;
    std::unique_ptr<HandListModel> m_handModel;
    std::unique_ptr<PlayerStatusModel> m_playerModel;
    std::unique_ptr<SettingsRepository> m_settingsRepo;
    std::unique_ptr<StatisticsRepository> m_statisticsRepo;
    std::unique_ptr<SoundService> m_sound;
    AppSettings m_settings;
    QListView* m_handView = nullptr;
    CardTableWidget* m_cardTable = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_gameInfoLabel = nullptr;
    QLabel* m_bottomCardsLabel = nullptr;
    QLabel* m_countdownLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_passButton = nullptr;
    QPushButton* m_hintButton = nullptr;
    QWidget* m_biddingPanel = nullptr;
    QVector<QPushButton*> m_bidButtons;
    QTimer* m_aiTimer = nullptr;
    QTimer* m_turnCountdownTimer = nullptr;
    int m_turnSecondsRemaining = 0;
    bool m_wasHumanTurn = false;
    bool m_biddingControlsVisible = false;
    bool m_forceExitRequested = false;
    QElapsedTimer m_battleShortcutTimer;
    QVector<int> m_registeredHotkeys;
    QVector<QShortcut*> m_applicationShortcuts;
    QAction* m_battleAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_retryCloudAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QMenu* m_gameMenu = nullptr;
    QMenu* m_settingsMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    int m_topLevelMenuFocusIndex = -1;
    bool m_plainAltMenuCandidate = false;
    bool m_handAccessibilitySuppressedUntilUserAction = false;
    ResultDialog* m_resultDialog = nullptr;
    std::wstring m_lastActionText;
    std::wstring m_lastPlayedCardsText;
    uint64_t m_bottomCardsAnnouncedGameId = 0;
    QString m_lastSpeechDeliveryTime;
    QString m_lastSpeechDeliveryChannel;
    QString m_lastSpeechDeliveryResult;
    QString m_lastBottomCardsQueryTime;
};

std::unique_ptr<MainWindow> createMainWindow(GameEngine& engine,
                                             AccessibilityService& accessibility,
                                             DiagnosticTraceService* diagnosticTrace = nullptr,
                                             GameMode gameMode = GameMode::Offline);
} // namespace fpdz
