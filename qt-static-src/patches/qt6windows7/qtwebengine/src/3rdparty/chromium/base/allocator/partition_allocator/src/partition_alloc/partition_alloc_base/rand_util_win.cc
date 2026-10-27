// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/partition_alloc_base/rand_util.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "partition_alloc/partition_alloc_base/check.h"

// Prototype for ProcessPrng.
// See: https://learn.microsoft.com/en-us/windows/win32/seccng/processprng
extern "C" {
BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData);
}

namespace partition_alloc::internal::base {

namespace {

// Windows 7 backport: ProcessPrng was introduced in Windows 10. bcryptprimitives.dll
// itself is present on Windows 7, so LoadLibraryW() below succeeds and only the export
// lookup fails - which the original code turns into a CHECK failure, aborting the
// process (int 3) the first time random bytes are requested.
//
// Fall back to RtlGenRandom(), which is what Chromium used before it switched to
// ProcessPrng. That switch was made to avoid opening a handle to \Device\KsecDD in the
// sandboxed renderer (see the comment in RandBytes() below); nothing here depends on it.
using RtlGenRandomFn = BOOLEAN(WINAPI*)(PVOID RandomBuffer, ULONG RandomBufferLength);

RtlGenRandomFn GetRtlGenRandom() {
  HMODULE hmod = LoadLibraryW(L"advapi32.dll");
  PA_BASE_CHECK(hmod);
  RtlGenRandomFn rtl_gen_random_fn =
      reinterpret_cast<RtlGenRandomFn>(GetProcAddress(hmod, "SystemFunction036"));
  PA_BASE_CHECK(rtl_gen_random_fn);
  return rtl_gen_random_fn;
}

}  // namespace

void RandBytes(void* output, size_t output_length) {
  // Import bcryptprimitives directly rather than cryptbase to avoid opening a
  // handle to \\Device\KsecDD in the renderer.
  // Note: we cannot use a magic static here as PA runs too early in process
  // startup, but this should be safe as the process will be single-threaded
  // when this first runs.
  static decltype(&ProcessPrng) process_prng_fn = nullptr;
  // Windows 7 backport: only set when ProcessPrng is unavailable.
  static RtlGenRandomFn rtl_gen_random_fn = nullptr;

  if (!process_prng_fn && !rtl_gen_random_fn) {
    HMODULE hmod = LoadLibraryW(L"bcryptprimitives.dll");
    PA_BASE_CHECK(hmod);
    process_prng_fn = reinterpret_cast<decltype(&ProcessPrng)>(
        GetProcAddress(hmod, "ProcessPrng"));
    if (!process_prng_fn) {
      rtl_gen_random_fn = GetRtlGenRandom();
    }
  }

  if (rtl_gen_random_fn) {
    // Windows 7 backport: RtlGenRandom takes a ULONG length, so feed it in chunks.
    BYTE* cursor = static_cast<BYTE*>(output);
    size_t remaining = output_length;
    while (remaining > 0) {
      const ULONG chunk = static_cast<ULONG>(
          std::min<size_t>(remaining, std::numeric_limits<ULONG>::max()));
      const BOOLEAN success = rtl_gen_random_fn(cursor, chunk);
      PA_BASE_CHECK(success);
      cursor += chunk;
      remaining -= chunk;
    }
    return;
  }

  BOOL success = process_prng_fn(static_cast<BYTE*>(output), output_length);
  // ProcessPrng is documented to always return TRUE.
  PA_BASE_CHECK(success);
}

}  // namespace partition_alloc::internal::base
