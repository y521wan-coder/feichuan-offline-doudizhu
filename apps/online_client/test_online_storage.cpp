#include "online_storage.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray smallWave() {
    QByteArray bytes;
    bytes.append("RIFF", 4);
    bytes.append("&\0\0\0", 4); // 46-byte file: RIFF size 38 + 8.
    bytes.append("WAVEfmt ", 8);
    bytes.append("\x10\0\0\0", 4);
    bytes.append("\x01\0\x01\0", 4); // PCM, mono.
    bytes.append("\x40\x1f\0\0", 4); // 8000 Hz.
    bytes.append("\x40\x1f\0\0", 4); // 8000 bytes/second.
    bytes.append("\x01\0\x08\0", 4); // 8-bit samples.
    bytes.append("data\x02\0\0\0", 8);
    bytes.append("\x80\x80", 2);
    return bytes;
}
}

class TestOnlineStorage : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("fpdz_client_storage_test"));
        QCoreApplication::setApplicationName(QStringLiteral("OnlineStorageTest"));
        fpdz::OnlineStorage::clearSession();
        for (int id = 1; id <= 8; ++id) fpdz::OnlineStorage::removeEmojiSound(id);
    }
    void cleanupTestCase() {
        fpdz::OnlineStorage::clearSession();
        for (int id = 1; id <= 8; ++id) fpdz::OnlineStorage::removeEmojiSound(id);
    }
    void sessionEncryptedAndRevoked() {
        const QString dummyToken(64, QLatin1Char('a'));
        QVERIFY(fpdz::OnlineStorage::writeSession(dummyToken));
        QCOMPARE(fpdz::OnlineStorage::readSession(), dummyToken);
        QFile file(fpdz::OnlineStorage::dataDir() + QStringLiteral("/session.dpapi"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains(dummyToken.toUtf8()));
        file.close();
        fpdz::OnlineStorage::clearSession();
        QVERIFY(fpdz::OnlineStorage::readSession().isEmpty());
    }
    void emojiFilesAreIndependentAndReplaceable() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("mine.wav"));
        QFile wave(source);
        QVERIFY(wave.open(QIODevice::WriteOnly));
        const QByteArray original = smallWave();
        QCOMPARE(wave.write(original), original.size());
        wave.close();
        QString error;
        QVERIFY(fpdz::OnlineStorage::importEmojiSound(1, source, &error));
        QVERIFY(fpdz::OnlineStorage::emojiSoundPath(2).isEmpty());
        QFile imported(fpdz::OnlineStorage::emojiSoundPath(1));
        QVERIFY(imported.open(QIODevice::ReadOnly));
        QCOMPARE(imported.readAll(), original);
        imported.close();
        QVERIFY(fpdz::OnlineStorage::removeEmojiSound(1));
        QVERIFY(fpdz::OnlineStorage::emojiSoundPath(1).isEmpty());
    }
    void malformedSoundCannotReplaceExistingSound() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("bad.wav"));
        QFile bad(source);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        QCOMPARE(bad.write(QByteArray(50, 'x')), qint64(50));
        bad.close();
        QString error;
        QVERIFY(!fpdz::OnlineStorage::importEmojiSound(3, source, &error));
        QVERIFY(fpdz::OnlineStorage::emojiSoundPath(3).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestOnlineStorage)
#include "test_online_storage.moc"
