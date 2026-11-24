// test_pch.h - the precompiled header of the native tests (tmdq_core_tests.vcxproj).
//
// doctest and the standard headers that most test files include, parsed once instead of by each of the 31 files that use
// doctest. The project forces it into every test_*.cpp (ForcedIncludeFiles), so a test file need not include it, and the
// includes a test file has stay as they are, for clangd and for a reader of the file alone. Not used by main.cpp, which
// defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN before doctest.h (a precompiled doctest.h would ignore the define), nor by
// the core's sources and irig106's C files, which the project compiles for the test executable too.
//
// A header belongs here when many test files include it and it is slow to parse (<format>, <filesystem>, <random>,
// <thread>, doctest itself), not because one file does. Adding one costs a rebuild of every test file; a change to
// this file does the same.
//
// A test file must still include everything it uses, as if this header did not exist: clangd reads
// compile_flags.txt, not the project, and does not see the forced include, so a file that leans on it shows errors in
// the editor while the build stays green.
//
// Measured on four cores, the hosted runner's size (a full rebuild of the test project, twice each): 75.5 s without it,
// 66.4 s with it, about 12 %. The project compiles with /Z7 for it (see tmdq_core_tests.vcxproj).
#pragma once

#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
