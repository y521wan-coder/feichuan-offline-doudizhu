#include "ai_battle_settings_dialog.h"

#include "credential_manager_dialog.h"
#include "../../app/ai_service_client.h"
#include "../../app/ai_battle_prompt.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace fpdz {

AiBattleSettingsDialog::AiBattleSettingsDialog(const AiBattleSettings& settings,
                                               AiServiceClient& client,
                                               bool gameInProgress, QWidget* parent)
    : QDialog(parent), m_client(client), m_initial(settings),
      m_gameInProgress(gameInProgress) {
    setWindowTitle(QString::fromUtf8(u8"AI对战设置"));
    setAccessibleName(windowTitle());
    resize(680, 620);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_playerCount = new QComboBox(this);
    m_playerCount->setObjectName(QStringLiteral("aiBattlePlayerCountCombo"));
    m_playerCount->addItem(QString::fromUtf8(u8"二人（一副牌）"), TWO_PLAYER_COUNT);
    m_playerCount->addItem(QString::fromUtf8(u8"三人（一副牌）"), THREE_PLAYER_COUNT);
    m_playerCount->addItem(QString::fromUtf8(u8"四人（两副牌）"), PLAYER_COUNT);
    m_playerCount->setCurrentIndex(std::max(0, m_playerCount->findData(settings.playerCount)));
    m_playerCount->setEnabled(!gameInProgress);
    form->addRow(QString::fromUtf8(u8"游戏人数"), m_playerCount);
    auto* human = new QLabel(QString::fromUtf8(u8"1：真人（固定）"), this);
    human->setAccessibleName(QString::fromUtf8(u8"1固定为真人"));
    form->addRow(human);
    m_autoPassEnabled = new QCheckBox(QString::fromUtf8(u8"轮到我时超时自动过牌"), this);
    m_autoPassEnabled->setObjectName(QStringLiteral("aiBattleAutoPassEnabledCheckBox"));
    m_autoPassEnabled->setChecked(settings.autoPassEnabled);
    form->addRow(QString::fromUtf8(u8"自动过牌"), m_autoPassEnabled);
    m_autoPassSeconds = new QSpinBox(this);
    m_autoPassSeconds->setObjectName(QStringLiteral("aiBattleAutoPassSecondsSpinBox"));
    m_autoPassSeconds->setRange(3, 1800);
    m_autoPassSeconds->setSingleStep(1);
    m_autoPassSeconds->setAccelerated(true);
    m_autoPassSeconds->setSuffix(QString::fromUtf8(u8" 秒"));
    m_autoPassSeconds->setValue(settings.autoPassSeconds);
    m_autoPassSeconds->setAccessibleName(QString::fromUtf8(u8"真人回合等待时间"));
    m_autoPassSeconds->setAccessibleDescription(QString::fromUtf8(
        u8"用上下光标调整，最长1800秒，即30分钟"));
    form->addRow(QString::fromUtf8(u8"真人等待时间"), m_autoPassSeconds);
    m_landlordMustLeadFirstTurn = new QCheckBox(
        QString::fromUtf8(u8"地主首轮必须出牌（不可过牌）"), this);
    m_landlordMustLeadFirstTurn->setObjectName(
        QStringLiteral("aiBattleLandlordMustLeadFirstTurnCheckBox"));
    m_landlordMustLeadFirstTurn->setChecked(settings.landlordMustLeadFirstTurn);
    form->addRow(QString::fromUtf8(u8"地主首轮"), m_landlordMustLeadFirstTurn);
    root->addLayout(form);

    const auto& shared = settings.seats[1];
    m_cloud.group = new QGroupBox(
        QString::fromUtf8(u8"云模型（2及之后的电脑座位共用这一个）"), this);
    m_cloud.group->setObjectName(QStringLiteral("aiBattleCloudGroup"));
    m_cloud.group->setAccessibleDescription(QString::fromUtf8(
        u8"所有电脑座位共用同一个认证、模型和最大等待时间"));
    auto* cloudForm = new QFormLayout(m_cloud.group);
    m_cloud.credential = new QComboBox(m_cloud.group);
    m_cloud.credential->setObjectName(QStringLiteral("aiBattleSharedCredentialCombo"));
    m_cloud.model = new QComboBox(m_cloud.group);
    m_cloud.model->setEditable(true);
    m_cloud.model->setObjectName(QStringLiteral("aiBattleSharedModelCombo"));
    m_cloud.model->setAccessibleDescription(QString::fromUtf8(u8"可从列表选择或手工输入模型标识"));
    m_cloud.timeout = new QComboBox(m_cloud.group);
    m_cloud.timeout->setObjectName(QStringLiteral("aiBattleSharedTimeoutCombo"));
    m_cloud.timeout->setEditable(true);
    m_cloud.timeout->setInsertPolicy(QComboBox::NoInsert);
    m_cloud.timeout->setValidator(new QIntValidator(1, 180, m_cloud.timeout));
    for (const int seconds : {1, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120, 150, 180}) {
        m_cloud.timeout->addItem(QString::number(seconds), seconds);
    }
    m_cloud.timeout->setCurrentText(QString::number(shared.timeoutSeconds));
    m_cloud.timeout->setAccessibleDescription(QString::fromUtf8(
        u8"上下方向键选择常用秒数，也可以输入1到180秒"));
    cloudForm->addRow(QString::fromUtf8(u8"云认证"), m_cloud.credential);
    cloudForm->addRow(QString::fromUtf8(u8"模型"), m_cloud.model);
    cloudForm->addRow(QString::fromUtf8(u8"最大等待（秒）"), m_cloud.timeout);
    connect(m_cloud.credential, &QComboBox::currentIndexChanged, this,
            [this]() { updateCloudControls(); });
    root->addWidget(m_cloud.group);

    auto* promptGroup = new QGroupBox(QString::fromUtf8(u8"每轮AI策略提示词"), this);
    auto* promptLayout = new QVBoxLayout(promptGroup);
    auto* promptHelp = new QLabel(QString::fromUtf8(
        u8"所有电脑座位、叫分和出牌回合共用。规则、牌面数据和合法动作由程序另附；修改后从下一次模型请求生效。"), promptGroup);
    promptHelp->setWordWrap(true);
    promptLayout->addWidget(promptHelp);
    m_strategyPrompt = new QPlainTextEdit(promptGroup);
    m_strategyPrompt->setObjectName(QStringLiteral("aiBattleStrategyPromptEdit"));
    m_strategyPrompt->setTabChangesFocus(true);
    m_strategyPrompt->setPlainText(settings.strategyPrompt.isEmpty()
        ? defaultAiStrategyPrompt() : settings.strategyPrompt);
    m_strategyPrompt->setAccessibleName(QString::fromUtf8(u8"每轮AI策略提示词"));
    m_strategyPrompt->setAccessibleDescription(QString::fromUtf8(
        u8"编辑每轮发送给云模型的策略文字，最多8192字；不要填写密钥"));
    m_strategyPrompt->setMinimumHeight(130);
    promptLayout->addWidget(m_strategyPrompt);
    auto* resetPrompt = new QPushButton(QString::fromUtf8(u8"恢复默认提示词"), promptGroup);
    resetPrompt->setObjectName(QStringLiteral("resetAiBattleStrategyPromptButton"));
    connect(resetPrompt, &QPushButton::clicked, this, [this]() {
        m_strategyPrompt->setPlainText(defaultAiStrategyPrompt());
    });
    promptLayout->addWidget(resetPrompt);
    root->addWidget(promptGroup);

    auto* manage = new QPushButton(QString::fromUtf8(u8"API认证管理"), this);
    manage->setObjectName(QStringLiteral("manageCredentialsButton"));
    connect(manage, &QPushButton::clicked, this, &AiBattleSettingsDialog::manageCredentials);
    root->addWidget(manage);
    auto* testModel = new QPushButton(QString::fromUtf8(u8"测试所选模型"), this);
    testModel->setObjectName(QStringLiteral("testSelectedModelButton"));
    connect(testModel, &QPushButton::clicked,
            this, &AiBattleSettingsDialog::testSelectedModel);
    root->addWidget(testModel);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8(u8"保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8(u8"取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        bool timeoutValid = false;
        const int timeout = m_cloud.timeout->currentText().trimmed().toInt(&timeoutValid);
        if (!timeoutValid || timeout < 1 || timeout > 180) {
            QMessageBox::warning(this, QString::fromUtf8(u8"无法保存AI对战设置"),
                                 QString::fromUtf8(u8"最大等待时间须为1到180秒"));
            return;
        }
        if (m_strategyPrompt->toPlainText().size() > AI_MAX_STRATEGY_PROMPT_CHARS) {
            QMessageBox::warning(this, QString::fromUtf8(u8"无法保存AI对战设置"),
                                 QString::fromUtf8(u8"策略提示词最多8192字"));
            return;
        }
        QString reason;
        if (!this->settings().validForStart(&reason)) {
            QMessageBox::warning(this, QString::fromUtf8(u8"无法保存AI对战设置"), reason);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    loadCredentials();
}

AiBattleSettings AiBattleSettingsDialog::settings() const {
    AiBattleSettings result = m_initial;
    result.playerCount = m_playerCount->currentData().toInt();
    result.autoPassEnabled = m_autoPassEnabled->isChecked();
    result.autoPassSeconds = m_autoPassSeconds->value();
    result.landlordMustLeadFirstTurn = m_landlordMustLeadFirstTurn->isChecked();
    result.strategyPrompt = m_strategyPrompt->toPlainText().trimmed();
    SeatControllerConfig seat;
    seat.kind = SeatControllerKind::CloudAi;
    seat.credentialId = m_cloud.credential->currentData().toString();
    seat.credentialName = m_cloud.credential->currentText();
    seat.model = m_cloud.model->currentText().trimmed();
    seat.strength = CloudStrength::Fast;
    seat.timeoutSeconds = m_cloud.timeout->currentText().trimmed().toInt();
    for (int index = 1; index < PLAYER_COUNT; ++index) {
        result.seats[static_cast<std::size_t>(index)] = seat;
    }
    result.normalize();
    return result;
}

void AiBattleSettingsDialog::loadCredentials() {
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credentials_list")}});
    m_credentials = response.value("credentials").toArray();
    refreshCredentials();
}

void AiBattleSettingsDialog::refreshCredentials() {
    const QString selected = m_initial.seats[1].credentialId.isEmpty()
        ? m_cloud.credential->currentData().toString() : m_initial.seats[1].credentialId;
    m_cloud.credential->clear();
    for (const auto& value : m_credentials) {
        const auto record = value.toObject();
        m_cloud.credential->addItem(record.value("name").toString(), record.value("id"));
    }
    const int selectedIndex = m_cloud.credential->findData(selected);
    if (selectedIndex >= 0) m_cloud.credential->setCurrentIndex(selectedIndex);
    updateCloudControls();
}

void AiBattleSettingsDialog::updateCloudControls() {
    const QString currentModel = m_cloud.model->currentText().isEmpty()
        ? m_initial.seats[1].model : m_cloud.model->currentText();
    m_cloud.model->clear();
    const auto record = credentialById(m_cloud.credential->currentData().toString());
    for (const auto& model : record.value("models").toArray()) {
        m_cloud.model->addItem(model.toString());
    }
    const QString manual = record.value("manual_model").toString();
    if (!manual.isEmpty() && m_cloud.model->findText(manual) < 0) {
        m_cloud.model->addItem(manual);
    }
    m_cloud.model->setCurrentText(currentModel.isEmpty() ? manual : currentModel);
    m_cloud.model->setAccessibleName(QString::fromUtf8(u8"模型，当前%1，共%2项")
        .arg(m_cloud.model->currentText()).arg(m_cloud.model->count()));
}

void AiBattleSettingsDialog::manageCredentials() {
    QSet<QString> protectedIds;
    if (m_gameInProgress) {
        const AiBattleSettings current = settings();
        if (!current.seats[1].credentialId.isEmpty()) {
            protectedIds.insert(current.seats[1].credentialId);
        }
    }
    CredentialManagerDialog dialog(m_client, protectedIds, this);
    dialog.exec();
    m_credentials = dialog.credentials();
    refreshCredentials();
}

void AiBattleSettingsDialog::testSelectedModel() {
    const SeatControllerConfig seat = settings().seats[1];
    if (seat.credentialId.isEmpty() || seat.model.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8(u8"模型测试"),
                                 QString::fromUtf8(u8"请先选择云认证和模型"));
        return;
    }
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("test_model")},
        {QStringLiteral("credential_id"), seat.credentialId},
        {QStringLiteral("model"), seat.model},
        {QStringLiteral("strength"), cloudStrengthApiValue(seat.strength)},
        {QStringLiteral("timeout_ms"), seat.timeoutSeconds * 1000}},
        seat.timeoutSeconds * 1000 + 2000);
    QMessageBox::information(this, QString::fromUtf8(u8"模型测试"),
        response.value("ok").toBool()
            ? QString::fromUtf8(u8"所选模型测试成功")
            : QString::fromUtf8(u8"所选模型测试失败：") +
                response.value("message").toString());
}

QJsonObject AiBattleSettingsDialog::credentialById(const QString& id) const {
    for (const auto& value : m_credentials) {
        if (value.toObject().value("id").toString() == id) return value.toObject();
    }
    return {};
}

} // namespace fpdz
