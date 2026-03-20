# TMB ISNAN Macro Bug Reprex

Minimal reproducible example demonstrating a compilation failure
in packages that link to both TMB and Rcpp on GCC (Linux and
Windows).

## Motivation

We are developing an R package that refactors repeated C++
header functions from multiple fisheries stock assessment
models into a single shared source.  The goal is to reduce
code drift across projects and improve maintainability.

These shared functions (e.g., Baranov equation solvers,
composition likelihood kernels) are not TMB objective
functions -- they are standalone templates that use TMB types
such as `tmbutils::array<Type>` and `vector<Type>`.  Because
the functions have no `objective_function`, we cannot test
them through TMB's `MakeADFun()` interface.  Instead we
expose them to R via Rcpp exports and test directly with
`testthat`, which requires both `#include <TMB.hpp>` (for
the types) and `#include <Rcpp.h>` (for the exports) in the
same translation unit.

This include pattern triggers a compilation failure on GCC.

## The bug

TMB's `tiny_ad/bessel/bessel.hpp` redefines the `ISNAN` macro:

```cpp
// bessel.hpp line 48: #undef ISNAN
// bessel.hpp line 60:
#define ISNAN(x) (isnan(x)!=0)
```

This replaces R's own `ISNAN` macro with one that calls bare
`isnan()` (no `std::` prefix, no namespace qualification).

When Rcpp headers are included *after* TMB, Rcpp's internal code
picks up TMB's redefined `ISNAN` macro instead of R's.  For
example, `Rcpp/traits/is_infinite.h` line 36:

```cpp
return !( ISNAN(x) || R_FINITE(x) ) ;
```

This expands to `isnan(x)`, which GCC rejects because:

- C++17 does not guarantee `isnan` in the global namespace
  (it is only in `std::`)
- The `bessel_utils::isnan` template is not found by
  unqualified lookup inside `Rcpp::traits`

## Conditions that trigger the failure

All three must be true:

1. **GCC compiler** -- Apple clang is more permissive and
   puts `isnan` in the global namespace even under C++17
2. **C++17 or later** -- R >= 4.4 defaults to C++17, so
   this is now the default on all platforms
3. **TMB included before Rcpp** -- if Rcpp is included first,
   its headers are preprocessed with R's correct `ISNAN` macro
   before TMB redefines it

## Why CRAN does not catch this

TMB's own CRAN checks compile TMB in isolation -- the package
does not include Rcpp headers, so the macro conflict is never
exposed.

Downstream CRAN packages that depend on TMB (e.g.,
[glmmTMB](https://github.com/glmmTMB/glmmTMB)) avoid the bug
because they do not include `Rcpp.h` in the same translation
unit as `TMB.hpp`.  glmmTMB uses `RcppEigen` in `LinkingTo`
only for Eigen header paths, but never actually
`#include`s `Rcpp.h` in its source files.

The bug surfaces in packages that use both TMB's C++ interface
and Rcpp exports in the same `.cpp` file -- a less common but
legitimate pattern.  TMB's
[documentation](https://kaskr.github.io/adcomp/_book/Tutorial.html)
shows `#include <TMB.hpp>` as the first include, which means
any downstream code that follows this pattern and also uses
Rcpp will hit the bug on GCC.

## Workarounds

1. **Reverse the include order** -- `#include <Rcpp.h>` before
   `#include <TMB.hpp>`.  Rcpp's headers get preprocessed with
   R's correct `ISNAN` before TMB redefines it.  This is
   fragile and contradicts TMB's documented include pattern.

2. **Avoid mixing TMB and Rcpp in the same file** -- use
   separate translation units for TMB model code and Rcpp
   exports.  This is what glmmTMB does.

Neither workaround addresses the root cause.

## The fix

In `tiny_ad/bessel/bessel.hpp`, change:

```cpp
#define ISNAN(x) (isnan(x)!=0)
```

to:

```cpp
#define ISNAN(x) (std::isnan(asDouble(x))!=0)
```

This is strictly more correct and portable.  It matches the
pattern already used for `R_finite` at line 33 of the same
file and works on all compilers and C++ standards.

See: [adcomp commit 456bc479f](https://github.com/kaskr/adcomp/compare/master...samueldnj:adcomp:fix/isnan-bessel)

## Files in this reprex

| File | Include order | Expected on GCC |
|------|--------------|-----------------|
| `src/isnan_bug.cpp` | TMB then Rcpp | **FAILS** |
| `src/reversed_includes.cpp` | Rcpp then TMB | passes |

## CI results

The [diagnostic matrix](https://github.com/samueldnj/isnan_bugreprex/actions)
tests 24 combinations: 3 OS x 4 R versions x 2 include
orders.

### Matrix results

| OS | R 4.3 | R 4.4 | release | devel |
|----|-------|-------|---------|-------|
| **Ubuntu (GCC)** | FAIL / pass | FAIL / pass | FAIL / pass | FAIL / pass |
| **Windows (Rtools GCC)** | FAIL / pass | FAIL / pass | FAIL / pass | FAIL / pass |
| **macOS (clang)** | pass / pass | pass / pass | pass / pass | pass / pass |

Each cell shows: tmb-first / rcpp-first.

All failures report `'isnan' was not declared in this scope`
at `bessel.hpp:60`.

**Key findings:**

- The bug affects **all R versions** (4.3 through devel), not
  just R >= 4.4. The boundary is **GCC vs clang**, not the
  C++ standard version.
- **Both Ubuntu (GCC) and Windows (Rtools/MinGW GCC)** are
  affected.
- **macOS (clang)** is unaffected regardless of include order.
- The **include-order workaround** (Rcpp before TMB) works
  on all GCC platforms.

### R-hub results

[R-hub checks](https://github.com/samueldnj/isnan_bugreprex/actions/runs/23355366718)
on CRAN-equivalent platforms both fail:

- **linux (R-devel)** -- GCC with `-std=gnu++20`: ERROR
- **ubuntu-gcc12** (matches CRAN's
  `r-devel-linux-x86_64-debian-gcc`): ERROR

Both report `'isnan' was not declared in this scope` at
`bessel.hpp:60` when the macro expands inside Rcpp headers.
