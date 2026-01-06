# toonlite

A minimal-dependency, performance-first R package for reading, writing, validating, streaming, and converting **TOON** (Token-Oriented Object Notation) data.

## What is TOON?

TOON is a human-readable data serialization format designed for structured data, with particular strengths for tabular data representation. It combines the readability of YAML with explicit typing and efficient tabular encoding.

### TOON Format Examples

**Objects** use `key: value` syntax:
```
name: "Alice"
age: 30
active: true
settings:
  theme: "dark"
  notifications: false
```

**Arrays** use `[N]:` header with `- ` prefixed items:
```
colors: [3]:
  - "red"
  - "green"
  - "blue"
```

**Tabular arrays** use `[N]{fields}:` for efficient data.frame representation:
```
[3]{name, age, score}:
  "Alice", 30, 95.5
  "Bob", 25, 87.0
  "Charlie", 35, 92.3
```

**Comments** are supported with `#` or `//`:
```
# Configuration file
server: "localhost"  # default server
port: 8080           // can be overridden
```

## When to use toonlite

- **LLM payloads**: Structured input/output for language models
- **Structured logs**: Machine-readable but human-inspectable log formats
- **Tabular data**: Large datasets with explicit schema
- **Configuration files**: Readable settings with comments

## Installation

```r
# Install from source
install.packages("toonlite", repos = NULL, type = "source")
```

## Quick Examples

### Basic I/O

```r
library(toonlite)

# Parse TOON from string
data <- from_toon('
name: "Alice"
age: 30
active: true
')
# Returns: list(name = "Alice", age = 30L, active = TRUE)

# Serialize to TOON
toon_str <- to_toon(list(x = 1, y = 2))
# Returns:
# x: 1.0
# y: 2.0

# Read/write files
data <- read_toon("config.toon")
write_toon(data, "output.toon")
```

### Tabular Data

```r
# Write data.frame as tabular TOON
write_toon_df(mtcars[1:3, 1:4], "cars.toon")
# Creates:
# [3]{mpg, cyl, disp, hp}:
#   21.0, 6, 160.0, 110
#   21.0, 6, 160.0, 110
#   22.8, 4, 108.0, 93

# Read tabular TOON as data.frame
df <- read_toon_df("cars.toon")

# Streaming for large files (memory-efficient)
toon_stream_rows("large.toon",
  callback = function(batch) {
    # Process each batch independently
    print(nrow(batch))
  },
  batch_size = 10000
)
```

### Validation

```r
# Validate TOON (returns TRUE/FALSE)
valid <- validate_toon('key: "value"')

# Assert validity (throws on error)
assert_toon('key: "value"')

# Format/pretty-print
formatted <- format_toon('key:1', indent = 2)
```

### Conversions

```r
# JSON <-> TOON (requires jsonlite)
toon <- json_to_toon('{"x": 1}')
json <- toon_to_json('x: 1')

# CSV <-> TOON
csv_to_toon("data.csv", "data.toon")
toon_to_csv("data.toon", "data.csv")

# Parquet/Feather (requires arrow)
toon_to_parquet("data.toon", "data.parquet")
parquet_to_toon("data.parquet", "data.toon")
```

## Features

- **Performance-first**: Optimized C++ core for large files
- **Minimal dependencies**: Base R + compiled C++ only
- **Tabular fast path**: Efficient data.frame I/O with `[N]{fields}:` syntax
- **Streaming**: Process files without loading into memory
- **Comments**: Supports `#` and `//` comments
- **Robust diagnostics**: Detailed error messages with line/column info
- **Flexible schema**: Handles ragged rows and type promotion

## API Reference

### Core I/O
- `from_toon()` / `to_toon()` - Parse/serialize strings
- `read_toon()` / `write_toon()` - File I/O

### Validation
- `validate_toon()` - Returns TRUE/FALSE with error details
- `assert_toon()` - Throws on invalid input
- `format_toon()` - Pretty-print TOON

### Tabular (data.frame)
- `read_toon_df()` / `write_toon_df()` - Tabular I/O
- `as_tabular_toon()` - Convert array-of-objects to tabular

### Streaming
- `toon_stream_rows()` - Stream tabular rows in batches
- `toon_stream_items()` - Stream array items
- `toon_stream_write_rows()` - Write rows incrementally

### Conversions
- `json_to_toon()` / `toon_to_json()` - JSON conversion (requires jsonlite)
- `csv_to_toon()` / `toon_to_csv()` - CSV conversion
- `toon_to_parquet()` / `parquet_to_toon()` - Parquet (requires arrow)
- `toon_to_feather()` / `feather_to_toon()` - Feather (requires arrow)

### Introspection
- `toon_peek()` - Preview file structure
- `toon_info()` - Get structure summary

## License

MIT
