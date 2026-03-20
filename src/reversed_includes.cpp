/*
 * Same as isnan_bug.cpp but with Rcpp included BEFORE TMB.
 *
 * If the include order matters, this file should compile fine
 * because Rcpp's headers get preprocessed with R's correct
 * ISNAN macro before TMB's bessel.hpp redefines it.
 */

// Prevent TMB from redefining the DLL init function
#define TMB_LIB_INIT R_init_isnanReprex_reversed

#include <Rcpp.h>
#include <TMB.hpp>

// [[Rcpp::export]]
double test_besselK_reversed(double x, double nu) {
    return besselK(x, nu);
}
