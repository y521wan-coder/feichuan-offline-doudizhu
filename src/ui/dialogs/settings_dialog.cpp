#include "settings_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>

namespace fpdz {

SettingsDialog::SettingsDialog(const AppSettings& settings, QWidget* parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(QString::fromStdWString(L"设置"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_playerCount = new QComboBox(this);
    m_playerCount->setObjectName(QStringLiteral("playerCountComboBox"));
    m_playerCount->addItem(QString::fromUtf8(u8"四人（两副牌）"), PLAYER_COUNT);
    m_playerCount->addItem(QString::fromUtf8(u8"三人（一副牌）"), THREE_PLAYER_COUNT);
    m_playerCount->addItem(QString::fromUtf8(u8"二人（一副牌）"), TWO_PLAYER_COUNT);
    const int playerCountIndex = m_playerCount->findData(settings.playerCount);
    m_playerCount->setCurrentIndex(playerCountIndex >= 0 ? playerCountIndex : 0);
    m_playerCount->setAccessibleName(QString::fromUtf8(u8"游戏人数"));
    form->addRow(QString::fromUtf8(u8"游戏人数"), m_playerCount);

    m_autoPassEnabled = new QCheckBox(QString::fromStdWString(L"轮到我时超时自动过牌"), this);
    m_autoPassEnabled->setChecked(settings.autoPassEnabled);
    form->addRow(QString::fromStdWString(L"自动过牌"), m_autoPassEnabled);

    m_autoPassSeconds = new QSpinBox(this);
    m_autoPassSeconds->setObjectName(QStringLiteral("autoPassSecondsSpinBox"));
    m_autoPassSeconds->setRange(3, 1800);
    m_autoPassSeconds->setSingleStep(1);
    m_autoPassSeconds->setAccelerated(true);
    m_autoPassSeconds->setSuffix(QString::fromStdWString(L" 秒"));
    m_autoPassSeconds->setValue(settings.autoPassSeconds);
    form->addRow(QString::fromStdWString(L"等待时间"), m_autoPassSeconds);

    m_landlordMustLeadFirstTurn = new QCheckBox(
        QString::fromUtf8(u8"地主首轮必须出牌（不可过牌）"), this);
    m_landlordMustLeadFirstTurn->setObjectName(
        QStringLiteral("landlordMustLeadFirstTurnCheckBox"));
    m_landlordMustLeadFirstTurn->setChecked(settings.landlordMustLeadFirstTurn);
    form->addRow(QString::fromUtf8(u8"地主首轮"), m_landlordMustLeadFirstTurn);

    m_aiDifficulty = new QComboBox(this);
    m_aiDifficulty->setObjectName(QStringLiteral("aiModeComboBox"));
    m_aiDifficulty->addItem(QString::fromStdWString(L"初级"), static_cast<int>(AiDifficulty::Beginner));
    m_aiDifficulty->addItem(QString::fromStdWString(L"中级"), static_cast<int>(AiDifficulty::Intermediate));
    m_aiDifficulty->addItem(QString::fromStdWString(L"高级"), static_cast<int>(AiDifficulty::Advanced));
    const int difficultyIndex = m_aiDifficulty->findData(settings.aiDifficulty);
    m_aiDifficulty->setCurrentIndex(difficultyIndex >= 0 ? difficultyIndex : 0);
    form->addRow(QString::fromStdWString(L"机器人模式"), m_aiDifficulty);

    m_soundEnabled = new QCheckBox(QString::fromUtf8(u8"启用游戏音效"), this);
    m_soundEnabled->setChecked(settings.soundEnabled);
    form->addRow(QString::fromUtf8(u8"游戏音效"), m_soundEnabled);

    m_soundVolume = new QSpinBox(this);
    m_soundVolume->setObjectName(QStringLiteral("soundVolumeSpinBox"));
    m_soundVolume->setRange(0, 100);
    m_soundVolume->setSingleStep(1);
    m_soundVolume->setSuffix(QStringLiteral("%"));
    m_soundVolume->setValue(settings.soundVolume);
    form->addRow(QString::fromUtf8(u8"音效音量"), m_soundVolume);

    m_humanVoice = new QComboBox(this);
    m_humanVoice->setObjectName(QStringLiteral("humanVoiceComboBox"));
    m_humanVoice->addItem(QString::fromUtf8(u8"男声"), static_cast<int>(PlayerVoice::Male));
    m_humanVoice->addItem(QString::fromUtf8(u8"女声"), static_cast<int>(PlayerVoice::Female));
    m_humanVoice->setCurrentIndex(std::max(0, m_humanVoice->findData(
        static_cast<int>(settings.humanVoice))));
    form->addRow(QString::fromUtf8(u8"我的出牌声线"), m_humanVoice);

    m_backgroundMusicEnabled = new QCheckBox(QString::fromUtf8(u8"播放背景音乐"), this);
    m_backgroundMusicEnabled->setChecked(settings.backgroundMusicEnabled);
    form->addRow(QString::fromUtf8(u8"背景音乐"), m_backgroundMusicEnabled);

    m_backgroundMusicVolume = new QSpinBox(this);
    m_backgroundMusicVolume->setObjectName(QStringLiteral("backgroundMusicVolumeSpinBox"));
    m_backgroundMusicVolume->setRange(0, 100);
    m_backgroundMusicVolume->setSingleStep(1);
    m_backgroundMusicVolume->setSuffix(QStringLiteral("%"));
    m_backgroundMusicVolume->setValue(settings.backgroundMusicVolume);
    form->addRow(QString::fromUtf8(u8"音乐音量"), m_backgroundMusicVolume);

    m_backgroundMusicMode = new QComboBox(this);
    m_backgroundMusicMode->setObjectName(QStringLiteral("backgroundMusicModeComboBox"));
    m_backgroundMusicMode->addItem(QString::fromUtf8(u8"自动切换"),
                                   static_cast<int>(BackgroundMusicMode::Automatic));
    m_backgroundMusicMode->addItem(QString::fromUtf8(u8"背景音乐"),
                                   static_cast<int>(BackgroundMusicMode::Background));
    m_backgroundMusicMode->addItem(QString::fromUtf8(u8"普通"),
                                   static_cast<int>(BackgroundMusicMode::Normal));
    m_backgroundMusicMode->addItem(QString::fromUtf8(u8"激昂"),
                                   static_cast<int>(BackgroundMusicMode::Intense));
    m_backgroundMusicMode->setCurrentIndex(std::max(0, m_backgroundMusicMode->findData(
        static_cast<int>(settings.backgroundMusicMode))));
    form->addRow(QString::fromUtf8(u8"音乐模式"), m_backgroundMusicMode);

    m_automaticUpdateChecks = new QCheckBox(
        QString::fromUtf8(u8"每天启动软件时自动检测一次更新"), this);
    m_automaticUpdateChecks->setObjectName(QStringLiteral("automaticUpdateChecksCheckBox"));
    m_automaticUpdateChecks->setChecked(settings.automaticUpdateChecks);
    form->addRow(QString::fromUtf8(u8"软件更新"), m_automaticUpdateChecks);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

AppSettings SettingsDialog::settings() const {
    AppSettings settings = m_settings;
    settings.playerCount = m_playerCount->currentData().toInt();
    settings.autoPassEnabled = m_autoPassEnabled->isChecked();
    settings.autoPassSeconds = m_autoPassSeconds->value();
    settings.landlordMustLeadFirstTurn = m_landlordMustLeadFirstTurn->isChecked();
    const int selectedMode = m_aiDifficulty->currentData().toInt();
    settings.soundEnabled = m_soundEnabled->isChecked();
    settings.soundVolume = m_soundVolume->value();
    settings.humanVoice = static_cast<PlayerVoice>(m_humanVoice->currentData().toInt());
    settings.backgroundMusicEnabled = m_backgroundMusicEnabled->isChecked();
    settings.setSoundCategoryEnabled(SoundCategory::BackgroundMusic,
                                     settings.backgroundMusicEnabled);
    settings.backgroundMusicVolume = m_backgroundMusicVolume->value();
    settings.backgroundMusicMode = static_cast<BackgroundMusicMode>(
        m_backgroundMusicMode->currentData().toInt());
    settings.automaticUpdateChecks = m_automaticUpdateChecks->isChecked();
    settings.aiDifficulty = selectedMode;
    settings.normalize();
    return settings;
}

} // namespace fpdz
