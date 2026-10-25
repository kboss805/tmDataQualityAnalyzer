// Helpers the native tests share. Header-only, no Qt.

#ifndef TMDQ_TEST_SUPPORT_H
#define TMDQ_TEST_SUPPORT_H

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"

namespace tmdq_test {

namespace fs = std::filesystem;

using Strings = std::vector<std::string>;

/// A fresh folder under the system temp directory, removed with everything in it when it goes out of scope.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        std::ostringstream name;
        name << "tmdq-test-" << std::hex << rd() << rd();
        m_path = fs::temp_directory_path() / name.str();
        fs::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }
    fs::path file(const std::string& name) const { return m_path / name; }

private:
    fs::path m_path;
};

/// Writes raw bytes, so a test controls the encoding, BOM and line endings exactly.
inline void writeBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    REQUIRE(f.is_open());
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(f.good());
}

inline std::string readBytes(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        return {};
    }
    std::ostringstream all;
    all << f.rdbuf();
    return all.str();
}

inline bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// The UTF-8 bytes of a char8_t literal.
inline std::string utf8(const char8_t* text)
{
    return std::string(reinterpret_cast<const char*>(text));
}

/// The repository root: the first folder above the working directory, or above this header's own location, that
/// holds `tmdq.sln`. The second is what makes the committed fixtures (`tests/fixtures`, `settings/`) reachable however
/// the executable is started; MSBuild hands the compiler absolute paths, so `__FILE__` is one. Empty if neither finds it.
inline fs::path repoRoot()
{
    std::error_code ec;
    for (fs::path start : {fs::current_path(ec), fs::path(__FILE__).parent_path()})
    {
        for (fs::path dir = fs::absolute(start, ec); !dir.empty(); dir = dir.parent_path())
        {
            if (fs::exists(dir / "tmdq.sln", ec))
            {
                return dir;
            }
            if (dir == dir.parent_path())
            {
                break;
            }
        }
    }
    return {};
}

/// A committed file under the repository, required to exist: a missing fixture is a broken checkout, not a skip.
inline fs::path repoFile(const fs::path& relative)
{
    const fs::path root = repoRoot();
    REQUIRE_MESSAGE(!root.empty(), "the repository root (the folder holding tmdq.sln) was not found");
    const fs::path file = root / relative;
    REQUIRE_MESSAGE(fs::exists(file), "missing committed file: " << file.string());
    return file;
}

} // namespace tmdq_test

#endif // TMDQ_TEST_SUPPORT_H
