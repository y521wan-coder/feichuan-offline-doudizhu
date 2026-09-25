#pragma once

#include "../../app/ai_battle_types.h"

#include <QDialog>
#include <QJsonArray>

class QComboBox;
class QCheckBox;
class QGroupBox;
class QPlainTextEdit;
class QSpinBox;

namespace fpdz {

class AiServiceClient;

class AiBattleSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    AiBattleSettingsDialog(const AiBattleSettings& settings, AiServiceClient& client,
                           bool gameInProgress, QWidget* parent = nullptr);
    AiBattleSettings settings() const;

private:
    struct CloudWidgets {
        QGroupBox* group = nullptr;
        QComboBox* credential = nullptr;
        QComboBox* model = nullptr;
        QComboBox* timeout = nullptr;
    };
    void loadCredentials();
    void refreshCredentials();
    void updateCloudControls();
    void manageCredentials();
    void testSelectedModel();
    QJsonObject credentialById(const QString& id) const;

    AiServiceClient& m_client;
    AiBattleSettings m_initial;
    bool m_gameInProgress = false;
    QJsonArray m_credentials;
    QComboBox* m_playerCount = nullptr;
    QCheckBox* m_autoPassEnabled = nullptr;
    QSpinBox* m_autoPassSeconds = nullptr;
    QCheckBox* m_landlordMustLeadFirstTurn = nullptr;
    CloudWidgets m_cloud;
    QPlainTextEdit* m_strategyPrompt = nullptr;
};

} // namespace fpdz
