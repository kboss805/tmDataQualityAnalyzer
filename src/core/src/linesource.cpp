/**
 * @file linesource.cpp
 * @brief The lines of a text file as UTF-8, and comma splitting (see linesource.h).
 */

#include "linesource.h"

#include <algorithm>
#include <sstream>

#include "textfile.h"

namespace csvlines {

LineSource::LineSource(const std::filesystem::path& path)
{
    m_file.open(path, std::ios::binary);
    if (!m_file)
    {
        return;
    }
    m_file.seekg(0, std::ios::end);
    m_size = static_cast<std::size_t>(m_file.tellg());
    m_file.seekg(0, std::ios::beg);

    unsigned char head[3] = {0, 0, 0};
    m_file.read(reinterpret_cast<char*>(head), 3);
    const std::streamsize got = m_file.gcount();
    m_file.clear();
    m_file.seekg(0, std::ios::beg);

    const textfile::ByteOrderMark mark = textfile::byteOrderMark(head, static_cast<std::size_t>(got));
    if (mark == textfile::ByteOrderMark::Utf16LittleEndian || mark == textfile::ByteOrderMark::Utf16BigEndian)
    {
        std::ostringstream all;
        all << m_file.rdbuf();
        m_text = textfile::decodeText(all.str());
        m_in_memory = true;
        m_size = m_text.size();
    }
    else if (mark == textfile::ByteOrderMark::Utf8)
    {
        m_file.seekg(3, std::ios::beg);
        m_consumed = 3;
    }
}

bool LineSource::next(std::string& line)
{
    bool got = false;
    if (m_in_memory)
    {
        if (m_pos >= m_text.size())
        {
            return false;
        }
        const std::size_t end = m_text.find('\n', m_pos);
        if (end == std::string::npos)
        {
            line.assign(m_text, m_pos, std::string::npos);
            m_pos = m_text.size();
        }
        else
        {
            line.assign(m_text, m_pos, end - m_pos);
            m_pos = end + 1;
        }
        m_consumed = m_pos;
        got = true;
    }
    else
    {
        if (!std::getline(m_file, line, '\n'))
        {
            return false;
        }
        m_consumed += line.size() + 1;
        got = true;
    }
    // Every carriage return is ignored, wherever it is (not just the one before a line feed), as
    // v2.14's importer read them, so a lone one neither ends a line nor stays in a name.
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    return got;
}

void splitKeepEmpty(std::string_view text, char sep, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t at = text.find(sep, start);
        if (at == std::string_view::npos)
        {
            out.push_back(text.substr(start));
            return;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

} // namespace csvlines
