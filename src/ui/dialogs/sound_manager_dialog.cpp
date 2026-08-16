#include "sound_manager_dialog.h"

#include "../sound_service.h"

#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace fpdz {

SoundManagerDialog::SoundManagerDialog(AppSettings& settings,
                                       SoundService& soundService,
                                       bool fileChangesAllowed,
                                       QWidget* parent)
    : QDialog(parent),
      m_settings(settings),
      m_soundService(soundService),
      m_overrideManager(soundService),
      m_fileChangesAllowed(fileChangesAllowed) {
    setWindowTitle(QString::fromUtf8(u8"音效管理"));
    setAccessibleName(QString::fromUtf8(u8"音效管理"));
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName(QStringLiteral("soundCategoryComboBox"));
    m_categoryCombo->setAccessibleName(QString::fromUtf8(u8"音效分类"));
    form->addRow(QString::fromUtf8(u8"音效分类"), m_categoryCombo);
    m_categoryEnabled = new QCheckBox(QString::fromUtf8(u8"启用当前分类"), this);
    m_categoryEnabled->setObjectName(QStringLiteral("soundCategoryEnabledCheckBox"));
    form->addRow(QString::fromUtf8(u8"分类开关"), m_categoryEnabled);
    m_entryCombo = new QComboBox(this);
    m_entryCombo->setObjectName(QStringLiteral("soundEntryComboBox"));
    m_entryCombo->setAccessibleName(QString::fromUtf8(u8"具体音效"));
    form->addRow(QString::fromUtf8(u8"具体音效"), m_entryCombo);
    layout->addLayout(form);

    m_entryStatus = new QLabel(this);
    m_entryStatus->setObjectName(QStringLiteral("soundEntryStatusLabel"));
    m_entryStatus->setWordWrap(true);
    m_entryStatus->setTextInteractionFlags(Qt::TextSelectableByKeyboard);
    m_entryStatus->setAccessibleName(QString::fromUtf8(u8"当前音效状态"));
    layout->addWidget(m_entryStatus);

    m_previewButton = new QPushButton(QString::fromUtf8(u8"试听当前音效(&P)"), this);
    m_browseButton = new QPushButton(QString::fromUtf8(u8"浏览 WAV 文件(&B)"), this);
    m_pendingFileLabel = new QLabel(QString::fromUtf8(u8"尚未选择替换文件"), this);
    m_pendingFileLabel->setObjectName(QStringLiteral("pendingSoundFileLabel"));
    m_pendingFileLabel->setWordWrap(true);
    m_pendingFileLabel->setAccessibleName(QString::fromUtf8(u8"待替换文件"));
    m_confirmButton = new QPushButton(QString::fromUtf8(u8"确认替换(&R)"), this);
    m_confirmButton->setObjectName(QStringLiteral("confirmSoundReplacementButton"));
    m_restoreCurrentButton = new QPushButton(QString::fromUtf8(u8"恢复当前音效(&C)"), this);
    m_restoreCurrentButton->setObjectName(QStringLiteral("restoreCurrentSoundButton"));
    m_restoreCategoryButton = new QPushButton(QString::fromUtf8(u8"恢复当前分类(&G)"), this);
    m_restoreCategoryButton->setObjectName(QStringLiteral("restoreSoundCategoryButton"));
    m_importButton = new QPushButton(QString::fromUtf8(u8"导入音效包(&I)"), this);
    m_importButton->setObjectName(QStringLiteral("importSoundPackButton"));
    m_exportCurrentButton = new QPushButton(QString::fromUtf8(u8"导出当前完整音效包(&E)"), this);
    m_exportCurrentButton->setObjectName(QStringLiteral("exportCurrentSoundPackButton"));
    m_exportButton = new QPushButton(QString::fromUtf8(u8"导出音效包模板(&T)"), this);
    m_exportButton->setObjectName(QStringLiteral("exportSoundPackTemplateButton"));
    m_restoreAllButton = new QPushButton(QString::fromUtf8(u8"恢复全部默认音效(&A)"), this);
    m_restoreAllButton->setObjectName(QStringLiteral("restoreAllSoundsButton"));
    m_documentationButton = new QPushButton(QString::fromUtf8(u8"打开音效文件说明(&D)"), this);
    layout->addWidget(m_previewButton);
    layout->addWidget(m_browseButton);
    layout->addWidget(m_pendingFileLabel);
    layout->addWidget(m_confirmButton);
    layout->addWidget(m_restoreCurrentButton);
    layout->addWidget(m_restoreCategoryButton);
    layout->addWidget(m_importButton);
    layout->addWidget(m_exportCurrentButton);
    layout->addWidget(m_exportButton);
    layout->addWidget(m_restoreAllButton);
    layout->addWidget(m_documentationButton);

    if (!m_fileChangesAllowed) {
        auto* warning = new QLabel(QString::fromUtf8(
            u8"当前牌局尚未暂停。为避免打断牌型队列和倒计时，只能调整分类开关；试听、替换、恢复和导入将在暂停或结束牌局后可用。"), this);
        warning->setWordWrap(true);
        warning->setAccessibleName(QString::fromUtf8(u8"牌局进行中限制提示"));
        layout->addWidget(warning);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_musicPreviewTimer = new QTimer(this);
    m_musicPreviewTimer->setSingleShot(true);
    m_musicPreviewTimer->setInterval(5000);
    connect(m_musicPreviewTimer, &QTimer::timeout, this, [this]() {
        m_soundService.stopMusic();
        emit backgroundMusicRestoreRequested();
    });
    connect(m_categoryCombo, &QComboBox::currentIndexChanged,
            this, [this]() { populateEntries(); });
    connect(m_entryCombo, &QComboBox::currentIndexChanged,
            this, [this]() { refreshEntryStatus(); });
    connect(m_categoryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        const auto category = currentCategory();
        m_settings.setSoundCategoryEnabled(category, enabled);
        m_soundService.setCategoryEnabled(category, enabled);
        if (category == SoundCategory::BackgroundMusic) {
            m_soundService.setMusicEnabled(enabled);
            emit backgroundMusicRestoreRequested();
        }
        emit settingsChanged();
        setStatus(enabled ? QString::fromUtf8(u8"当前音效分类已开启")
                          : QString::fromUtf8(u8"当前音效分类已关闭"));
    });
    connect(m_previewButton, &QPushButton::clicked, this, &SoundManagerDialog::previewCurrent);
    connect(m_browseButton, &QPushButton::clicked, this, &SoundManagerDialog::browseReplacement);
    connect(m_confirmButton, &QPushButton::clicked, this, &SoundManagerDialog::confirmReplacement);
    connect(m_restoreCurrentButton, &QPushButton::clicked, this,
            &SoundManagerDialog::restoreCurrent);
    connect(m_restoreCategoryButton, &QPushButton::clicked, this,
            &SoundManagerDialog::restoreCurrentCategory);
    connect(m_importButton, &QPushButton::clicked, this, &SoundManagerDialog::importPack);
    connect(m_exportCurrentButton, &QPushButton::clicked, this,
            &SoundManagerDialog::exportCurrentPack);
    connect(m_exportButton, &QPushButton::clicked, this, &SoundManagerDialog::exportTemplate);
    connect(m_restoreAllButton, &QPushButton::clicked, this, &SoundManagerDialog::restoreAll);
    connect(m_documentationButton, &QPushButton::clicked, this,
            &SoundManagerDialog::openSoundDocumentationRequested);

    populateCategories();
    refreshFileActionAvailability();
}

