#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

/* Declarations */
extern SEXP C_from_toon(SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_read_toon(SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_to_toon(SEXP, SEXP, SEXP, SEXP);
extern SEXP C_validate_toon(SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_read_toon_df(SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_write_toon_df(SEXP, SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_stream_rows(SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_format_toon(SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_toon_peek(SEXP, SEXP, SEXP);
extern SEXP C_toon_info(SEXP, SEXP);
extern SEXP C_from_toon_df(SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP, SEXP);
extern SEXP C_stream_write_init(SEXP, SEXP, SEXP);
extern SEXP C_stream_write_batch(SEXP, SEXP);
extern SEXP C_stream_write_close(SEXP);

static const R_CallMethodDef CallEntries[] = {
    {"C_from_toon",          (DL_FUNC) &C_from_toon,          5},
    {"C_read_toon",          (DL_FUNC) &C_read_toon,          5},
    {"C_to_toon",            (DL_FUNC) &C_to_toon,            4},
    {"C_validate_toon",      (DL_FUNC) &C_validate_toon,      5},
    {"C_read_toon_df",       (DL_FUNC) &C_read_toon_df,       10},
    {"C_write_toon_df",      (DL_FUNC) &C_write_toon_df,      6},
    {"C_stream_rows",        (DL_FUNC) &C_stream_rows,        12},
    {"C_format_toon",        (DL_FUNC) &C_format_toon,        5},
    {"C_toon_peek",          (DL_FUNC) &C_toon_peek,          3},
    {"C_toon_info",          (DL_FUNC) &C_toon_info,          2},
    {"C_from_toon_df",       (DL_FUNC) &C_from_toon_df,       10},
    {"C_stream_write_init",  (DL_FUNC) &C_stream_write_init,  3},
    {"C_stream_write_batch", (DL_FUNC) &C_stream_write_batch, 2},
    {"C_stream_write_close", (DL_FUNC) &C_stream_write_close, 1},
    {NULL, NULL, 0}
};

void R_init_toonlite(DllInfo *dll) {
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
