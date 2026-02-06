#' @useDynLib toonlite, .registration = TRUE
#' @importFrom utils read.csv write.csv
NULL

.onLoad <- function(libname, pkgname) {
  # Package load hook
  # Currently nothing needed here
}

.onUnload <- function(libpath) {
  library.dynam.unload("toonlite", libpath)
}