SoundManagerDialog::~SoundManagerDialog() {
    if (m_musicPreviewTimer && m_musicPreviewTimer->isActive()) {
        m_musicPreviewTimer->stop();
        m_soundService.stopMusic();
    }
}

void SoundManagerDialog::populateCategories() {
    m_categoryCombo->clear();
    for (const auto category : soundCategories()) {
        m_categoryCombo->addItem(soundCategoryDisplayName(category), static_cast<int>(category));
    }
    populateEntries();
}

SoundCategory SoundManagerDialog::currentCategory() const {
    return static_cast<SoundCategory>(m_categoryCombo->currentData().toInt());
}

void SoundManagerDialog::populateEntries() {
    const QSignalBlocker blocker(m_categoryEnabled);
    m_categoryEnabled->setChecked(m_settings.isSoundCategoryEnabled(currentCategory()));
    m_entries = soundCatalogEntries(currentCategory());
    m_entryCombo->clear();
    for (const auto& entry : m_entries) m_entryCombo->addItem(entry.displayName, entry.id);
    m_pendingFile.clear();
    m_pendingFileLabel->setText(QString::fromUtf8(u8"尚未选择替换文件"));
    refreshEntryStatus();
}

const SoundCatalogEntry* SoundManagerDialog::currentEntry() const {
    const int index = m_entryCombo->currentIndex();
    if (index < 0 || index >= m_entries.size()) return nullptr;
    return &m_entries[index];
}

