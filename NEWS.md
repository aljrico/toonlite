# toonlite 0.2.0

toonlite now implements the [TOON specification v4.4.0](https://github.com/toon-format/spec)
and passes all 564 official conformance fixtures (404 decode, 160 encode),
which run in the test suite. No fixture is skipped.

## Breaking changes

Output is now canonical TOON, so most documents are written differently:

* Strings are quoted only when the spec requires it, and cells have no space
  after the delimiter: `1,Ada,true`, not `1, "Ada", true`.
* Numbers use the shortest text that reads back to the same double: `1`
  (not `1.0`), `0.1`, `1e-7`, `1e+21`.
* Atomic vectors are inline arrays (`tags[2]: a,b`). A length-1 vector is a
  primitive; wrap it in `I()` to write a one-element array.
* Lists of uniform records are written as tabular arrays, with nested field
  groups (`{id,geo{lat,lon}}`) for nested records or data frame columns, and
  objects of two or more uniform records as keyed tables (`key[N:]{...}:`).
* Empty arrays are `key: []` (`[]` at the root), empty objects `key:`.
* `NA`, `NaN` and `Inf` are always written as `null`.
* Documents never end with a newline. `to_toon()`, `write_toon()`,
  `write_toon_df()` and `toon_stream_write_rows()` produce the same bytes for
  the same data.
* `format_toon()` decodes and re-encodes, giving the canonical form.

Input follows the spec, and `strict = TRUE` (the default) means the spec's
strict mode:

* Only full-line `#` comments are comments. `//` lines and trailing
  `# ...` or `// ...` are content now, which usually makes them errors.
* Duplicate keys are an error (they used to warn, last one wins). With
  `strict = FALSE` the last one wins silently, keeping the first position.
* Declared lengths must match the items, rows or values found; mismatches
  used to warn. With `strict = FALSE` they are accepted with a warning
  (the first five per document), in `read_toon()` as in `read_toon_df()`.
* Blank lines inside arrays, tabs or uneven indentation, and lines indented
  more than one level deeper than their parent are errors. `strict = FALSE`
  applies the spec's five recoveries instead.
* List items need an array header (`items[2]:`); `- ` lines under a plain
  `key:` are no longer an array, and `key: [3]:` is not a header.
* Invalid escapes, unterminated strings, escaped surrogates and text after a
  closing quote are errors in both modes. Tabular rows of the wrong width are
  errors in both modes.
* Only tokens matching the JSON number grammar are numbers (`05`, `.5`, `1.`
  and `+1` are strings). Integers outside R's integer range are doubles, and
  numbers beyond the double range are `-Inf`/`Inf`.
* An empty object (`key:` with nothing under it, or an empty document) is a
  named list of length 0.

## New features

* Keyed tables (`key[N:]{fields}:`), nested field groups, root forms and
  every other v4.4 construct are read and written.
* `indent` (the spec's `indentSize`) for every reader and writer, and
  `delimiter` (`","`, `"\t"` or `"|"`) for every writer.
* `toon_stream_items()` now streams: items are decoded and handed to the
  callback one batch at a time, with memory reused between batches, instead
  of reading the whole file first. Tabular rows arrive as row objects.
* `read_toon_df()` and `toon_stream_rows()` read nested field groups into
  columns named by their dotted path (`geo.lat`) and accept `key: []`.
* `toon_info()` gains `strict` and `indent`.
* Writers stream to the file in chunks instead of building the text first.

## Deprecations

Deprecated arguments are ignored and warn; the 0.1.1 deprecations still warn.

* `allow_comments` in every reader and validator, and in `toon_peek()` and
  `toon_info()`: comment lines are always skipped. Warns unless `TRUE`.
* `allow_duplicate_keys` in `from_toon()`, `read_toon()`, `validate_toon()`,
  `assert_toon()` and `toon_stream_items()`: duplicates are governed by
  `strict`. Warns when supplied.
* `pretty` in `to_toon()`, `write_toon()`, `write_toon_df()` and
  `json_to_toon()`: `pretty = FALSE` produced invalid TOON. Warns unless
  `TRUE`.
* `strict` in the writers (`to_toon()`, `write_toon()`, `write_toon_df()`,
  `json_to_toon()` and the CSV, Parquet and Feather converters): `NaN` and
  `Inf` are always `null`. Warns when supplied.
* `allow_comments` in `to_toon()`: encoders never write comments. Warns when
  `TRUE`.

## Bug fixes

* Writers stop with an error beyond 1000 levels of nesting, the readers'
  limit, instead of writing documents that cannot be read back (they
  allowed 10000 levels).
* `validate_toon()` attached its `error` attribute to R's shared `FALSE`
  value, so unrelated `FALSE` results could carry it for the rest of the
  session.
* `read_toon_df()` no longer rounds numbers to 6 decimals when a column falls
  back to character; it keeps their canonical text.
* Named lists holding vectors or data frames, nested data frames, and empty
  lists were written as malformed TOON.
* Decoded strings and names are marked as UTF-8; invalid UTF-8 input is an
  error.

## Performance

* `read_toon()` stores values in a few flat arrays instead of one heap object
  per value, and tabular arrays keep their cells in one block.
* Doubles are formatted and parsed with exact integer arithmetic where the
  compiler supports 128-bit integers, falling back to `snprintf()` and
  `strtod()` elsewhere.

## Known deviations

These follow from R's data model; see the README.

* `\u0000` (and NUL bytes) cannot be represented in R strings and are an
  error when decoding.
* Integers are R integers between -2147483647 and 2147483647 and doubles
  otherwise, so integers beyond 2^53 lose precision.
* Nesting deeper than 1000 levels is an error, when encoding and when
  decoding.

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
