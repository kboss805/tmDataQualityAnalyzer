/**
 * @file containerutil.h
 * @brief Small helpers for working with `std::` containers in code that was written
 *        against Qt's.
 *
 * `QVector::size()` returns a signed `qsizetype`; `std::vector::size()` returns an
 * unsigned `size_t`. The code base indexes with `int` throughout, and the signed type
 * is what makes `for (int i = 1; i < v.size(); ++i)` correct on an empty vector and
 * `v.size() - 1` a harmless -1 rather than 18 quintillion. Rather than rewrite every
 * loop to unsigned arithmetic (and audit each subtraction), call sites that used
 * `.size()` as a number use intSize() and keep their signed idiom.
 */

#ifndef CONTAINERUTIL_H
#define CONTAINERUTIL_H

/// Element count of a `std::` container as an `int`, matching what `QVector::size()`
/// gave the call site. Containers here never approach INT_MAX elements.
template <typename Container>
constexpr int intSize(const Container& c) noexcept
{
    return static_cast<int>(c.size());
}

#endif // CONTAINERUTIL_H
