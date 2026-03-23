# TMB/Rcpp Macro Conflict Reprex

Minimal reproducible example demonstrating macro
definition order conflicts when TMB and Rcpp are included
in the same C++ translation unit.

## Motivation

We are developing an R package that refactors repeated C++
header functions from multiple fisheries stock assessment
models into a single shared source.  The goal is to reduce
code drift across projects and improve maintainability.

These shared functions (e.g., Baranov equation solvers,
composition likelihood kernels) are not TMB objective
functions -- they are standalone templates that use TMB
types such as `tmbutils::array<Type>` and `vector<Type>`.
Because the functions have no `objective_function`, we
cannot test them through TMB's `MakeADFun()` interface.
Instead we expose them to R via Rcpp exports and test
directly with `testthat`, which requires both
`#include <TMB.hpp>` (for the types) and
`#include <Rcpp.h>` (for the exports) in the same
translation unit.

This include pattern triggers compilation failures.

## The conflicts

TMB re-exports several R macros that collide with Rcpp
class methods and template code. There are two distinct
conflicts:

### 1. ISNAN macro leak

TMB's `tiny_ad/bessel/bessel.hpp` (and `gamma.hpp`,
`pbeta.hpp`) defines:

```cpp
namespace bessel_utils {
  template<class T>
  int isnan(T x) { return std::isnan(asDouble(x)); }
  ...
  #define ISNAN(x) (isnan(x)!=0)
  ...
  #include "bessel_k.cpp"  // uses ISNAN internally
  #include "undefs.h"      // does NOT #undef ISNAN
}
```

The `ISNAN` macro is defined inside `bessel_utils` where
a local `isnan()` template is in scope. The corresponding
`undefs.h` cleans up other macros but **not** `ISNAN`, so
it leaks out of the namespace. When Rcpp code later
expands `ISNAN(x)`, the bare `isnan()` call fails on GCC
because the namespaced function is no longer in scope and
C++17 does not guarantee `isnan` in the global namespace.

### 2. isNull / length macro clobber

TMB pulls in `R.h` / `Rinternals.h`, which define:

```cpp
#define isNull   Rf_isNull
#define length   Rf_length
```

When TMB is included first, these macros are active while
Rcpp's own headers are parsed. This rewrites Rcpp method
names during preprocessing -- e.g.,
`Rcpp::Nullable::isNull()` becomes
`Rcpp::Nullable::Rf_isNull()`, causing a signature
mismatch. This cannot be fixed by `#undef` after
`TMB.hpp` because the damage occurs during Rcpp's header
processing.

## Conditions that trigger the failure

### ISNAN (conflict 1)

All three must be true:

1. **GCC compiler** -- Apple clang puts `isnan` in the
   global namespace even under C++17
2. **C++17 or later** -- R >= 4.4 defaults to C++17
3. **TMB included before Rcpp**

### isNull (conflict 2)

Only one condition:

1. **TMB included before Rcpp** -- affects all compilers
   and all C++ standards

## Why CRAN does not catch this

TMB's own CRAN checks compile TMB in isolation -- the
package does not include Rcpp headers, so the macro
conflicts are never exposed.

## Why other downstream packages do not see this

Most TMB-dependent packages (e.g., glmmTMB) never
`#include` `Rcpp.h` in any source file. They use
`RcppEigen` in `LinkingTo` only for Eigen header paths.
Tests run through R via TMB's `MakeADFun()` interface
with no C++-level unit testing and no Rcpp exports, so
the macro conflicts are never exposed.

The bug surfaces in packages that use both TMB's C++
interface and Rcpp exports in the same `.cpp` file. TMB's
[documentation](https://kaskr.github.io/adcomp/_book/Tutorial.html)
shows `#include <TMB.hpp>` as the first include, which
means any downstream code that follows this pattern and
also uses Rcpp will hit the conflicts.

## Working include order

The only include order that works:

```cpp
#include <Rcpp.h>       // FIRST — compiles cleanly
#include <TMB.hpp>      // SECOND — re-exports R macros
#undef isNull           // clean up macro collisions
#undef length
```

Including Rcpp first lets its headers compile before TMB
re-exports the clashing R macros. The `#undef` lines
after TMB restore Rcpp method names for user code.

This contradicts TMB's documented include pattern but is
the only reliable workaround.

## Files in this reprex

| File | Include order | Exercises |
|------|--------------|-----------|
| `src/isnan_bug.cpp` | TMB then Rcpp | ISNAN + isNull |
| `src/reversed_includes.cpp` | Rcpp then TMB | ISNAN + isNull |

Both files call `besselK()` (triggers ISNAN) and use
`Rcpp::Nullable::isNull()` (triggers isNull conflict).

## CI results

The [diagnostic matrix](https://github.com/samueldnj/isnan_bugreprex/actions)
tests combinations of:
- 3 OS (Ubuntu, macOS, Windows)
- 4 R versions (4.3, 4.4, release, devel)
- 2 include orders (tmb-first, rcpp-first)
- 3 TMB sources (CRAN, bessel-fix, undefs-fix)

### CRAN TMB (unmodified)

| OS | tmb-first | rcpp-first |
|----|-----------|------------|
| **Ubuntu (GCC)** | FAIL | pass |
| **Windows (Rtools GCC)** | FAIL | pass |
| **macOS (clang)** | pass | pass |

tmb-first failures report
`'isnan' was not declared in this scope`.
macOS passes because clang is more permissive about
global-namespace math functions.

### bessel.hpp fix (`fix/isnan-bessel`)

Changes `#define ISNAN(x) (isnan(x)!=0)` to
`#define ISNAN(x) (std::isnan(asDouble(x))!=0)`.

| OS | tmb-first | rcpp-first |
|----|-----------|------------|
| **All** | **pass** | pass |

This fixes the ISNAN conflict because `std::isnan` is
globally qualified and does not depend on namespace
resolution. However, when tested on a real package
(LFRKernels) that exercises `Rcpp::Nullable`, the
isNull conflict still causes tmb-first to fail. The
reprex originally missed this because it did not exercise
any clashing Rcpp methods.

### undefs.h fix (`fix/isnan-undefs`)

Adds `#undef ISNAN` to `bessel/undefs.h`,
`beta/undefs.h`, and `gamma/undefs.h`.

| OS | tmb-first | rcpp-first |
|----|-----------|------------|
| **All** | **FAIL** | pass |

Removing the `ISNAN` macro entirely breaks downstream
code that depends on it.

### Key findings

- The ISNAN conflict boundary is **GCC vs clang**, not
  the C++ standard version — all R versions affected
- The isNull conflict affects **all compilers** when TMB
  is included first
- The only reliable solution is **Rcpp before TMB** with
  post-hoc `#undef` of clashing macros
- A compile-time `#error` guard between the includes
  catches ordering mistakes early