void SoundManagerDialog::refreshEntryStatus() {
    const auto* entry = currentEntry();
    if (!entry) {
        m_entryStatus->setText(QString::fromUtf8(u8"当前分类没有可替换音效"));
        refreshFileActionAvailability();
        return;
    }
    const bool custom = m_overrideManager.hasOverride(entry->relativePath);
    const QString path = custom ? m_overrideManager.overridePath(entry->relativePath)
                                : m_soundService.resolvedSoundPath(entry->relativePath);
    const QString separator = QStringLiteral(" | ");
    m_entryStatus->setText(
        (custom ? QString::fromUtf8(u8"正在使用自定义音效")
                : QString::fromUtf8(u8"正在使用软件默认音效")) + separator +
        QString::fromUtf8(u8"触发场景：") + entry->description + separator +
        QString::fromUtf8(u8"标准路径：") + entry->relativePath + separator +
        QString::fromUtf8(u8"当前文件：") + path);
    refreshFileActionAvailability();
}

void SoundManagerDialog::refreshFileActionAvailability() {
    const auto* entry = currentEntry();
    const bool hasEntry = entry != nullptr;
    const bool mutableEntry = m_fileChangesAllowed && hasEntry;
    m_previewButton->setEnabled(m_fileChangesAllowed && hasEntry);
    m_browseButton->setEnabled(mutableEntry);
    m_confirmButton->setEnabled(mutableEntry && !m_pendingFile.isEmpty());
    m_restoreCurrentButton->setEnabled(
        mutableEntry && m_overrideManager.hasOverride(entry ? entry->relativePath : QString()));
    m_restoreCategoryButton->setEnabled(m_fileChangesAllowed);
    m_importButton->setEnabled(m_fileChangesAllowed);
    m_exportCurrentButton->setEnabled(m_fileChangesAllowed);
    m_restoreAllButton->setEnabled(m_fileChangesAllowed);
}

