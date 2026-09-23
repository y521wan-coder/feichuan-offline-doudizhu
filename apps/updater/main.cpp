#include "update_service.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>

using namespace fpdz;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("feichuan_offline_doudizhu_updater"));
    app.setApplicationDisplayName(QString::fromUtf8(u8"飞船斗地主更新器"));
    app.setApplicationVersion(QStringLiteral(FPDZ_APP_VERSION));

    const QStringList arguments = QCoreApplication::arguments();
    const bool manual = arguments.contains(QStringLiteral("--manual"));
    const int versionIndex = arguments.indexOf(QStringLiteral("--current-version"));
    const QString currentVersion = versionIndex >= 0 && versionIndex + 1 < arguments.size()
        ? arguments[versionIndex + 1] : QCoreApplication::applicationVersion();

    UpdateService service;
    QProgressDialog* progress = nullptr;
    QObject::connect(&service, &UpdateService::checkFinished, &app,
        [&](const UpdateCheckResult& result) {
            if (!result.success) {
                if (manual) QMessageBox::warning(nullptr, QString::fromUtf8(u8"检查更新失败"),
                                                 result.errorMessage);
                app.quit();
                return;
            }
            if (!result.updateAvailable) {
                if (manual) QMessageBox::information(
                    nullptr, QString::fromUtf8(u8"检查更新"),
                    QString::fromUtf8(u8"当前已是最新版本：") + currentVersion);
                app.quit();
                return;
            }
            const auto answer = QMessageBox::question(
                nullptr, QString::fromUtf8(u8"发现新版本 ") + result.latestVersion,
                result.releaseNotes.trimmed().isEmpty()
                    ? QString::fromUtf8(u8"是否下载并安装经过校验的新版本？")
                    : result.releaseNotes);
            if (answer != QMessageBox::Yes) {
                app.quit();
                return;
            }
            progress = new QProgressDialog(QString::fromUtf8(u8"正在下载并验证更新……"),
                                           QString::fromUtf8(u8"取消"), 0, 1000);
            progress->setWindowTitle(QString::fromUtf8(u8"飞船斗地主更新器"));
            progress->setMinimumDuration(0);
            QObject::connect(progress, &QProgressDialog::canceled,
                             &service, &UpdateService::cancel);
            QObject::connect(&service, &UpdateService::downloadProgress, progress,
                [progress](qint64 received, qint64 total) {
                    if (total <= 0) {
                        progress->setRange(0, 0);
                    } else {
                        progress->setRange(0, 1000);
                        progress->setValue(static_cast<int>(received * 1000 / total));
                    }
                });
            progress->show();
            service.downloadAndVerify(result);
        });
    QObject::connect(&service, &UpdateService::downloadFinished, &app,
        [&](const QString& installerPath, const QString& error) {
            if (progress) progress->close();
            if (!error.isEmpty()) {
                QMessageBox::critical(nullptr, QString::fromUtf8(u8"更新失败"), error);
                app.quit();
                return;
            }
            const QFileInfo installer(installerPath);
            if (!installer.exists() || !QProcess::startDetached(
                    installer.absoluteFilePath(), {QStringLiteral("/CLOSEAPPLICATIONS"),
                                                   QStringLiteral("/RESTARTAPPLICATIONS")})) {
                QMessageBox::critical(nullptr, QString::fromUtf8(u8"更新失败"),
                                      QString::fromUtf8(u8"无法启动已验证的更新安装包。"));
            }
            app.quit();
        });

    service.checkForUpdates(currentVersion);
    return app.exec();
}
