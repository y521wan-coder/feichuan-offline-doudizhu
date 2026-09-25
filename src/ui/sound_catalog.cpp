#include "sound_catalog.h"

#include <array>

namespace fpdz {
namespace {

void addEntry(QVector<SoundCatalogEntry>& entries, SoundCategory category,
              const QString& id, const QString& name, const QString& path,
              const QString& description) {
    entries.push_back({id, category, name, path, description});
}

QString voiceLabel(const QString& directory) {
    return directory == QStringLiteral("girl")
        ? QString::fromUtf8(u8"女声") : QString::fromUtf8(u8"男声");
}

QVector<SoundCatalogEntry> buildCatalog() {
    QVector<SoundCatalogEntry> entries;
    addEntry(entries, SoundCategory::StartupDeal, QStringLiteral("game_start"),
             QString::fromUtf8(u8"软件启动"), QStringLiteral("card_four/BeginGame.wav"),
             QString::fromUtf8(u8"软件启动后播放，也用于现有测试音效"));
    addEntry(entries, SoundCategory::StartupDeal, QStringLiteral("deal_cards"),
             QString::fromUtf8(u8"开始发牌"), QStringLiteral("card_four/start.wav"),
             QString::fromUtf8(u8"新一局发牌时播放"));
    addEntry(entries, SoundCategory::YourTurn, QStringLiteral("your_turn"),
             QString::fromUtf8(u8"轮到你"), QStringLiteral("card_four/din.wav"),
             QString::fromUtf8(u8"轮到1时播放；该文件也被总音效开启提示共用"));
    addEntry(entries, SoundCategory::BiddingLandlord, QStringLiteral("landlord"),
             QString::fromUtf8(u8"地主确定"), QStringLiteral("card_four/robLandlord.wav"),
             QString::fromUtf8(u8"叫分结束并确定地主时播放"));
    addEntry(entries, SoundCategory::Multiplier, QStringLiteral("multiplier"),
             QString::fromUtf8(u8"倍数变化"), QStringLiteral("card_four/qiangbi.wav"),
             QString::fromUtf8(u8"非出牌事件造成倍数变化时播放"));
    addEntry(entries, SoundCategory::CardPattern, QStringLiteral("king_bomb"),
             QString::fromUtf8(u8"公共王炸效果"), QStringLiteral("card_four/king_bomb.wav"),
             QString::fromUtf8(u8"王炸和天尊等王类炸弹的公共效果"));
    addEntry(entries, SoundCategory::CardSelection, QStringLiteral("card_select"),
             QString::fromUtf8(u8"拿起牌"), QStringLiteral("card_four/up.wav"),
             QString::fromUtf8(u8"拿起一张或一组牌时播放"));
    addEntry(entries, SoundCategory::CardSelection, QStringLiteral("card_deselect"),
             QString::fromUtf8(u8"放下牌"), QStringLiteral("card_four/move.wav"),
             QString::fromUtf8(u8"放下一张或全部放下时播放"));
    addEntry(entries, SoundCategory::InvalidAction, QStringLiteral("invalid_action"),
             QString::fromUtf8(u8"非法操作"), QStringLiteral("card_four/GiveError.wav"),
             QString::fromUtf8(u8"非法出牌、无效命令或操作失败时播放"));
    addEntry(entries, SoundCategory::GameResult, QStringLiteral("win"),
             QString::fromUtf8(u8"胜利"), QStringLiteral("card_four/win.wav"),
             QString::fromUtf8(u8"1所在阵营获胜时播放"));
    addEntry(entries, SoundCategory::GameResult, QStringLiteral("lose"),
             QString::fromUtf8(u8"失败"), QStringLiteral("card_four/fail.wav"),
             QString::fromUtf8(u8"1所在阵营失败时播放"));
    addEntry(entries, SoundCategory::BackgroundMusic, QStringLiteral("music_background"),
             QString::fromUtf8(u8"背景音乐"), QStringLiteral("music/background.wav"),
             QString::fromUtf8(u8"背景或自动模式使用的音乐"));
    addEntry(entries, SoundCategory::BackgroundMusic, QStringLiteral("music_normal"),
             QString::fromUtf8(u8"普通音乐"), QStringLiteral("music/normal.wav"),
             QString::fromUtf8(u8"普通背景音乐模式使用"));
    addEntry(entries, SoundCategory::BackgroundMusic, QStringLiteral("music_intense"),
             QString::fromUtf8(u8"激昂音乐"), QStringLiteral("music/intense.wav"),
             QString::fromUtf8(u8"激昂背景音乐模式使用"));

    const std::array<QString, 15> rankStems = {
        QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"),
        QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
        QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("J"),
        QStringLiteral("Q"), QStringLiteral("K"), QStringLiteral("A"),
        QStringLiteral("2"), QStringLiteral("SmallKing"), QStringLiteral("BigKing")
    };
    const std::array<QString, 15> rankNames = {
        QString::fromUtf8(u8"3"), QString::fromUtf8(u8"4"), QString::fromUtf8(u8"5"),
        QString::fromUtf8(u8"6"), QString::fromUtf8(u8"7"), QString::fromUtf8(u8"8"),
        QString::fromUtf8(u8"9"), QString::fromUtf8(u8"10"), QString::fromUtf8(u8"钩"),
        QString::fromUtf8(u8"圈"), QString::fromUtf8(u8"凯"), QString::fromUtf8(u8"尖"),
        QString::fromUtf8(u8"2"), QString::fromUtf8(u8"小王"), QString::fromUtf8(u8"大王")
    };
    const std::array<QString, 2> voiceDirs = {QStringLiteral("boy"), QStringLiteral("girl")};
    for (const auto& voiceDir : voiceDirs) {
        const QString label = voiceLabel(voiceDir);
        for (std::size_t i = 0; i < rankStems.size(); ++i) {
            addEntry(entries, SoundCategory::CardPattern,
                     voiceDir + QStringLiteral("_single_") + rankStems[i],
                     label + QString::fromUtf8(u8"单张") + rankNames[i],
                     QStringLiteral("card_four/%1/%2.wav").arg(voiceDir, rankStems[i]),
                     QString::fromUtf8(u8"该声线打出对应单张，也用于组合牌点数拼接"));
            addEntry(entries, SoundCategory::CardPattern,
                     voiceDir + QStringLiteral("_pair_") + rankStems[i],
                     label + QString::fromUtf8(u8"对子") + rankNames[i],
                     QStringLiteral("card_four/%1/pair%2.wav").arg(voiceDir, rankStems[i]),
                     QString::fromUtf8(u8"该声线打出对应对子或组合牌对子翅膀时播放"));
        }

        const std::array<std::pair<QString, QString>, 12> patternStems = {{
            {QStringLiteral("three"), QString::fromUtf8(u8"三张提示")},
            {QStringLiteral("to"), QString::fromUtf8(u8"带牌连接词")},
            {QStringLiteral("zhi"), QString::fromUtf8(u8"连续范围连接词“至”")},
            {QStringLiteral("line"), QString::fromUtf8(u8"顺子提示")},
            {QStringLiteral("linkPair"), QString::fromUtf8(u8"连对提示")},
            {QStringLiteral("plane"), QString::fromUtf8(u8"飞机提示")},
            {QStringLiteral("qiangbi"), QString::fromUtf8(u8"枪提示")},
            {QStringLiteral("shuangwangqiangbi"), QString::fromUtf8(u8"双王枪毙")},
            {QStringLiteral("paohong"), QString::fromUtf8(u8"炮提示")},
            {QStringLiteral("huojian"), QString::fromUtf8(u8"火箭提示")},
            {QStringLiteral("daodan"), QString::fromUtf8(u8"导弹提示")},
            {QStringLiteral("tianzha"), QString::fromUtf8(u8"天炸提示")}
        }};
        for (const auto& item : patternStems) {
            const QString& stem = item.first;
            addEntry(entries, SoundCategory::CardPattern,
                     voiceDir + QStringLiteral("_pattern_") + stem,
                     label + item.second,
                     QStringLiteral("card_four/%1/%2.wav").arg(voiceDir, stem),
                     QString::fromUtf8(u8"出牌牌型语音的组成部分"));
        }
        for (int bid = 1; bid <= 3; ++bid) {
            addEntry(entries, SoundCategory::BiddingLandlord,
                     voiceDir + QStringLiteral("_bid_") + QString::number(bid),
                     label + QString::fromUtf8(u8"叫%1分").arg(bid),
                     QStringLiteral("card_four/%1/jiao%2.wav").arg(voiceDir).arg(bid),
                     QString::fromUtf8(u8"对应声线叫分时播放"));
        }
        addEntry(entries, SoundCategory::BiddingLandlord,
                 voiceDir + QStringLiteral("_no_bid"), label + QString::fromUtf8(u8"不叫"),
                 QStringLiteral("card_four/%1/bujiao.wav").arg(voiceDir),
                 QString::fromUtf8(u8"对应声线选择不叫时播放"));
        for (int pass = 1; pass <= 4; ++pass) {
            addEntry(entries, SoundCategory::Pass,
                     voiceDir + QStringLiteral("_pass_") + QString::number(pass),
                     label + QString::fromUtf8(u8"过牌变化%1").arg(pass),
                     QStringLiteral("card_four/%1/pass%2.wav").arg(voiceDir).arg(pass),
                     QString::fromUtf8(u8"过牌时按事件序号轮换播放"));
        }
        addEntry(entries, SoundCategory::LowCards,
                 voiceDir + QStringLiteral("_low_one"), label + QString::fromUtf8(u8"剩一张报警"),
                 QStringLiteral("card_four/%1/baojing1.wav").arg(voiceDir),
                 QString::fromUtf8(u8"对应玩家只剩一张牌时播放"));
        addEntry(entries, SoundCategory::LowCards,
                 voiceDir + QStringLiteral("_low_two"), label + QString::fromUtf8(u8"少量牌报警"),
                 QStringLiteral("card_four/%1/baojing2.wav").arg(voiceDir),
                 QString::fromUtf8(u8"对应玩家剩余少量牌时播放"));
    }
    return entries;
}

} // namespace

QString soundCategoryDisplayName(SoundCategory category) {
    switch (category) {
    case SoundCategory::StartupDeal: return QString::fromUtf8(u8"启动、菜单与发牌");
    case SoundCategory::BiddingLandlord: return QString::fromUtf8(u8"叫分与地主确定");
    case SoundCategory::CardPattern: return QString::fromUtf8(u8"出牌牌型语音");
    case SoundCategory::Pass: return QString::fromUtf8(u8"过牌语音");
    case SoundCategory::LowCards: return QString::fromUtf8(u8"剩牌报警");
    case SoundCategory::Multiplier: return QString::fromUtf8(u8"倍数变化");
    case SoundCategory::YourTurn: return QString::fromUtf8(u8"轮到你");
    case SoundCategory::CardSelection: return QString::fromUtf8(u8"拿起与放下牌");
    case SoundCategory::InvalidAction: return QString::fromUtf8(u8"非法操作提示");
    case SoundCategory::GameResult: return QString::fromUtf8(u8"胜负结算");
    case SoundCategory::BackgroundMusic: return QString::fromUtf8(u8"背景音乐");
    case SoundCategory::Count: break;
    }
    return {};
}

QVector<SoundCategory> soundCategories() {
    QVector<SoundCategory> categories;
    for (std::size_t i = 0; i < SOUND_CATEGORY_COUNT; ++i) {
        categories.push_back(static_cast<SoundCategory>(i));
    }
    return categories;
}

const QVector<SoundCatalogEntry>& soundCatalog() {
    static const QVector<SoundCatalogEntry> entries = buildCatalog();
    return entries;
}

QVector<SoundCatalogEntry> soundCatalogEntries(SoundCategory category) {
    QVector<SoundCatalogEntry> entries;
    for (const auto& entry : soundCatalog()) {
        if (entry.category == category) entries.push_back(entry);
    }
    return entries;
}

const SoundCatalogEntry* findSoundCatalogEntry(const QString& relativePath) {
    for (const auto& entry : soundCatalog()) {
        if (entry.relativePath == relativePath) return &entry;
    }
    return nullptr;
}

} // namespace fpdz
