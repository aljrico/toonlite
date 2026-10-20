#' @useDynLib toonlite, .registration = TRUE
#' @importFrom utils read.csv write.csv
NULL

.onUnload <- function(libpath) {
  library.dynam.unload("toonlite", libpath)
}
