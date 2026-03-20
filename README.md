# TMB ISNAN Macro Bug Reprex

Minimal reproducible example demonstrating a compilation failure
in packages that link to both TMB and Rcpp on Linux (GCC).

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
picks up TMB's broken `ISNAN` macro instead of R's.  For example,
`Rcpp/traits/is_infinite.h` line 36:

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

- TMB's own CRAN checks do not include Rcpp headers after the
  bessel headers, so the macro conflict is never exposed
- CRAN's Linux builders use GCC, but TMB itself compiles fine
  because the macro only disrupts *downstream* code
- Downstream CRAN packages that use both TMB and Rcpp
  (e.g., glmmTMB) may include Rcpp first, accidentally
  avoiding the bug

## Files in this reprex

| File | Include order | Expected result on GCC |
|------|--------------|----------------------|
| `src/isnan_bug.cpp` | TMB then Rcpp | **FAILS** |
| `src/reversed_includes.cpp` | Rcpp then TMB | passes |

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

## CI results

The GitHub Actions workflow runs four jobs on Ubuntu (GCC):

1. **tmb-before-rcpp** -- compiles `isnan_bug.cpp` (expected
   to fail)
2. **rcpp-before-tmb** -- compiles `reversed_includes.cpp`
   (expected to pass, demonstrating the include-order
   workaround)
3. **cran-check** -- runs `R CMD check --as-cran` (expected
   to fail)
4. **tmb-cran-check** -- runs `R CMD check --as-cran` on
   TMB itself (expected to pass, showing why CRAN does not
   catch the bug)

### R-hub results

R-hub checks on CRAN-equivalent platforms both fail:

- **linux (R-devel)** -- GCC with `-std=gnu++20`: ERROR
- **ubuntu-gcc12** (matches CRAN's
  `r-devel-linux-x86_64-debian-gcc`): ERROR

Both report `'isnan' was not declared in this scope` at
`bessel.hpp:60` when the macro expands inside Rcpp headers.
