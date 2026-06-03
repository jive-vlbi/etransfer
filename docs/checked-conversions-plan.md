# Checked-conversions plan

Status: **deferred**. The build does *not* currently enforce
`-Wconversion`. This document records the rationale for that decision
and sketches what a future, targeted "checked conversions" pass would
look like if and when we want to revisit it.

## Why `-Wconversion` is off

A trial run with the following flag set (Jun 2026)

```text
-std=c++11 -pedantic-errors -Wall -Wextra -Werror \
-Wcast-qual -Wwrite-strings -Wredundant-decls \
-Wfloat-equal -Wshadow -Wundef -Wconversion
```

produced ~30 `-Werror,-Wsign-conversion` (+ a handful of
`-Wshorten-64-to-32` / `-Wimplicit-int-conversion`) errors across the
project. The breakdown was:

| Category | Count | Shape | Real bug? |
|---|---|---|---|
| A | 2 | `#if O_LARGEFILE` on a system that doesn't define it (`-Wundef`) | **Yes** – fixed |
| B | ~19 | `read()`/`write()`/`lseek()` returning `ssize_t`/`off_t`, fed into `size_t` buffers | No – pure POSIX-API boundary noise |
| C | 7 | `tagged<T>{int_literal}` instantiations narrowing into `unsigned short` / `unsigned int` | No – literal fits, just no `_u` suffix |
| D | 1 | Mixed-sign ternary assigned to `rlim_t` | Cosmetic – fixed |
| E | 2 | `setsockopt` `level`/`optname` plumbing (`unsigned long` → `int`) | No – constant SOL_SOCKET-style identifiers |

Categories A and D were genuine cleanups and were fixed regardless.
Categories B, C and E together account for ~28 of the 30 hits and would
all be silenced by sprinkling `static_cast<>` at the call site.

**`static_cast` does not check anything at runtime.** It is a
compile-time annotation: narrowing converts modulo-2ⁿ, sign-flips
wrap, and out-of-range unsigned→signed is implementation-defined
pre-C++20 (defined as wrap from C++20). So a `-Wconversion`-driven
`static_cast` sweep would:

* add visual noise at every POSIX I/O boundary;
* hide nothing of value (the values are positive at those sites
  because the caller has already checked `n > 0` etc.);
* give zero additional runtime safety.

For a systems-y POSIX codebase that's a poor trade. `-Wconversion`
suits new code that controls its own integer plumbing end-to-end; it
does not suit one that hugs `read(2)`/`write(2)`/`lseek(2)`/UDT/SRT
APIs.

## What we *would* do instead, if we revisit this

The interesting part of `-Wconversion` is the small subset of warnings
that flag **actually unsafe** narrowing: a `size_t` from somewhere
inside the runtime flowing into an `int` that goes to a syscall, a
user-supplied `off_t` flowing into a buffer index, etc. The right tool
for these is a *checked* narrowing helper that throws on overflow,
rather than a project-wide warning that demands `static_cast`s.

### Step 1 – introduce `etdc::narrow<T>(x)`

A C++11-compatible drop-in equivalent of `gsl::narrow`:

```cpp
// etdc_narrow.h
#ifndef ETDC_NARROW_H
#define ETDC_NARROW_H

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace etdc {

    struct narrowing_error : public std::range_error {
        using std::range_error::range_error;
    };

    // narrow_cast: explicit, unchecked. Use when you've already
    // proved the value fits and you want the static_cast<> noise to
    // be self-documenting.
    template <typename T, typename U>
    constexpr T narrow_cast(U&& u) noexcept {
        return static_cast<T>(std::forward<U>(u));
    }

    // narrow: explicit, *checked*. Throws etdc::narrowing_error if
    // the value doesn't survive the round-trip through T.
    template <typename T, typename U>
    T narrow(U u) {
        const T t = static_cast<T>(u);
        if( static_cast<U>(t) != u )
            throw narrowing_error("etdc::narrow: value out of range for target type");
        // Sign-flip check (both directions, branchless ish)
        if( std::is_signed<T>::value != std::is_signed<U>::value &&
            ((t < T{}) != (u < U{})) )
            throw narrowing_error("etdc::narrow: sign change");
        return t;
    }

} // namespace etdc

#endif
```

Header-only, no dependencies, no allocations on the success path,
trivially testable.

### Step 2 – apply it *surgically* at boundary points

Not project-wide. Only where the value genuinely originates from an
untrusted source and a wrong narrowing would corrupt state. Strong
candidates, in rough order of payoff:

1. **`etdc_etdserver.cc` PROG parser** (`bytes_so_far` from the wire
   into `off_t`) – already uses `string2off_t`, fine. *No change.*
2. **`requestFileWrite` / `requestFileRead` offsets coming over the
   wire** – currently parsed straight into `off_t`. Wrap with
   `etdc::narrow<off_t>(...)` once we have `narrow`.
3. **Buffer length plumbing in `ETDServer::sendFile`/`getFile`**
   (`nWritten`/`nRead` ssize_t into size_t bookkeeping). Replace
   the implicit conversions with `etdc::narrow<size_t>(n)` after the
   existing `if( n > 0 )` guard. If POSIX ever lies about `n`, we
   throw instead of corrupting `todo`.
4. **UDT/SRT API edge** – sizes coming back from the lib.
5. **`tagged<>` constructor** – consider an internal
   `narrow_cast<T>(u)` in the perfect-forwarding ctor body, so
   `port_type{4004}` keeps compiling without the user adding a `u`
   suffix. This is the "category C" fix.

### Step 3 – decide whether to re-enable `-Wconversion`

After Step 2, residual `-Wconversion` hits should be a much smaller
set. We could then *either*:

* re-enable `-Wconversion` globally and fix the rest with
  `narrow_cast`/`narrow` as appropriate, *or*
* leave it off and rely on the explicit `narrow_cast`/`narrow`
  call-site annotations as the project's convention.

The author's leaning: option 2. Enforcement-by-convention is enough
when the helpers are visible, named, and easy to grep for. Adding
project-wide enforcement at this point would mostly produce
`narrow_cast<size_t>(n)` annotations at boundaries that are already
manifestly safe.

## Out of scope

* C-side flags (`CCOPT`). The few C compilation units are tiny; same
  analysis would apply if we cared.
* `libudt5ab` / `libsrt5ab`. Those have their own `Wall -Wextra` and
  are not part of this project's hygiene budget.
* Replacing `static_cast` in the existing fixes
  (`etdc_signal.h:54`, `etdc_fd.h:399`, `etd.cc:669`). Those are
  one-off annotations in places where the bounds are statically
  obvious; not worth wrapping in `narrow_cast`.

## TL;DR

* `-Wconversion` is intentionally **off**.
* Real bugs it surfaced (A, D) are fixed.
* Future hygiene work should be *runtime-checked* (`etdc::narrow`),
  not *compile-time-silenced* (project-wide `-Wconversion` +
  `static_cast` sprinkles).
* Start from `etdc_narrow.h` (sketched above), then apply at
  ~5 boundary points, then re-evaluate flag policy.
