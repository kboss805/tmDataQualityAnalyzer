/**
 * @file tst_textformat.h
 * @brief Tests for formatFixed(), which reproduces QString's 'f' formatting without Qt.
 *
 * Qt is the oracle: every case compares the helper against `QString::number(v, 'f', n)`. The
 * one place a plain `std::format` differs is an exact tie, so the cases concentrate there.
 */

#ifndef TST_TEXTFORMAT_H
#define TST_TEXTFORMAT_H

#include <QObject>

class TestTextFormat : public QObject
{
    Q_OBJECT

private slots:
    void exactTiesRoundAwayFromZeroLikeQt();
    void valuesThatAreNotTiesAgreeWithQt();
    void signsAndZerosAgreeWithQt();
    void everyBinaryFractionAgreesWithQtAtEveryPrecision();
    void aSweepOfArbitraryDoublesAgreesWithQt();
    void largeAndSmallMagnitudesAgreeWithQt();
    void isExactTieSeesOnlyRealTies();
};

#endif // TST_TEXTFORMAT_H
