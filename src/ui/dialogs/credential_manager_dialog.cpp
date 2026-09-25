#include "credential_manager_dialog.h"

#include "../../app/ai_service_client.h"
#include "../../app/ai_battle_types.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace fpdz {

CredentialManagerDialog::CredentialManagerDialog(AiServiceClient& client,
                                                 QSet<QString> protectedCredentialIds,
                                                 QWidget* parent)
    : QDialog(parent), m_client(client),
      m_protectedCredentialIds(std::move(protectedCredentialIds)) {
    setWindowTitle(QString::fromUtf8(u8"API认证管理"));
    setAccessibleName(windowTitle());
    resize(720, 460);
    auto* layout = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("credentialList"));
    m_list->setAccessibleName(QString::fromUtf8(u8"API认证列表"));
    layout->addWidget(m_list);

    auto* row = new QHBoxLayout;
    auto addButton = [this, row](const QString& text, const QString& name,
                                 const QKeySequence& shortcut, auto slot) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(name);
        button->setShortcut(shortcut);
        connect(button, &QPushButton::clicked, this, slot);
        row->addWidget(button);
    };
    addButton(QString::fromUtf8(u8"新增(&N)"), QStringLiteral("addCredentialButton"),
              QKeySequence(QStringLiteral("Alt+N")), [this]() { addCredential(); });
    addButton(QString::fromUtf8(u8"编辑(&E)"), QStringLiteral("editCredentialButton"),
              QKeySequence(QStringLiteral("Alt+E")), [this]() { editCredential(); });
    addButton(QString::fromUtf8(u8"删除(&D)"), QStringLiteral("deleteCredentialButton"),
              QKeySequence(QStringLiteral("Alt+D")), [this]() { deleteCredential(); });
    addButton(QString::fromUtf8(u8"获取模型(&F)"), QStringLiteral("fetchModelsButton"),
              QKeySequence(QStringLiteral("Alt+F")), [this]() { fetchModels(); });
    layout->addLayout(row);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("credentialStatus"));
    m_status->setAccessibleName(QString::fromUtf8(u8"认证状态"));
    layout->addWidget(m_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QString::fromUtf8(u8"关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    reload();
}

void CredentialManagerDialog::reload() {
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credentials_list")}});
    if (!response.value("ok").toBool()) {
        announceStatus(response.value("message").toString(QString::fromUtf8(u8"获取认证失败")));
        return;
    }
    m_credentials = response.value("credentials").toArray();
    refreshList();
}

void CredentialManagerDialog::refreshList() {
    const int previous = currentIndex();
    m_list->clear();
    for (int index = 0; index < m_credentials.size(); ++index) {
        const auto record = m_credentials[index].toObject();
        const QString text = record.value("name").toString() + QString::fromUtf8(u8"，") +
            record.value("request_url").toString();
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, record.value("id").toString());
        item->setData(Qt::AccessibleTextRole,
            QString::fromUtf8(u8"%1，第%2项，共%3项")
                .arg(record.value("name").toString()).arg(index + 1).arg(m_credentials.size()));
    }
    if (m_list->count() > 0) m_list->setCurrentRow(std::clamp(previous, 0, m_list->count() - 1));
    announceStatus(QString::fromUtf8(u8"共有%1个认证").arg(m_credentials.size()));
}

void CredentialManagerDialog::addCredential() {
    QJsonObject record{{QStringLiteral("protocol"), 0},
                       {QStringLiteral("auth_method"), 0},
                       {QStringLiteral("request_url"), QStringLiteral("https://api.openai.com/v1/responses")}};
    if (editRecord(record, true)) reload();
}

void CredentialManagerDialog::editCredential() {
    const int index = currentIndex();
    if (index < 0) return;
    if (editRecord(m_credentials[index].toObject(), false)) reload();
}