void SoundManagerDialog::setStatus(const QString& text) {
    m_pendingFileLabel->setText(text);
    QAccessibleEvent event(m_pendingFileLabel, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&event);
    QAccessibleEvent alert(m_pendingFileLabel, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void SoundManagerDialog::previewCurrent(bool updateStatus) {
    const auto* entry = currentEntry();
    if (!entry) return;
    if (!m_soundService.isEnabled()) {
        if (updateStatus) {
            QMessageBox::information(this, QString::fromUtf8(u8"无法试听"),
                                     QString::fromUtf8(u8"请先开启总音效开关。"));
        }
        return;
    }
    if (entry->category == SoundCategory::BackgroundMusic) {
        if (m_soundService.previewMusic(entry->relativePath)) {
            m_musicPreviewTimer->start();
            if (updateStatus) {
                setStatus(QString::fromUtf8(u8"正在试听背景音乐，5秒后恢复原状态"));
            }
        } else {
            QMessageBox::warning(this, QString::fromUtf8(u8"试听失败"),
                                 QString::fromUtf8(u8"当前背景音乐文件无法播放。"));
        }
    } else {
        m_soundService.previewFile(entry->relativePath);
        if (updateStatus) setStatus(QString::fromUtf8(u8"正在试听：") + entry->displayName);
    }
}

void SoundManagerDialog::browseReplacement() {
    const QString file = QFileDialog::getOpenFileName(
        this, QString::fromUtf8(u8"选择替换音效"), QString(),
        QString::fromUtf8(u8"WAV 音频 (*.wav)"));
    if (file.isEmpty()) return;
    m_pendingFile = file;
    m_pendingFileLabel->setText(QString::fromUtf8(u8"待替换文件：") + file);
    refreshFileActionAvailability();
    m_confirmButton->setFocus(Qt::OtherFocusReason);
}

void SoundManagerDialog::confirmReplacement() {
    const auto* entry = currentEntry();
    if (!entry || m_pendingFile.isEmpty()) return;
    const QString warning = QString::fromUtf8(
        u8"即将覆盖“%1”的当前自定义音效。新音效会永久优先使用，直到恢复默认音效或再次替换。是否继续？")
        .arg(entry->displayName);
    if (!confirmDestructiveAction(QString::fromUtf8(u8"确认覆盖音效"), warning)) return;
    QString error;
    if (!m_overrideManager.replaceSound(*entry, m_pendingFile, &error)) {
        QMessageBox::warning(this, QString::fromUtf8(u8"替换失败"), error);
        m_confirmButton->setFocus(Qt::OtherFocusReason);
        return;
    }
    const QString name = entry->displayName;
    m_pendingFile.clear();
    refreshEntryStatus();
    setStatus(QString::fromUtf8(u8"替换成功，正在使用自定义音效：") + name);
    previewCurrent(false);
    m_browseButton->setFocus(Qt::OtherFocusReason);
}

void SoundManagerDialog::restoreCurrent() {
    const auto* entry = currentEntry();
    if (!entry || !m_overrideManager.hasOverride(entry->relativePath)) return;
    if (!confirmDestructiveAction(QString::fromUtf8(u8"恢复默认"),
            QString::fromUtf8(u8"确定删除“%1”的自定义音效并恢复软件默认音效吗？")
                .arg(entry->displayName))) return;
    QString error;
    if (!m_overrideManager.restoreSound(*entry, &error)) {
        QMessageBox::warning(this, QString::fromUtf8(u8"恢复失败"), error);
        return;
    }
    const QString name = entry->displayName;
    refreshEntryStatus();
    setStatus(QString::fromUtf8(u8"已恢复默认音效：") + name);
}

void SoundManagerDialog::restoreCurrentCategory() {
    const auto category = currentCategory();
    if (!confirmDestructiveAction(QString::fromUtf8(u8"恢复当前分类"),
            QString::fromUtf8(u8"确定删除“%1”分类中的全部自定义音效并恢复默认吗？")
                .arg(soundCategoryDisplayName(category)))) return;
    QString error;
    const int restored = m_overrideManager.restoreCategory(category, &error);
    if (restored < 0) {
        QMessageBox::warning(this, QString::fromUtf8(u8"恢复失败"), error);
        return;
    }
    refreshEntryStatus();
    setStatus(QString::fromUtf8(u8"当前分类已恢复%1个默认音效").arg(restored));
}

void SoundManagerDialog::restoreAll() {
    if (!confirmDestructiveAction(QString::fromUtf8(u8"恢复全部默认音效"),
            QString::fromUtf8(u8"确定删除全部已识别的自定义音效并恢复软件默认音效吗？其他设置不会改变。"))) return;
    QString error;
    const int restored = m_overrideManager.restoreAll(&error);
    if (restored < 0) {
        QMessageBox::warning(this, QString::fromUtf8(u8"恢复失败"), error);
        return;
    }
    refreshEntryStatus();
    setStatus(QString::fromUtf8(u8"已恢复%1个默认音效").arg(restored));
}

void SoundManagerDialog::importPack() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8(u8"选择音效包文件夹"));
    if (folder.isEmpty()) return;
    const auto analysis = m_overrideManager.analyzePack(folder);
    if (!analysis.invalidFiles.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8(u8"音效包无效"),
            QString::fromUtf8(u8"以下 WAV 无效，本次不会导入任何文件：") +
            QString(QChar(10)) + analysis.invalidFiles.join(QString(QChar(10))));
        return;
    }
    if (analysis.files.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8(u8"没有可导入文件"),
            QString::fromUtf8(u8"所选目录没有与标准音效路径匹配的 WAV 文件。"));
        return;
    }
    const QString summary = QString::fromUtf8(
        u8"识别到%1个可导入音效，%2个未知 WAV 将被忽略。导入内容将永久优先使用，直到恢复默认音效或再次替换。是否继续？")
        .arg(analysis.files.size()).arg(analysis.unknownFiles.size());
    if (!confirmDestructiveAction(QString::fromUtf8(u8"确认导入音效包"), summary)) return;
    QString error;
    if (!m_overrideManager.importPack(analysis, &error)) {
        QMessageBox::warning(this, QString::fromUtf8(u8"导入失败"), error);
        return;
    }
    refreshEntryStatus();
    setStatus(QString::fromUtf8(u8"已导入%1个自定义音效").arg(analysis.files.size()));
}

void SoundManagerDialog::exportTemplate() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8(u8"选择模板导出位置"));
    if (folder.isEmpty()) return;
    QString exportedPath;
    QString error;
    if (!m_overrideManager.exportTemplate(folder, &exportedPath, &error)) {
        QMessageBox::warning(this, QString::fromUtf8(u8"导出失败"), error);
        return;
    }
    setStatus(QString::fromUtf8(u8"音效包模板已导出到：") + exportedPath);
}

void SoundManagerDialog::exportCurrentPack() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8(u8"选择完整音效包导出位置"));
    if (folder.isEmpty()) return;
    QString exportedPath;
    QString error;
    if (!m_overrideManager.exportCurrentPack(folder, &exportedPath, &error)) {
        QMessageBox::warning(this, QString::fromUtf8(u8"导出失败"), error);
        return;
    }
    setStatus(QString::fromUtf8(u8"当前完整音效包已导出到：") + exportedPath);
}

bool SoundManagerDialog::confirmDestructiveAction(const QString& title,
                                                  const QString& text) {
    QMessageBox message(QMessageBox::Question, title, text,
                        QMessageBox::Yes | QMessageBox::No, this);
    message.setDefaultButton(QMessageBox::No);
    message.setEscapeButton(QMessageBox::No);
    return message.exec() == QMessageBox::Yes;
}

} // namespace fpdz
