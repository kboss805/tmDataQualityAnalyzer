/**
 * @file abi_internal.h
 * @brief What the files behind tmdq_abi.h share: the per-thread error, the exception guard, and the
 *        reading and writing of versioned structs. Not part of the ABI.
 */

#ifndef TMDQ_ABI_INTERNAL_H
#define TMDQ_ABI_INTERNAL_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "tmdq_abi.h"

namespace tmdq {

/// Records the calling thread's last error (what tmdq_last_error returns) / clears it.
void setError(const std::string& message);
void clearError();

/// Runs @p body, which returns a tmdq_status, and turns anything it throws into TMDQ_ERR_INTERNAL with
/// the reason as the last error: unwinding across a C ABI into managed code is undefined, and a crash
/// there would be blamed on P/Invoke. @p what names the call for the message.
template <typename Body>
int32_t guarded(const char* what, Body&& body)
{
    try
    {
        return body();
    }
    catch (const std::exception& e)
    {
        setError(std::string("exception in ") + what + ": " + e.what());
    }
    catch (...)
    {
        setError(std::string("unknown exception in ") + what);
    }
    return TMDQ_ERR_INTERNAL;
}

/// @p text as a string; a NULL pointer reads as empty.
inline std::string textOf(const char* text) { return text ? std::string(text) : std::string(); }

/// Writes @p text into the caller's @p buffer, snprintf-style: returns its length (not counting the NUL), and
/// a buffer that is too small gets a truncated, terminated copy. A NULL buffer with a capacity of zero just
/// measures. @return -1 if the buffer and capacity do not make sense.
inline int32_t copyText(const std::string& text, char* buffer, int32_t capacity)
{
    if (capacity < 0 || (capacity > 0 && !buffer))
    {
        return -1;
    }
    if (capacity > 0)
    {
        const std::size_t room = static_cast<std::size_t>(capacity) - 1;
        const std::size_t count = std::min(room, text.size());
        std::memcpy(buffer, text.data(), count);
        buffer[count] = '\0';
    }
    return static_cast<int32_t>(text.size());
}

/// A path from UTF-8 text by the standard's UTF-8 conversion, not the process code page's.
inline std::filesystem::path pathFromUtf8(const char* utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8 ? utf8 : "")));
}

/// Copies a struct the host filled in. The host may have been built against an older revision, so only
/// the first @p in->struct_size bytes are read; the rest of @p out keeps its defaults. @return false if
/// @p in is NULL or its struct_size is below @p minimum (the size of the struct as first released).
template <typename T>
bool readStruct(const T* in, std::size_t minimum, T& out)
{
    if (!in || in->struct_size < minimum)
    {
        return false;
    }
    const std::size_t known = std::min<std::size_t>(in->struct_size, sizeof(T));
    std::memcpy(&out, in, known);
    out.struct_size = static_cast<uint32_t>(sizeof(T));
    return true;
}

/// Writes @p value into a struct the host owns, no further than the host's own struct_size. @return
/// false if @p out is NULL or its struct_size is below @p minimum.
template <typename T>
bool writeStruct(const T& value, std::size_t minimum, T* out)
{
    if (!out || out->struct_size < minimum)
    {
        return false;
    }
    const std::size_t host = std::min<std::size_t>(out->struct_size, sizeof(T));
    T copy = value;
    copy.struct_size = static_cast<uint32_t>(host);
    std::memcpy(out, &copy, host);
    return true;
}

} // namespace tmdq

#endif // TMDQ_ABI_INTERNAL_H
