/**
 * @file linesource.h
 * @brief The lines of a text file as UTF-8, and comma splitting: what the CSV readers share.
 *
 * `CsvSeriesParser` (the application's own export) and `PhasedArrayLogParser` (a third party's log) read a file the same
 * way: UTF-8 or UTF-16 with a byte-order mark honoured, a line ends at a line feed and carriage returns are ignored
 * wherever they are, and a field is split at every comma with empty ones kept.
 */

#ifndef LINESOURCE_H
#define LINESOURCE_H

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace csvlines {

/// The lines of the file as UTF-8. A UTF-16 file (with a BOM) is read whole and transcoded; anything
/// else is streamed, with a UTF-8 BOM dropped.
class LineSource
{
public:
    explicit LineSource(const std::filesystem::path& path);

    bool isOpen() const { return m_in_memory || m_file.is_open(); }

    /// Bytes of the file not read yet (an estimate for pre-allocation only).
    std::size_t remaining() const { return m_size > m_consumed ? m_size - m_consumed : 0; }

    /// The next line without its terminator. @return false at the end of the file.
    bool next(std::string& line);

private:
    std::ifstream m_file;
    std::string m_text;
    bool m_in_memory = false;
    std::size_t m_pos = 0;
    std::size_t m_size = 0;
    std::size_t m_consumed = 0;
};

/// Splits @p text at every @p sep, keeping empty parts (so "a,,b" is three).
void splitKeepEmpty(std::string_view text, char sep, std::vector<std::string_view>& out);

} // namespace csvlines

#endif // LINESOURCE_H
