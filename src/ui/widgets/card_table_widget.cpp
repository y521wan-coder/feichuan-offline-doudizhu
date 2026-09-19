#include "card_table_widget.h"

#include "../../core/engine/game_phase.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <algorithm>

namespace fpdz {

namespace {

QString rankText(Rank rank) {
    switch (rank) {
    case Rank::Three: return QStringLiteral("3");
    case Rank::Four: return QStringLiteral("4");
    case Rank::Five: return QStringLiteral("5");
    case Rank::Six: return QStringLiteral("6");
    case Rank::Seven: return QStringLiteral("7");
    case Rank::Eight: return QStringLiteral("8");
    case Rank::Nine: return QStringLiteral("9");
    case Rank::Ten: return QStringLiteral("10");
    case Rank::Jack: return QStringLiteral("J");
    case Rank::Queen: return QStringLiteral("Q");
    case Rank::King: return QStringLiteral("K");
    case Rank::Ace: return QStringLiteral("A");
    case Rank::Two: return QStringLiteral("2");
    case Rank::SmallJoker: return QString::fromUtf8(u8"小王");
    case Rank::BigJoker: return QString::fromUtf8(u8"大王");
    }
    return QStringLiteral("?");
}

QString suitText(Suit suit) {
    switch (suit) {
    case Suit::Spades: return QString::fromUtf8(u8"♠");
    case Suit::Hearts: return QString::fromUtf8(u8"♥");
    case Suit::Clubs: return QString::fromUtf8(u8"♣");
    case Suit::Diamonds: return QString::fromUtf8(u8"♦");
    case Suit::None: return {};
    }
    return {};
}

QColor cardColor(const Card& card) {
    if (card.rank() == Rank::BigJoker) return QColor(190, 32, 45);
    if (card.rank() == Rank::SmallJoker) return QColor(35, 35, 40);
    return card.suit() == Suit::Hearts || card.suit() == Suit::Diamonds
        ? QColor(196, 32, 45)
        : QColor(27, 31, 38);
}

QString roleText(const PlayerPublicState& player) {
    if (!player.roleRevealed) return {};
    return player.role == Role::Landlord
        ? QString::fromUtf8(u8"地主")
        : QString::fromUtf8(u8"农民");
}

bool containsCardId(const std::vector<CardId>& ids, CardId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void drawCardBack(QPainter& painter, const QRectF& rect) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(245, 226, 174), std::max(1.0, rect.width() * 0.035)));
    painter.setBrush(QColor(31, 79, 143));
    painter.drawRoundedRect(rect, rect.width() * 0.09, rect.width() * 0.09);

    const QRectF inner = rect.adjusted(rect.width() * 0.11, rect.width() * 0.11,
                                       -rect.width() * 0.11, -rect.width() * 0.11);
    painter.setPen(QPen(QColor(129, 176, 224), std::max(1.0, rect.width() * 0.025)));
    painter.drawRoundedRect(inner, rect.width() * 0.06, rect.width() * 0.06);
    painter.drawLine(inner.topLeft(), inner.bottomRight());
    painter.drawLine(inner.topRight(), inner.bottomLeft());
    painter.drawEllipse(inner.center(), inner.width() * 0.22, inner.width() * 0.22);
    painter.restore();
}

