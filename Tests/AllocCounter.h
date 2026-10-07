#pragma once

// Allocation counter (from Brimstone's SoakTests). Replaces the global
// operator new / delete, so include it in exactly ONE source file per test
// executable.
//
// Counts every operator new / delete made by a thread while its
// `countingAllocs` is set. Only the audio path is wrapped in a CountScope, so
// the harness (and other threads) are free to allocate.

#include <cstdlib>
#include <new>

static thread_local bool countingAllocs = false;
static long allocCount = 0, freeCount = 0;

void* operator new (std::size_t n)
{
    if (countingAllocs)
        ++allocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { if (countingAllocs && p != nullptr) ++freeCount; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

struct CountScope
{
    CountScope() { countingAllocs = true; }
    ~CountScope() { countingAllocs = false; }
};