void CredentialManagerDialog::deleteCredential() {
    const int index = currentIndex();
    if (index < 0) return;
    const auto record = m_credentials[index].toObject();
    if (m_protectedCredentialIds.contains(record.value("id").toString())) {
        QMessageBox::information(this, QString::fromUtf8(u8"不能删除认证"),
                                 QString::fromUtf8(u8"该认证正在被当前牌局使用"));
        return;
    }
    if (QMessageBox::question(this, QString::fromUtf8(u8"删除认证"),
        QString::fromUtf8(u8"确认删除“%1”吗？").arg(record.value("name").toString()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credential_delete")},
        {QStringLiteral("credential_id"), record.value("id")}});
    announceStatus(response.value("ok").toBool() ? QString::fromUtf8(u8"认证已删除")
        : response.value("message").toString(QString::fromUtf8(u8"删除失败")));
    reload();
}

void CredentialManagerDialog::fetchModels() {
    const int index = currentIndex();
    if (index < 0) return;
    announceStatus(QString::fromUtf8(u8"正在获取模型"));
    const auto record = m_credentials[index].toObject();
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("fetch_models")},
        {QStringLiteral("credential_id"), record.value("id")},
        {QStringLiteral("timeout_ms"), 15000}}, 17000);
    if (!response.value("ok").toBool()) {
        announceStatus(QString::fromUtf8(u8"获取模型失败：") + response.value("message").toString());
        return;
    }
    QJsonObject updated = record;
    updated[QStringLiteral("models")] = response.value("models").toArray();
    const auto saved = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credential_save")},
        {QStringLiteral("credential"), updated}});
    if (saved.value("ok").toBool()) {
        announceStatus(QString::fromUtf8(u8"获取模型成功，共%1个").arg(
            response.value("models").toArray().size()));
        reload();
    } else {
        announceStatus(saved.value("message").toString(QString::fromUtf8(u8"模型列表保存失败")));
    }
}