void drawCardFace(QPainter& painter, const QRectF& rect, const Card& card, bool selected) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 55));
    painter.drawRoundedRect(rect.translated(2.0, 3.0), rect.width() * 0.09, rect.width() * 0.09);

    QLinearGradient face(rect.topLeft(), rect.bottomRight());
    face.setColorAt(0.0, QColor(255, 255, 252));
    face.setColorAt(1.0, QColor(238, 238, 230));
    painter.setBrush(face);
    painter.setPen(QPen(selected ? QColor(255, 193, 45) : QColor(62, 66, 72),
                        selected ? 2.5 : 1.0));
    painter.drawRoundedRect(rect, rect.width() * 0.09, rect.width() * 0.09);

    const QColor ink = cardColor(card);
    painter.setPen(ink);
    QFont cornerFont = painter.font();
    cornerFont.setBold(true);
    cornerFont.setPixelSize(std::max(10, static_cast<int>(rect.height() * 0.19)));
    painter.setFont(cornerFont);

    const qreal pad = rect.width() * 0.09;
    if (card.suit() == Suit::None) {
        painter.drawText(rect.adjusted(pad, pad, -pad, -pad),
                         Qt::AlignCenter | Qt::TextWordWrap, rankText(card.rank()));
    } else {
        painter.drawText(QRectF(rect.left() + pad, rect.top() + pad,
                                rect.width() * 0.48, rect.height() * 0.28),
                         Qt::AlignLeft | Qt::AlignTop, rankText(card.rank()));
        QFont suitFont = cornerFont;
        suitFont.setPixelSize(std::max(13, static_cast<int>(rect.height() * 0.34)));
        painter.setFont(suitFont);
        painter.drawText(rect, Qt::AlignCenter, suitText(card.suit()));
    }

    QFont markerFont = painter.font();
    markerFont.setBold(true);
    markerFont.setPixelSize(std::max(7, static_cast<int>(rect.height() * 0.10)));
    painter.setFont(markerFont);
    painter.setPen(QColor(90, 94, 101));
    painter.drawText(QRectF(rect.right() - rect.width() * 0.28, rect.top() + pad,
                            rect.width() * 0.18, rect.height() * 0.15),
                     Qt::AlignRight | Qt::AlignTop,
                     QString::number(static_cast<int>(card.deckIndex()) + 1));
    painter.restore();
}

void drawFaceFan(QPainter& painter, const std::vector<Card>& cards,
                 const QRectF& area, qreal preferredHeight,
                 const std::vector<CardId>& selectedIds = {}, qreal selectedLift = 0.0) {
    if (cards.empty() || area.width() <= 0.0 || area.height() <= 0.0) return;
    const qreal cardHeight = std::min(preferredHeight, area.height() - 2.0);
    if (cardHeight <= 10.0) return;
    const qreal cardWidth = cardHeight * 0.68;
    const qreal available = std::max(cardWidth, area.width());
    const qreal naturalStep = cardWidth * 0.42;
    const qreal step = cards.size() <= 1
        ? 0.0
        : std::min(naturalStep,
                   std::max(3.0, (available - cardWidth) /
                                     static_cast<qreal>(cards.size() - 1)));
    const qreal fanWidth = cardWidth + step * static_cast<qreal>(cards.size() - 1);
    const qreal startX = area.center().x() - fanWidth / 2.0;
    const qreal baseY = area.bottom() - cardHeight;
    for (size_t index = 0; index < cards.size(); ++index) {
        const bool selected = containsCardId(selectedIds, cards[index].id());
        const qreal y = baseY - (selected ? selectedLift : 0.0);
        drawCardFace(painter,
                     QRectF(startX + step * static_cast<qreal>(index), y,
                            cardWidth, cardHeight),
                     cards[index], selected);
    }
}

void drawBackStack(QPainter& painter, const QRectF& area, int count, bool vertical) {
    if (count <= 0) return;
    const int shown = std::min(count, 5);
    const qreal cardHeight = std::min(48.0, vertical ? area.width() * 0.92 : area.height() * 0.84);
    const qreal cardWidth = cardHeight * 0.68;
    const qreal step = 5.0;
    const qreal stackWidth = vertical ? cardWidth : cardWidth + step * (shown - 1);
    const qreal stackHeight = vertical ? cardHeight + step * (shown - 1) : cardHeight;
    const qreal startX = area.center().x() - stackWidth / 2.0;
    const qreal startY = area.center().y() - stackHeight / 2.0;
    for (int index = 0; index < shown; ++index) {
        drawCardBack(painter, QRectF(startX + (vertical ? 0.0 : step * index),
                                     startY + (vertical ? step * index : 0.0),
                                     cardWidth, cardHeight));
    }
}

