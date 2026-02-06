# toonlite

<!-- badges: start -->
[![R-CMD-check](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

A fast, minimal-dependency R package for TOON data. TOON is a format designed for structured data that's both human-readable and efficient to parse.

## Why TOON?

JSON is verbose. YAML is ambiguous. CSV can't nest. TOON sits in the sweet spot:

| Feature | JSON | YAML | CSV | TOON |
|---------|------|------|-----|------|
| Human-readable | ✓ | ✓ | ✓ | ✓ |
| Comments | ✗ | ✓ | ✗ | ✓ |
| Explicit types | ✗ | ✗ | ✗ | ✓ |
| Efficient tabular | ✗ | ✗ | ✓ | ✓ |
| Nested structures | ✓ | ✓ | ✗ | ✓ |

TOON is particularly useful for LLM payloads, structured logs, and large tabular datasets where you want both readability and performance.

## Installation

```r
# Install from CRAN
install.packages("toonlite")

# Or install the development version from GitHub
# install.packages("devtools")
devtools::install_github("aljrico/toonlite")
```

## Quick start

```r
library(toonlite)

# Parse TOON
data <- from_toon('
name: "Alice"
age: 30
active: true
')
#> list(name = "Alice", age = 30L, active = TRUE)

# Serialize to TOON
to_toon(list(x = 1, y = 2))
#> x: 1.0
#> y: 2.0

# File I/O
write_toon(data, "config.toon")
data <- read_toon("config.toon")
```

## Tabular data

TOON's tabular syntax is compact and fast to parse:

```
[3]{name, age, score}:
  "Alice", 30, 95.5
  "Bob", 25, 87.0
  "Charlie", 35, 92.3
```

```r
# Write a data.frame
write_toon_df(mtcars[1:3, 1:4], "cars.toon")

# Read it back
df <- read_toon_df("cars.toon")

# Stream large files without loading into memory
toon_stream_rows("large.toon", batch_size = 10000,
  callback = function(batch) {
    # Process each batch
  }
)
```

## Format conversion

```r
# JSON (requires jsonlite)
toon <- json_to_toon('{"x": 1}')
json <- toon_to_json('x: 1')

# CSV
csv_to_toon("data.csv", "data.toon")
toon_to_csv("data.toon", "data.csv")

# Parquet (requires arrow)
toon_to_parquet("data.toon", "data.parquet")
```

## The format

**Objects** are key-value pairs:
```
name: "Alice"
age: 30
```

**Arrays** use a bracketed count with `- ` items:
```
colors: [3]:
  - "red"
  - "green"
  - "blue"
```

**Tabular** uses a header with fields, then rows:
```
[3]{name, age}:
  "Alice", 30
  "Bob", 25
  "Charlie", 35
```

**Comments** use `#` or `//`:
```
server: "localhost"  # default
port: 8080           // can override
```

## License

MIT