bool CredentialManagerDialog::editRecord(QJsonObject record, bool isNew) {
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? QString::fromUtf8(u8"新增API认证")
                                : QString::fromUtf8(u8"编辑API认证"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(record.value("name").toString(), &dialog);
    name->setObjectName(QStringLiteral("credentialNameEdit"));
    auto* protocol = new QComboBox(&dialog);
    protocol->setObjectName(QStringLiteral("apiProtocolCombo"));
    protocol->addItem(QString::fromUtf8(u8"OpenAI官方Responses API"), 0);
    protocol->addItem(QString::fromUtf8(u8"兼容Responses API"), 1);
    protocol->addItem(QString::fromUtf8(u8"兼容Chat Completions API"), 2);
    protocol->setCurrentIndex(std::max(0, protocol->findData(record.value("protocol").toInt())));
    auto* requestUrl = new QLineEdit(record.value("request_url").toString(), &dialog);
    requestUrl->setObjectName(QStringLiteral("requestUrlEdit"));
    auto* modelsUrl = new QLineEdit(record.value("models_url").toString(), &dialog);
    auto* auth = new QComboBox(&dialog);
    auth->setObjectName(QStringLiteral("authMethodCombo"));
    auth->addItem(QStringLiteral("Authorization: Bearer"), 0);
    auth->addItem(QStringLiteral("api-key"), 1);
    auth->addItem(QStringLiteral("x-api-key"), 2);
    auth->addItem(QString::fromUtf8(u8"自定义请求头"), 3);
    auth->addItem(QString::fromUtf8(u8"无需认证"), 4);
    auth->setCurrentIndex(std::max(0, auth->findData(record.value("auth_method").toInt())));
    auto* apiKey = new QLineEdit(&dialog);
    apiKey->setObjectName(QStringLiteral("apiKeyEdit"));
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setAccessibleName(record.value("has_secret").toBool()
        ? QString::fromUtf8(u8"API密钥，已填写，留空保持原值")
        : QString::fromUtf8(u8"API密钥，未填写"));
    apiKey->setAccessibleDescription(QString::fromUtf8(u8"内容不会被朗读或回显"));
    auto* clearSecret = new QCheckBox(QString::fromUtf8(u8"明确清除原密钥"), &dialog);
    auto* customName = new QLineEdit(record.value("custom_header_name").toString(), &dialog);
    auto* customValue = new QLineEdit(&dialog);
    customValue->setEchoMode(QLineEdit::Password);
    customValue->setAccessibleName(record.value("has_secret").toBool()
        ? QString::fromUtf8(u8"自定义请求头值，已填写，留空保持原值")
        : QString::fromUtf8(u8"自定义请求头值，未填写"));
    auto* manualModel = new QLineEdit(record.value("manual_model").toString(), &dialog);
    auto* reasoning = new QCheckBox(QString::fromUtf8(u8"发送标准推理强度参数"), &dialog);
    reasoning->setChecked(record.value("standard_reasoning_parameter").toBool());
    auto* structured = new QCheckBox(QString::fromUtf8(u8"启用结构化输出参数"), &dialog);
    structured->setChecked(record.value("structured_output").toBool());
    form->addRow(QString::fromUtf8(u8"名称"), name);
    form->addRow(QString::fromUtf8(u8"API类型"), protocol);
    form->addRow(QString::fromUtf8(u8"请求地址"), requestUrl);
    form->addRow(QString::fromUtf8(u8"模型列表地址（可选）"), modelsUrl);
    form->addRow(QString::fromUtf8(u8"认证方式"), auth);
    form->addRow(QString::fromUtf8(u8"API密钥"), apiKey);
    form->addRow(QString(), clearSecret);
    form->addRow(QString::fromUtf8(u8"自定义头名称"), customName);
    form->addRow(QString::fromUtf8(u8"自定义头值"), customValue);
    form->addRow(QString::fromUtf8(u8"手工模型名称"), manualModel);
    form->addRow(QString(), reasoning);
    form->addRow(QString(), structured);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8(u8"保存(&S)"));
    buttons->button(QDialogButtonBox::Save)->setShortcut(QKeySequence(QStringLiteral("Alt+S")));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8(u8"取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    connect(protocol, &QComboBox::currentIndexChanged, &dialog, [=](int) {
        const bool official = protocol->currentData().toInt() == 0;
        requestUrl->setEnabled(!official);
        modelsUrl->setEnabled(!official);
        if (official) {
            requestUrl->setText(QStringLiteral("https://api.openai.com/v1/responses"));
            modelsUrl->setText(QStringLiteral("https://api.openai.com/v1/models"));
            auth->setCurrentIndex(auth->findData(0));
        }
    });
    emit protocol->currentIndexChanged(protocol->currentIndex());
    if (dialog.exec() != QDialog::Accepted) return false;
    const QUrl url(requestUrl->text().trimmed());
    if (url.scheme() == QStringLiteral("http")) {
        if (QMessageBox::warning(this, QString::fromUtf8(u8"明文HTTP风险"),
            QString::fromUtf8(u8"HTTP会以明文传输API密钥和整桌暗牌。仍要保存吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    record[QStringLiteral("name")] = name->text().trimmed();
    record[QStringLiteral("protocol")] = protocol->currentData().toInt();
    record[QStringLiteral("request_url")] = requestUrl->text().trimmed();
    record[QStringLiteral("models_url")] = modelsUrl->text().trimmed();
    record[QStringLiteral("auth_method")] = auth->currentData().toInt();
    record[QStringLiteral("api_key")] = apiKey->text();
    record[QStringLiteral("custom_header_name")] = customName->text().trimmed();
    record[QStringLiteral("custom_header_value")] = customValue->text();
    record[QStringLiteral("manual_model")] = manualModel->text().trimmed();
    record[QStringLiteral("standard_reasoning_parameter")] = reasoning->isChecked();
    record[QStringLiteral("structured_output")] = structured->isChecked();
    const auto response = m_client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credential_save")},
        {QStringLiteral("credential"), record},
        {QStringLiteral("clear_secret"), clearSecret->isChecked()}});
    if (!response.value("ok").toBool()) {
        QMessageBox::warning(this, QString::fromUtf8(u8"保存失败"),
                             response.value("message").toString());
        return false;
    }
    announceStatus(QString::fromUtf8(u8"认证已保存"));
    return true;
}

int CredentialManagerDialog::currentIndex() const {
    return m_list ? m_list->currentRow() : -1;
}

void CredentialManagerDialog::announceStatus(const QString& text) {
    if (!m_status) return;
    m_status->setText(text);
    m_status->setAccessibleName(text);
}

} // namespace fpdz
