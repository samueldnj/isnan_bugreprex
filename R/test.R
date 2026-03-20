#' Test besselK via TMB (exercises the ISNAN macro)
#' @param x numeric scalar
#' @param nu numeric scalar (order)
#' @export
test_besselK <- function(x, nu) {
    .Call("test_besselK", as.double(x), as.double(nu),
          PACKAGE = "isnanReprex")
}

#' Test besselK with AD types (exercises the template path)
#' @param x numeric scalar
#' @param nu numeric scalar (order)
#' @export
test_besselK_ad <- function(x, nu) {
    .Call("test_besselK_ad", as.double(x), as.double(nu),
          PACKAGE = "isnanReprex")
}
