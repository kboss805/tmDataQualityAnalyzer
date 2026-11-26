/**
 * @file exceptionboundary.h
 * @brief Where an exception stops: the top of a thread the core starts, which must never let one out.
 *
 * An exception that leaves a thread's function calls std::terminate, which ends the host application with it: one
 * `std::bad_alloc` on a long run took the whole window down. The runs' threads (`PipelineRunner`, `CalibrationExtractor`)
 * run their bodies through containExceptions() instead, which turns whatever is thrown into a report the run makes
 * like any other failure. The C ABI has its own boundary for its functions (`tmdq::guarded`, abi_internal.h).
 */

#ifndef EXCEPTIONBOUNDARY_H
#define EXCEPTIONBOUNDARY_H

#include <exception>

/**
 * @brief Runs @p body; if it throws, runs @p on_throw with the exception's words, and lets nothing out either way.
 *
 * @p on_throw gets `what()` for a `std::exception` and "an unknown exception" for anything else. Whatever it throws
 * in turn (building a message can run out of memory too) is dropped: by then there is no one left to tell.
 *
 * @return true if @p body returned normally, false if it threw.
 */
template <typename Body, typename OnThrow>
bool containExceptions(Body&& body, OnThrow&& on_throw) noexcept
{
    try
    {
        body();
        return true;
    }
    catch (const std::exception& e)
    {
        try
        {
            on_throw(e.what());
        }
        // NOLINTNEXTLINE(bugprone-empty-catch): the report itself failed; nothing may leave the thread
        catch (...)
        {
        }
    }
    catch (...)
    {
        try
        {
            on_throw("an unknown exception");
        }
        // NOLINTNEXTLINE(bugprone-empty-catch): the report itself failed; nothing may leave the thread
        catch (...)
        {
        }
    }
    return false;
}

#endif // EXCEPTIONBOUNDARY_H
