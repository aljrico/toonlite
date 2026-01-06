#!/usr/bin/env Rscript
# Generate benchmark fixtures of various sizes
#
# Usage: Rscript generate_fixtures.R [output_dir]

library(toonlite)

args <- commandArgs(trailingOnly = TRUE)
output_dir <- if (length(args) > 0) args[1] else "bench_data"

if (!dir.exists(output_dir)) {
  dir.create(output_dir, recursive = TRUE)
}

cat("Generating benchmark fixtures in:", output_dir, "\n\n")

# Generate tabular data of various sizes
generate_tabular <- function(nrow, ncol, output_dir) {
  cat(sprintf("Generating tabular %d x %d... ", nrow, ncol))

  # Create data.frame with mixed types
  df <- data.frame(
    id = seq_len(nrow),
    name = paste0("item_", seq_len(nrow)),
    value = runif(nrow) * 1000,
    active = sample(c(TRUE, FALSE, NA), nrow, replace = TRUE),
    category = sample(LETTERS[1:5], nrow, replace = TRUE)
  )

  # Add more columns if needed
  if (ncol > 5) {
    for (i in 6:ncol) {
      col_name <- paste0("col", i)
      df[[col_name]] <- runif(nrow)
    }
  }

  filename <- sprintf("tabular_%dk_x_%d.toon", nrow / 1000, ncol)
  filepath <- file.path(output_dir, filename)

  start <- Sys.time()
  write_toon_df(df, filepath)
  elapsed <- as.numeric(Sys.time() - start, units = "secs")

  size_mb <- file.info(filepath)$size / 1024 / 1024
  cat(sprintf("%.2f MB, %.2f sec\n", size_mb, elapsed))

  invisible(filepath)
}

# Generate nested object data
generate_nested <- function(n_objects, depth, output_dir) {
  cat(sprintf("Generating nested %d objects, depth %d... ", n_objects, depth))

  create_nested <- function(d) {
    if (d <= 0) {
      return(list(value = runif(1), label = paste0("leaf_", sample(1000, 1))))
    }
    list(
      data = create_nested(d - 1),
      meta = list(level = d, timestamp = as.character(Sys.time()))
    )
  }

  data <- lapply(seq_len(n_objects), function(i) {
    list(
      id = i,
      name = paste0("object_", i),
      nested = create_nested(depth)
    )
  })
  names(data) <- paste0("obj", seq_len(n_objects))

  filename <- sprintf("nested_%d_depth%d.toon", n_objects, depth)
  filepath <- file.path(output_dir, filename)

  start <- Sys.time()
  write_toon(data, filepath)
  elapsed <- as.numeric(Sys.time() - start, units = "secs")

  size_mb <- file.info(filepath)$size / 1024 / 1024
  cat(sprintf("%.2f MB, %.2f sec\n", size_mb, elapsed))

  invisible(filepath)
}

# Generate array data
generate_array <- function(n_items, output_dir) {
  cat(sprintf("Generating array %d items... ", n_items))

  data <- list(
    items = lapply(seq_len(n_items), function(i) {
      list(id = i, value = runif(1), tag = sample(letters, 3, replace = TRUE))
    })
  )

  filename <- sprintf("array_%dk.toon", n_items / 1000)
  filepath <- file.path(output_dir, filename)

  start <- Sys.time()
  write_toon(data, filepath)
  elapsed <- as.numeric(Sys.time() - start, units = "secs")

  size_mb <- file.info(filepath)$size / 1024 / 1024
  cat(sprintf("%.2f MB, %.2f sec\n", size_mb, elapsed))

  invisible(filepath)
}

# Generate fixtures
cat("=== Tabular Fixtures ===\n")
generate_tabular(1000, 5, output_dir)
generate_tabular(10000, 5, output_dir)
generate_tabular(100000, 5, output_dir)
generate_tabular(1000000, 5, output_dir)
generate_tabular(100000, 20, output_dir)

cat("\n=== Nested Fixtures ===\n")
generate_nested(100, 5, output_dir)
generate_nested(1000, 3, output_dir)

cat("\n=== Array Fixtures ===\n")
generate_array(1000, output_dir)
generate_array(10000, output_dir)
generate_array(100000, output_dir)

cat("\nDone! Fixtures saved to:", output_dir, "\n")
