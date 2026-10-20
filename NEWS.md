# toonlite 0.1.1

## Build

* Fixed compilation on macOS, where Apple's libc++ does not provide
  `std::from_chars()` for `double`. Doubles are now parsed with
  `std::from_chars()` only when the standard library advertises it
  (`__cpp_lib_to_chars`) and with `strtod()` otherwise, so libc++ builds on
  Linux keep working too.

## Bug fixes

* `toon_stream_write_rows()` formats cells exactly like `write_toon_df()`:
  control characters are escaped, factors are written as their labels instead
  of integer codes, and `Inf`/`NaN` are written as `null` instead of the
  invalid token `inf`. Streamed files now read back identically to
  `write_toon_df()` output.
* `read_toon_df()` and `toon_stream_rows()` decode quoted strings with the
  same rules as `read_toon()`, including `\uXXXX` escapes and strict-mode
  handling of invalid escapes.
* `read_toon_df()` and `toon_stream_rows()` no longer turn `-2147483648` into
  `NA`; like `read_toon()`, they read it as a double.
* `to_toon()` and `write_toon_df()` no longer fail in strict mode when a
  double vector or column contains `NA`; it is written as `null`. Strict mode
  still rejects `NaN` and `Inf`.
* `json_to_toon()` no longer warns that `allow_comments` has no effect.
* `toon_stream_rows()` now honours `strict` like `read_toon_df()` (tabs in
  indentation are an error by default) and applies `col_types` to every
  batch, not just the first.

## Deprecations

* `tabular` in `write_toon_df()`, `csv_to_toon()`, `parquet_to_toon()` and
  `feather_to_toon()` never changed the output. It is deprecated and warns
  when set to anything other than `TRUE`.
* `batch_size` in `toon_stream_write_rows()` was never passed to
  `row_source`. It is deprecated and warns when supplied.
* `allow_duplicate_keys` in `read_toon_df()` and `toon_stream_rows()` was
  never used. It is deprecated and warns when set to anything other than
  `TRUE`.

# toonlite 0.1.0

* Initial CRAN release.