void drawPlayerPanel(QPainter& painter, const QRectF& rect,
                     const PlayerPublicState& player, bool current) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(current ? QColor(255, 220, 106, 235) : QColor(13, 62, 48, 225));
    painter.setPen(QPen(current ? QColor(130, 83, 4) : QColor(222, 238, 226),
                        current ? 2.4 : 1.0));
    painter.drawRoundedRect(rect, 8.0, 8.0);

    QString label = QString::fromStdWString(player.name);
    const QString role = roleText(player);
    if (!role.isEmpty()) label += QString::fromUtf8(u8" · ") + role;
    label += QString::fromUtf8(u8" · %1张").arg(player.remainingCards);
    if (player.lastActionWasPass) label += QString::fromUtf8(u8" · 过牌");
    QFont font = painter.font();
    font.setBold(current);
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(current ? QColor(58, 39, 0) : Qt::white);
    painter.drawText(rect.adjusted(7.0, 2.0, -7.0, -2.0), Qt::AlignCenter, label);
    painter.restore();
}

} // namespace

CardTableWidget::CardTableWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("visualCardTable"));
    setMinimumHeight(310);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAccessibleName({});
    setAccessibleDescription({});
}

void CardTableWidget::setTableState(const PublicGameSnapshot& snapshot,
                                    const std::vector<Card>& humanCards,
                                    const std::vector<CardId>& selectedCardIds) {
    m_snapshot = snapshot;
    m_humanCards = humanCards;
    m_selectedCardIds = selectedCardIds;
    update();
}

int CardTableWidget::displayedHumanCardCount() const {
    return static_cast<int>(m_humanCards.size());
}

int CardTableWidget::displayedBottomCardCount() const {
    return m_snapshot.bottomCardsRevealed
        ? static_cast<int>(m_snapshot.bottomCards.size())
        : 0;
}

int CardTableWidget::displayedLastPlayedCardCount() const {
    return static_cast<int>(m_snapshot.lastPlayedCards.size());
}

int CardTableWidget::displayedSelectedCardCount() const {
    return static_cast<int>(m_selectedCardIds.size());
}

int CardTableWidget::displayedOpponentBackCount(PlayerId playerId) const {
    const int index = static_cast<int>(playerId);
    if (index <= 0 || index >= PLAYER_COUNT) return 0;
    return m_snapshot.players[static_cast<size_t>(index)].remainingCards;
}

void CardTableWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect();
    QLinearGradient felt(bounds.topLeft(), bounds.bottomRight());
    felt.setColorAt(0.0, QColor(20, 113, 77));
    felt.setColorAt(1.0, QColor(8, 67, 49));
    painter.fillRect(bounds, felt);
    painter.setPen(QPen(QColor(225, 205, 129, 170), 2.0));
    painter.drawRoundedRect(bounds.adjusted(2.0, 2.0, -2.0, -2.0), 14.0, 14.0);

    if (m_snapshot.phase == GamePhase::NotStarted) {
        QFont titleFont = painter.font();
        titleFont.setBold(true);
        titleFont.setPixelSize(std::max(22, height() / 10));
        painter.setFont(titleFont);
        painter.setPen(QColor(250, 239, 193));
        painter.drawText(bounds, Qt::AlignCenter, QString::fromUtf8(u8"按 F1 开战"));
        return;
    }

    const qreal widthValue = bounds.width();
    const qreal heightValue = bounds.height();
    const qreal panelWidth = std::clamp(widthValue * 0.22, 145.0, 220.0);
    const qreal panelHeight = 28.0;

    const QRectF topPanel(bounds.center().x() - panelWidth / 2.0, 7.0,
                          panelWidth, panelHeight);
    const QRectF leftPanel(10.0, heightValue * 0.34, panelWidth, panelHeight);
    const QRectF rightPanel(widthValue - panelWidth - 10.0, heightValue * 0.34,
                           panelWidth, panelHeight);
    const QRectF humanPanel(10.0, heightValue - 35.0, panelWidth, panelHeight);
    drawPlayerPanel(painter, topPanel, m_snapshot.players[2],
                    m_snapshot.currentPlayer == PlayerId::Player3);
    drawPlayerPanel(painter, rightPanel, m_snapshot.players[1],
                    m_snapshot.currentPlayer == PlayerId::Player2);
    drawPlayerPanel(painter, leftPanel, m_snapshot.players[3],
                    m_snapshot.currentPlayer == PlayerId::Player4);
    drawPlayerPanel(painter, humanPanel, m_snapshot.players[0],
                    m_snapshot.currentPlayer == PlayerId::Player1);

    drawBackStack(painter,
                  QRectF(bounds.center().x() - 45.0, 34.0, 90.0, 56.0),
                  m_snapshot.players[2].remainingCards, false);
    drawBackStack(painter, QRectF(widthValue - 69.0, heightValue * 0.42, 55.0, 70.0),
                  m_snapshot.players[1].remainingCards, true);
    drawBackStack(painter, QRectF(14.0, heightValue * 0.42, 55.0, 70.0),
                  m_snapshot.players[3].remainingCards, true);

    const QRectF bottomArea(widthValue - 220.0, 8.0, 205.0, 64.0);
    QFont captionFont = painter.font();
    captionFont.setBold(true);
    captionFont.setPixelSize(12);
    painter.setFont(captionFont);
    painter.setPen(QColor(250, 239, 193));
    painter.drawText(QRectF(bottomArea.left(), bottomArea.top(), bottomArea.width(), 18.0),
                     Qt::AlignCenter,
                     m_snapshot.bottomCardsRevealed
                         ? QString::fromUtf8(u8"公开底牌")
                         : QString::fromUtf8(u8"底牌未公开"));
    if (m_snapshot.bottomCardsRevealed) {
        drawFaceFan(painter, m_snapshot.bottomCards,
                    bottomArea.adjusted(0.0, 18.0, 0.0, 0.0), 42.0);
    } else {
        drawBackStack(painter, bottomArea.adjusted(0.0, 18.0, 0.0, 0.0), BOTTOM_CARDS, false);
    }

    const QRectF playArea(widthValue * 0.22, heightValue * 0.30,
                          widthValue * 0.56, heightValue * 0.34);
    QString playCaption = QString::fromUtf8(u8"桌面暂无出牌");
    if (m_snapshot.lastPlayedValid && !m_snapshot.lastPlayedCards.empty()) {
        playCaption = QString::fromUtf8(u8"上一手：") +
                      QString::fromStdWString(
                          m_snapshot.players[static_cast<size_t>(m_snapshot.lastPlayedBy)].name);
    }
    if (!m_snapshot.actionHistory.empty() &&
        m_snapshot.actionHistory.back().type == PublicActionType::Pass) {
        const auto playerId = m_snapshot.actionHistory.back().playerId;
        playCaption += QString::fromUtf8(u8"　%1过牌").arg(
            QString::fromStdWString(m_snapshot.players[static_cast<size_t>(playerId)].name));
    }
    painter.setPen(QColor(250, 239, 193));
    painter.drawText(QRectF(playArea.left(), playArea.top(), playArea.width(), 20.0),
                     Qt::AlignCenter, playCaption);
    if (m_snapshot.lastPlayedValid) {
        drawFaceFan(painter, m_snapshot.lastPlayedCards,
                    playArea.adjusted(0.0, 20.0, 0.0, 0.0),
                    std::min(74.0, playArea.height() - 22.0));
    }

    const QRectF humanCardsArea(widthValue * 0.08, heightValue * 0.56,
                                widthValue * 0.84, heightValue * 0.31);
    drawFaceFan(painter, m_humanCards, humanCardsArea,
                std::min(92.0, humanCardsArea.height() - 2.0),
                m_selectedCardIds, 12.0);
}

} // namespace fpdz
