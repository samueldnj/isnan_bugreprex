/*
 * Minimal reprex for the TMB ISNAN macro bug.
 *
 * TMB's tiny_ad/bessel/bessel.hpp defines (inside namespace
 * bessel_utils):
 *
 *   template<class T> int isnan(T x) {
 *       return std::isnan(asDouble(x));
 *   }
 *   ...
 *   #define ISNAN(x) (isnan(x)!=0)
 *
 * The macro expands to a bare isnan() call.  Under GCC with C++17,
 * this can fail because isnan is not guaranteed to be in the global
 * namespace — it lives only in std::.
 *
 * This file includes TMB.hpp and calls besselK(), which exercises
 * the ISNAN macro expansion path in bessel.hpp.  If the bug is
 * present, this file will fail to compile on GCC + C++17.
 */

// Prevent TMB from redefining the DLL init function
#define TMB_LIB_INIT R_init_isnanReprex_tmb

#include <TMB.hpp>

// Undefine R macros that clash with Rcpp
#undef isNull
#undef length

#include <Rcpp.h>

// Force instantiation of besselK with AD type to exercise the
// ISNAN macro through the template code path.  Plain double may
// bypass the template isnan and use a compiler built-in instead.
// [[Rcpp::export]]
double test_besselK(double x, double nu) {
    return besselK(x, nu);
}

// Also try with AD<double> to exercise the template path
// that actually triggers the bug
// [[Rcpp::export]]
double test_besselK_ad(double x, double nu) {
    // Create AD variables and evaluate
    AD<double> x_ad = x;
    AD<double> nu_ad = nu;
    AD<double> result = besselK(x_ad, nu_ad);
    return CppAD::Value(result);
}

// Dummy objective function required by TMB
template<class Type>
Type objective_function<Type>::operator() ()
{
    return Type(0);
}
