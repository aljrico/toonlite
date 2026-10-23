# toonlite <img src="man/figures/logo.svg" align="right" height="139" alt="toonlite logo" />

<!-- badges: start -->
[![CRAN status](https://www.r-pkg.org/badges/version/toonlite)](https://cran.r-project.org/package=toonlite)
[![R-CMD-check](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

**Fast, minimal-dependency R interface for [TOON](https://toonformat.dev/) data** — read, write, validate, stream, and convert with a C++ backend.

TOON is a compact, human-readable serialization format designed for passing structured data to LLMs with significantly reduced token usage.

## Why TOON?

JSON is verbose. YAML is ambiguous. CSV can't nest. TOON gives you readability, explicit types, nested structures, and comments, with significantly fewer tokens.

TOON is particularly useful for LLM payloads, structured logs, and large tabular datasets where you want both readability and performance.

## Installation

```r
# Install from CRAN
install.packages("toonlite")

# Or install the development version from GitHub
# install.packages("devtools")
devtools::install_github("aljrico/toonlite")
```

## Spec compatibility

Implements [TOON spec v4.4.0](https://github.com/toon-format/spec) and passes
564/564 official conformance fixtures (404 decode, 160 encode). Known
deviations, all from R's data model:

* `\u0000` (NUL) cannot be stored in R strings, so decoding it is an error.
* Integers are R integers between -2147483647 and 2147483647 and doubles
  otherwise; integers beyond 2^53 lose precision, numbers beyond the double
  range become `-Inf`/`Inf`.
* Encoding and decoding stop with an error beyond 1000 levels of nesting.
* `read_toon_df(ragged_rows = "expand_warn", n_mismatch = "warn")` are opt-in
  extensions that accept input the spec rejects.

## Quick start

```r
library(toonlite)

# Parse TOON
data <- from_toon("
name: Alice
age: 30
tags[2]: admin,dev
")
str(data)
#> List of 3
#>  $ name: chr "Alice"
#>  $ age : int 30
#>  $ tags: chr [1:2] "admin" "dev"

# Serialize to TOON
to_toon(list(x = 1, y = c(2.5, 3)))
#> x: 1
#> y[2]: 2.5,3

# File I/O
write_toon(data, "config.toon")
data <- read_toon("config.toon")
```

R values map to TOON as follows: named lists are objects, unnamed lists and
vectors of length other than 1 are arrays, length-1 vectors are primitives
(wrap in `I()` for a one-element array), data frames are tabular arrays, and
`NA`, `NaN` and `Inf` are `null`. Decoding returns named lists for objects
and, with `simplify = TRUE`, atomic vectors for arrays of one primitive type.

## Tabular data

TOON's tabular syntax declares the fields once:

```
users[3]{name,age,score}:
  Alice,30,95.5
  Bob,25,87
  Charlie,35,92.3
```

```r
# Write a data.frame
write_toon_df(mtcars[1:3, 1:4], "cars.toon")

# Read it back
df <- read_toon_df("cars.toon")

# Stream large files in batches of rows (data frames)...
toon_stream_rows("large.toon", batch_size = 10000,
  callback = function(batch) {
    # Process each batch
  }
)

# ...or the items of any array (lists)
toon_stream_items("events.toon", key = "events",
  callback = function(items) {
    # Process each batch
  }
)
```

## Format conversion

```r
# JSON (requires jsonlite)
toon <- json_to_toon('{"x": 1}')
json <- toon_to_json("x: 1")

# CSV
csv_to_toon("data.csv", "data.toon")
toon_to_csv("data.toon", "data.csv")

# Parquet (requires arrow)
toon_to_parquet("data.toon", "data.parquet")
```

## The format

**Objects** are key-value pairs; nesting is by indentation:
```
name: Alice
address:
  city: Paris
```

**Arrays** declare their length. Primitives go inline, anything else is a
list of `- ` items:
```
colors[3]: red,green,blue
mixed[3]:
  - 1
  - text
  - id: 7
    ok: true
```

**Tabular** arrays of uniform objects use a header with fields, then rows:
```
[3]{name,age}:
  Alice,30
  Bob,25
  Charlie,35
```

**Types**: strings (quoted only when needed), numbers, booleans, and null:
```
name: Alice
zip: "01234"
score: 95.5
active: true
nickname: null
```

**Comments** are whole lines starting with `#`:
```
# connection settings
server: localhost
port: 8080
```

For the full format specification, see [toonformat.dev](https://toonformat.dev/reference/spec.html).

## License

MIT
