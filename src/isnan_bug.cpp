/*
 * Minimal reprex for TMB/Rcpp macro conflicts.
 *
 * This file includes TMB.hpp BEFORE Rcpp.h, which triggers
 * two classes of macro conflict:
 *
 * 1. ISNAN: TMB's bessel.hpp defines
 *        #define ISNAN(x) (isnan(x)!=0)
 *    inside namespace bessel_utils, where a local isnan()
 *    template is in scope.  The macro leaks out (undefs.h
 *    does not clean it up).  GCC rejects the bare isnan()
 *    call outside that namespace because C++17 does not
 *    guarantee isnan in the global namespace.
 *
 * 2. isNull / length: TMB pulls in R.h / Rinternals.h,
 *    which define:
 *        #define isNull  Rf_isNull
 *        #define length  Rf_length
 *    These rewrite Rcpp method names during preprocessing
 *    (e.g., Rcpp::Nullable::isNull() becomes
 *    Rcpp::Nullable::Rf_isNull()), causing signature
 *    mismatches that cannot be fixed by post-hoc #undef.
 */

// Prevent TMB from redefining the DLL init function
#define TMB_LIB_INIT R_init_isnanReprex_tmb

#include <TMB.hpp>

// Do NOT #undef isNull/length here — we want to
// demonstrate that TMB's R macros break Rcpp when
// Rcpp is included after TMB without cleanup.
#include <Rcpp.h>

// Now undef so our own code below compiles
#undef isNull
#undef length

// Force instantiation of besselK with AD type to exercise
// the ISNAN macro through the template code path.  Plain
// double may bypass the template isnan and use a compiler
// built-in instead.
// [[Rcpp::export]]
double test_besselK(double x, double nu) {
    return besselK(x, nu);
}

// Also try with AD<double> to exercise the template path
// that actually triggers the ISNAN bug
// [[Rcpp::export]]
double test_besselK_ad(double x, double nu) {
    AD<double> x_ad = x;
    AD<double> nu_ad = nu;
    AD<double> result = besselK(x_ad, nu_ad);
    return CppAD::Value(result);
}

// Exercise the isNull macro conflict: Rcpp::Nullable
// has an isNull() method that gets clobbered by R's
// #define isNull Rf_isNull when TMB is included first.
// [[Rcpp::export]]
double test_nullable(Rcpp::Nullable<Rcpp::NumericVector> x) {
    if (x.isNull()) return 0.0;
    Rcpp::NumericVector xv(x.get());
    return xv[0];
}

// Dummy objective function required by TMB
template<class Type>
Type objective_function<Type>::operator() ()
{
    return Type(0);
}
