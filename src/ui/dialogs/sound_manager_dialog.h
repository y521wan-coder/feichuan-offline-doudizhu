#pragma once

#include "../../app/app_settings.h"
#include "../sound_override_manager.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace fpdz {

class SoundService;

class SoundManagerDialog : public QDialog {
    Q_OBJECT
public:
    SoundManagerDialog(AppSettings& settings, SoundService& soundService,
                       bool fileChangesAllowed, QWidget* parent = nullptr);
    ~SoundManagerDialog() override;

signals:
    void settingsChanged();
    void backgroundMusicRestoreRequested();
    void openSoundDocumentationRequested();

private:
    void populateCategories();
    void populateEntries();
    void refreshEntryStatus();
    void refreshFileActionAvailability();
    SoundCategory currentCategory() const;
    const SoundCatalogEntry* currentEntry() const;
    void setStatus(const QString& text);
    void previewCurrent(bool updateStatus = true);
    void browseReplacement();
    void confirmReplacement();
    void restoreCurrent();
    void restoreCurrentCategory();
    void restoreAll();
    void importPack();
    void exportTemplate();
    void exportCurrentPack();
    bool confirmDestructiveAction(const QString& title, const QString& text);

    AppSettings& m_settings;
    SoundService& m_soundService;
    SoundOverrideManager m_overrideManager;
    bool m_fileChangesAllowed = true;
    QVector<SoundCatalogEntry> m_entries;
    QString m_pendingFile;

    QComboBox* m_categoryCombo = nullptr;
    QCheckBox* m_categoryEnabled = nullptr;
    QComboBox* m_entryCombo = nullptr;
    QLabel* m_entryStatus = nullptr;
    QPushButton* m_previewButton = nullptr;
    QPushButton* m_browseButton = nullptr;
    QLabel* m_pendingFileLabel = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_restoreCurrentButton = nullptr;
    QPushButton* m_restoreCategoryButton = nullptr;
    QPushButton* m_importButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_exportCurrentButton = nullptr;
    QPushButton* m_restoreAllButton = nullptr;
    QPushButton* m_documentationButton = nullptr;
    QTimer* m_musicPreviewTimer = nullptr;
};

} // namespace fpdz
