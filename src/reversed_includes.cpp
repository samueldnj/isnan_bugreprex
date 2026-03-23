/*
 * Same tests as isnan_bug.cpp but with Rcpp included
 * BEFORE TMB — the only include order that works.
 *
 * Including Rcpp first means its headers compile cleanly
 * before TMB re-exports R macros (isNull, length, ISNAN).
 * After TMB.hpp, we #undef the clashing macros so our own
 * code can use Rcpp methods normally.
 */

// Prevent TMB from redefining the DLL init function
#define TMB_LIB_INIT R_init_isnanReprex_reversed

#include <Rcpp.h>
#include <TMB.hpp>

// Undefine R macros that TMB re-introduces via R.h;
// these clash with Rcpp methods
#undef isNull
#undef length

// [[Rcpp::export]]
double test_besselK_reversed(double x, double nu) {
    return besselK(x, nu);
}

// Exercise Rcpp::Nullable to confirm isNull works
// [[Rcpp::export]]
double test_nullable_reversed(
    Rcpp::Nullable<Rcpp::NumericVector> x
) {
    if (x.isNull()) return 0.0;
    Rcpp::NumericVector xv(x.get());
    return xv[0];
}
