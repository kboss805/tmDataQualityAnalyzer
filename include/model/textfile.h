/**
 * @file textfile.h
 * @brief Reading of small text files as UTF-8, and a few path helpers.
 *
 * The settings reader (`TomlDocument`) and the step-file parser
 * (`StepDetector::parseStepConfig`) must both read the encodings Notepad writes, which
 * reading the bytes with the standard library does not handle, so that handling lives
 * here once instead of in each: a UTF-8 byte-order mark is skipped, and UTF-16 (little
 * or big endian, BOM required) is decoded. A file with no BOM is taken to be UTF-8.
 *
 * Pinned by native/tmdq_core_tests/test_textfile.cpp, test_tomldocument.cpp and the
 * `parseStepConfig` cases in test_stepdetector.cpp.
 */

#ifndef TEXTFILE_H
#define TEXTFILE_H

#include <filesystem>
#include <string>

namespace textfile {

/// The whole file as UTF-8 text, BOM handled. @return false if it cannot be opened,
/// leaving @p out untouched.
bool readText(const std::filesystem::path& path, std::string& out);

/// @p bytes as UTF-8: a UTF-8 BOM is dropped, UTF-16 with a BOM is transcoded (an
/// unpaired surrogate becomes U+FFFD), anything else is returned as it is.
std::string decodeText(const std::string& bytes);

/// The file-name part of @p path as UTF-8, for messages.
std::string utf8FileName(const std::filesystem::path& path);

/// A path from UTF-8 text, losslessly. `std::filesystem::path(std::string)` would read
/// the bytes in the ANSI code page on Windows.
std::filesystem::path pathFromUtf8(const std::string& utf8);

} // namespace textfile

#endif // TEXTFILE_H
