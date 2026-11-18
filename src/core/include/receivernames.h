/**
 * @file receivernames.h
 * @brief The naming rules of a receiver parameter: "<channel>_RCVR<N>", written and read in one place.
 *
 * A Receiver SNR channel is named for its position in the receiver's channels and for its receiver
 * (`L_RCVR1`, `R_RCVR1`, `C_RCVR1`, `CH4_RCVR1`, ...). The default word map, the calibration summary, the series builder
 * and the CSV column schema all write or read that name, and they must agree: a divergence silently misattaches
 * calibration profiles. The `_RCVR` token, the prefixes and the number's rule are here once.
 */

#ifndef RECEIVERNAMES_H
#define RECEIVERNAMES_H

#include <string>

namespace receivernames {

/// Channel-position prefix for a 0-based channel index: "L"/"R"/"C" for the known positions, "CH<n+1>" beyond them.
std::string channelPrefix(int channel_index);

/// Canonical default parameter name "<prefix>_RCVR<receiver+1>" for a 0-based channel and receiver index. The single
/// source of truth shared by the main processing run and the calibration extractor so their default word maps agree on
/// names (a divergence silently misattaches calibration profiles).
std::string receiverParameterName(int channel_index, int receiver_index);

/// Inverse of receiverParameterName(): the 1-based receiver number carried by a parameter name's "_RCVR<N>" suffix, or 0
/// when the name carries none.
///
/// Returning 0 rather than failing is deliberate: a receiver-params TOML may name its parameters anything at all, so
/// callers that group by receiver (the calibration summary) must degrade to listing the names themselves rather than
/// inventing a receiver number.
int receiverIndexFromName(const std::string& name);

/// The number after the LAST "_RCVR" in a name, as written: 0 when there is no token or the text after it is not a
/// whole decimal number, and a negative number kept as it is. The series builder and the CSV column schema read names
/// with this (a hand-edited CSV header "_RCVR-7" was a receiver -7 in v2.14.2, and the recorded digests of
/// SeriesColumnSchema keep it so); receiverIndexFromName() is this, with a number that is not positive made 0, because a
/// calibration summary has no use for a receiver 0 or -7.
int receiverNumberInName(const std::string& name);

/// The channel-position part of a "<prefix>_RCVR<N>" name ("L", "R", "C", ...), or the whole name when it carries no
/// "_RCVR<N>" suffix.
std::string channelPartOfName(const std::string& name);

} // namespace receivernames

#endif // RECEIVERNAMES_H
