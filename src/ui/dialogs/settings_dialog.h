#pragma once

#include "../../app/app_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>

namespace fpdz {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings& settings, QWidget* parent = nullptr);

    AppSettings settings() const;

private:
    AppSettings m_settings;
    QCheckBox* m_autoPassEnabled = nullptr;
    QSpinBox* m_autoPassSeconds = nullptr;
    QComboBox* m_aiDifficulty = nullptr;
    QCheckBox* m_soundEnabled = nullptr;
    QSpinBox* m_soundVolume = nullptr;
    QComboBox* m_humanVoice = nullptr;
    QCheckBox* m_backgroundMusicEnabled = nullptr;
    QSpinBox* m_backgroundMusicVolume = nullptr;
    QComboBox* m_backgroundMusicMode = nullptr;
    QCheckBox* m_automaticUpdateChecks = nullptr;
};

} // namespace fpdz
