#' @useDynLib toonlite, .registration = TRUE
NULL

.onLoad <- function(libname, pkgname) {
  # Package load hook
  # Currently nothing needed here
}

.onUnload <- function(libpath) {
  library.dynam.unload("toonlite", libpath)
}
