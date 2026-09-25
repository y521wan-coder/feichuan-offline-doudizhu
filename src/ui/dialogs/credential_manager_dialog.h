#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QSet>

class QListWidget;
class QLabel;

namespace fpdz {

class AiServiceClient;

class CredentialManagerDialog final : public QDialog {
    Q_OBJECT
public:
    explicit CredentialManagerDialog(AiServiceClient& client,
                                     QSet<QString> protectedCredentialIds = {},
                                     QWidget* parent = nullptr);
    QJsonArray credentials() const { return m_credentials; }

private:
    void reload();
    void refreshList();
    void addCredential();
    void editCredential();
    void deleteCredential();
    void fetchModels();
    bool editRecord(QJsonObject record, bool isNew);
    int currentIndex() const;
    void announceStatus(const QString& text);

    AiServiceClient& m_client;
    QJsonArray m_credentials;
    QSet<QString> m_protectedCredentialIds;
    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace fpdz
