# Benchmarks for toonlite against the CRAN `toon` package, jsonlite and CSV.
#
# Run from the package root with the package installed:
#
#   Rscript bench/run.R          # full run (about 10 minutes)
#   Rscript bench/run.R --quick  # small sizes, to check the script works
#
# Needs: toonlite, toon, jsonlite, data.table, bench. Token counts need `uv`
# (Python tiktoken, o200k_base); without it they are skipped.
#
# Writes bench/results.md (environment, all tables) and
# man/figures/bench-tabular.png. Times are medians from bench::mark(). Peak
# memory is the peak resident set size of a fresh R process running the
# operation, minus the same process without it (setup: packages loaded and,
# for writes, the data read from an .rds file), so allocations made in C/C++
# count too.

suppressPackageStartupMessages({
  library(toonlite)
  library(jsonlite)
  library(data.table)
  library(bench)
})
stopifnot(requireNamespace("toon", quietly = TRUE))

quick <- "--quick" %in% commandArgs(trailingOnly = TRUE)
tab_sizes <- if (quick) c(1e3, 1e4) else c(1e5, 1e6)
nested_sizes <- if (quick) c(1e3, 5e3) else c(1e4, 1e5)
token_rows <- 1000L
setDTthreads(0L)

dir <- file.path(tempdir(), "toonlite-bench")
dir.create(dir, showWarnings = FALSE)
path <- function(...) file.path(dir, paste0(...))
fmt_n <- function(n) format(n, big.mark = ",", scientific = FALSE, trim = TRUE)

# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------

with_na <- function(x, p = 0.05) {
  x[sample.int(length(x), round(length(x) * p))] <- NA
  x
}

make_tab <- function(n) {
  set.seed(42)
  cats <- c("north", "south", "east", "west", "central", "north, east",
            "online", "retail", "wholesale", "partner")
  data.frame(
    id = seq_len(n),
    x = rnorm(n),
    price = round(runif(n, 0, 1000), 2),
    qty = with_na(sample.int(100L, n, replace = TRUE)),
    category = with_na(sample(cats, n, replace = TRUE)),
    active = with_na(sample(c(TRUE, FALSE), n, replace = TRUE)),
    stringsAsFactors = FALSE
  )
}

make_nested <- function(n) {
  set.seed(42)
  words <- c("red", "green", "blue", "fast", "slow", "new", "old", "beta")
  ages <- sample(18:90, n, replace = TRUE)
  scores <- round(runif(n, 0, 100), 3)
  ntags <- sample(0:3, n, replace = TRUE)
  lapply(seq_len(n), function(i) list(
    id = i,
    user = list(name = paste0("user_", i), age = ages[i]),
    tags = sample(words, ntags[i]),
    score = scores[i]
  ))
}

# ---------------------------------------------------------------------------
# Formats: write(x, file) and read(file); NULL means unsupported
# ---------------------------------------------------------------------------

tab_formats <- list(
  toonlite = list(
    write = function(x, f) write_toon_df(x, f),
    read = function(f) read_toon_df(f)
  ),
  toon = list(
    write = function(x, f) writeLines(unclass(toon::as_toon(x)), f),
    read = NULL  # toon 0.0.1 has no decoder
  ),
  jsonlite = list(
    write = function(x, f) writeLines(toJSON(x, digits = NA), f),
    read = function(f) fromJSON(f)
  ),
  `data.table` = list(
    write = function(x, f) fwrite(x, f),
    read = function(f) fread(f, data.table = FALSE, na.strings = "")
  ),
  `base CSV` = list(
    write = function(x, f) write.csv(x, f, row.names = FALSE),
    read = function(f) read.csv(f, stringsAsFactors = FALSE)
  )
)

nested_formats <- list(
  toonlite = list(
    write = function(x, f) write_toon(x, f),
    read = function(f) read_toon(f)
  ),
  toon = list(write = NULL, read = NULL),  # output is not valid TOON, see below
  jsonlite = list(
    write = function(x, f) writeLines(toJSON(x, auto_unbox = TRUE, digits = NA), f),
    read = function(f) fromJSON(f, simplifyVector = FALSE)
  )
)

ext <- c(toonlite = "toon", toon = "toon", jsonlite = "json",
         `data.table` = "csv", `base CSV` = "csv")

# Same code as the format table, for the peak-memory subprocesses.
fmt_code <- list(
  tab = list(
    toonlite = c("toonlite::write_toon_df(x, f)", "toonlite::read_toon_df(f)"),
    toon = c("writeLines(unclass(toon::as_toon(x)), f)", NA),
    jsonlite = c("writeLines(jsonlite::toJSON(x, digits = NA), f)", "jsonlite::fromJSON(f)"),
    `data.table` = c("data.table::fwrite(x, f)",
                     "data.table::fread(f, data.table = FALSE, na.strings = '')"),
    `base CSV` = c("write.csv(x, f, row.names = FALSE)",
                   "read.csv(f, stringsAsFactors = FALSE)")
  ),
  nested = list(
    toonlite = c("toonlite::write_toon(x, f)", "toonlite::read_toon(f)"),
    jsonlite = c("writeLines(jsonlite::toJSON(x, auto_unbox = TRUE, digits = NA), f)",
                 "jsonlite::fromJSON(f, simplifyVector = FALSE)")
  )
)

# ---------------------------------------------------------------------------
# Equivalence checks
# ---------------------------------------------------------------------------

# Compare a data frame read back with the original, column by column.
tab_diff <- function(orig, got) {
  got <- as.data.frame(got)
  if (!identical(names(got), names(orig)) || nrow(got) != nrow(orig)) {
    return("different shape")
  }
  notes <- character()
  for (nm in names(orig)) {
    a <- orig[[nm]]
    b <- got[[nm]]
    if (is.numeric(a) && is.numeric(b)) {
      if (!identical(is.na(a), is.na(b))) {
        notes <- c(notes, paste0(nm, ": NA differ"))
      } else if (!isTRUE(all.equal(as.double(a), as.double(b), tolerance = 0))) {
        err <- max(abs(a - b) / pmax(abs(a), 1e-300), na.rm = TRUE)
        notes <- c(notes, sprintf("%s: rel. error %.0e", nm, err))
      } else if (typeof(a) != typeof(b)) {
        notes <- c(notes, paste0(nm, ": ", typeof(b)))
      }
    } else if (!identical(a, b)) {
      notes <- c(notes, paste0(nm, ": values differ"))
    }
  }
  if (length(notes) == 0) "exact" else paste(notes, collapse = "; ")
}

# Compare in the JSON data model (character(0) and list() are both []).
nested_json <- function(x) toJSON(x, auto_unbox = TRUE, digits = NA, null = "null")
nested_diff <- function(orig, got) {
  if (identical(nested_json(orig), nested_json(got))) "exact" else "values differ"
}

# Check that the CRAN toon package's output decodes to the input (it has no
# decoder, so toonlite reads it; toonlite passes the spec's fixtures).
toon_tab_check <- function(x) {
  f <- path("check-toon.toon")
  tab_formats$toon$write(x, f)
  got <- tryCatch(read_toon_df(f), error = function(e) NULL)
  if (is.null(got)) "not valid TOON" else tab_diff(x, got)
}
toon_nested_check <- function(x) {
  one <- function(x) {
    txt <- tryCatch(unclass(toon::as_toon(x)), error = function(e) e)
    if (inherits(txt, "error")) return(paste0("as_toon() errors (", conditionMessage(txt), ")"))
    got <- tryCatch(from_toon(txt), error = function(e) NULL)
    if (is.null(got)) return("output is not valid TOON")
    if (identical(nested_json(got), nested_json(x))) "exact" else "output decodes to different values"
  }
  all <- one(x)
  # Records without empty tag vectors, in case those are the only problem
  some <- one(Filter(function(r) length(r$tags) > 0, x))
  if (identical(all, some)) all else paste0(all, "; without empty tags: ", some)
}

# ---------------------------------------------------------------------------
# Timing and peak memory
# ---------------------------------------------------------------------------

time_it <- function(fun, n_iter) {
  m <- bench::mark(fun(), iterations = n_iter, check = FALSE, memory = FALSE,
                   filter_gc = FALSE, time_unit = "s")
  as.numeric(m$median)
}

rss_cmd <- function() {
  if (Sys.info()[["sysname"]] == "Darwin") c("-l") else c("-v")
}

# Peak RSS (MB) of `Rscript -e code`.
peak_rss <- function(code) {
  script <- tempfile(fileext = ".R")
  writeLines(code, script)
  out <- suppressWarnings(system2("/usr/bin/time", c(rss_cmd(), "Rscript", script),
                                  stdout = TRUE, stderr = TRUE))
  line <- grep("maximum resident set size|Maximum resident set size", out, value = TRUE)
  if (length(line) == 0) return(NA_real_)
  v <- as.numeric(gsub("[^0-9]", "", line[1]))
  # macOS reports bytes, GNU time kilobytes
  if (Sys.info()[["sysname"]] == "Darwin") v / 2^20 else v / 2^10
}

mem_delta <- function(setup, op) {
  base <- min(peak_rss(setup), peak_rss(setup))
  peak_rss(c(setup, op)) - base
}

setup_code <- function(pkg, rds = NULL) {
  c(sprintf("suppressPackageStartupMessages(library(%s))", pkg),
    if (pkg == "data.table") "data.table::setDTthreads(0L)",
    if (!is.null(rds)) sprintf("x <- readRDS('%s')", rds))
}

pkg_of <- c(toonlite = "toonlite", toon = "toon", jsonlite = "jsonlite",
            `data.table` = "data.table", `base CSV` = "utils")

run_case <- function(kind, x, n, formats, iters) {
  rds <- path(kind, "-", n, ".rds")
  saveRDS(x, rds, compress = FALSE)
  rows <- list()
  for (fmt in names(formats)) {
    fm <- formats[[fmt]]
    f <- path(kind, "-", n, "-", gsub("[^a-z]", "", fmt), ".", ext[[fmt]])
    code <- fmt_code[[kind]][[fmt]]
    for (op in c("write", "read")) {
      fun <- fm[[op]]
      if (is.null(fun)) {
        rows[[length(rows) + 1]] <- data.frame(
          data = kind, n = n, format = fmt, op = op, time = NA, mem = NA,
          check = "unsupported", stringsAsFactors = FALSE)
        next
      }
      if (op == "read" && !file.exists(f)) fm$write(x, f)
      # toon's encoder is slow; one run is enough at the large size
      it <- if (fmt == "toon" && n > 1e5) 1L else iters
      message(sprintf("  %s %s %s ...", kind, fmt, op))
      t <- if (op == "write") time_it(function() fun(x, f), it) else time_it(function() fun(f), it)
      check <- if (op == "write") {
        if (fmt == "toon") {
          if (kind == "tab") toon_tab_check(x) else toon_nested_check(x)
        } else {
          "-"
        }
      } else {
        got <- fun(f)
        if (kind == "tab") tab_diff(x, got) else nested_diff(x, got)
      }
      op_code <- sprintf("f <- '%s'; invisible(%s)", f, code[[if (op == "write") 1 else 2]])
      setup <- setup_code(pkg_of[[fmt]], if (op == "write") rds)
      mem <- if (fmt == "toon" && n > 1e5) NA else mem_delta(setup, op_code)
      rows[[length(rows) + 1]] <- data.frame(
        data = kind, n = n, format = fmt, op = op, time = t, mem = mem,
        check = check, stringsAsFactors = FALSE)
    }
  }
  do.call(rbind, rows)
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

results <- list()
sizes <- list()

for (n in tab_sizes) {
  message("Tabular, ", fmt_n(n), " rows")
  x <- make_tab(n)
  results[[length(results) + 1]] <- run_case("tab", x, n, tab_formats,
                                             if (n >= 1e6) 3L else 5L)
}

for (n in nested_sizes) {
  message("Nested, ", fmt_n(n), " records")
  x <- make_nested(n)
  # Show the toon package's nested output is not valid, rather than hide it.
  toon_nested <- toon_nested_check(x)
  results[[length(results) + 1]] <- run_case("nested", x, n, nested_formats,
                                             if (n >= 1e5) 3L else 5L)
}
res <- do.call(rbind, results)
res$check[res$data == "nested" & res$format == "toon"] <-
  paste("unsupported:", toon_nested)

# File sizes, from the files written above
size_row <- function(kind, n, fmt) {
  f <- path(kind, "-", n, "-", gsub("[^a-z]", "", fmt), ".", ext[[fmt]])
  if (!file.exists(f)) {
    x <- if (kind == "tab") make_tab(n) else make_nested(n)
    if (fmt == "toon" && kind == "tab") tab_formats$toon$write(x, f)
  }
  if (file.exists(f)) file.size(f) else NA
}
size_tab <- do.call(rbind, lapply(names(tab_formats), function(fmt) data.frame(
  data = "tab", n = max(tab_sizes), format = fmt, bytes = size_row("tab", max(tab_sizes), fmt))))
size_nested <- do.call(rbind, lapply(names(nested_formats), function(fmt) data.frame(
  data = "nested", n = max(nested_sizes), format = fmt,
  bytes = if (fmt == "toon") NA else size_row("nested", max(nested_sizes), fmt))))

# ---------------------------------------------------------------------------
# Streaming vs full read (peak memory)
# ---------------------------------------------------------------------------

message("Streaming")
n_big <- max(tab_sizes)
f_tab <- path("tab-", n_big, "-toonlite.toon")
n_nest <- max(nested_sizes)
f_nest <- path("nested-", n_nest, "-toonlite.toon")
stream_cases <- list(
  list(what = sprintf("Tabular, %s rows", fmt_n(n_big)),
       how = "read_toon_df()",
       code = sprintf("df <- toonlite::read_toon_df('%s'); s <- sum(df$price)", f_tab)),
  list(what = sprintf("Tabular, %s rows", fmt_n(n_big)),
       how = "toon_stream_rows(), batches of 10,000",
       code = sprintf("s <- 0; toonlite::toon_stream_rows('%s', batch_size = 10000, callback = function(b) s <<- s + sum(b$price))", f_tab)),
  list(what = sprintf("Nested, %s records", fmt_n(n_nest)),
       how = "read_toon()",
       code = sprintf("x <- toonlite::read_toon('%s'); s <- sum(vapply(x, function(r) r$score, 0))", f_nest)),
  list(what = sprintf("Nested, %s records", fmt_n(n_nest)),
       how = "toon_stream_items(), batches of 1,000",
       code = sprintf("s <- 0; toonlite::toon_stream_items('%s', batch_size = 1000, callback = function(b) s <<- s + sum(vapply(b, function(r) r$score, 0)))", f_nest))
)
stream <- do.call(rbind, lapply(stream_cases, function(sc) {
  t <- time_it(function() eval(parse(text = sc$code), envir = new.env()), 3L)
  data.frame(data = sc$what, method = sc$how, time = t,
             mem = mem_delta(setup_code("toonlite"), sc$code),
             stringsAsFactors = FALSE)
}))

# ---------------------------------------------------------------------------
# Tokens (o200k_base) for a 1,000-row / 1,000-record sample
# ---------------------------------------------------------------------------

message("Tokens")
tok_dir <- path("tokens")
dir.create(tok_dir, showWarnings = FALSE)
tab_s <- make_tab(token_rows)
nest_s <- make_nested(token_rows)
tok_files <- c(
  "tab|toonlite" = "tab-toonlite.toon", "tab|toon" = "tab-toon.toon",
  "tab|jsonlite" = "tab-json.json", "tab|data.table" = "tab-csv.csv",
  "nested|toonlite" = "nested-toonlite.toon", "nested|jsonlite" = "nested-json.json"
)
tab_formats$toonlite$write(tab_s, file.path(tok_dir, tok_files[["tab|toonlite"]]))
tab_formats$toon$write(tab_s, file.path(tok_dir, tok_files[["tab|toon"]]))
tab_formats$jsonlite$write(tab_s, file.path(tok_dir, tok_files[["tab|jsonlite"]]))
tab_formats$`data.table`$write(tab_s, file.path(tok_dir, tok_files[["tab|data.table"]]))
nested_formats$toonlite$write(nest_s, file.path(tok_dir, tok_files[["nested|toonlite"]]))
nested_formats$jsonlite$write(nest_s, file.path(tok_dir, tok_files[["nested|jsonlite"]]))

py <- tempfile(fileext = ".py")
writeLines(c(
  "import sys, tiktoken",
  "enc = tiktoken.get_encoding('o200k_base')",
  "for p in sys.argv[1:]:",
  "    with open(p, encoding='utf-8') as fh:",
  "        print(len(enc.encode(fh.read())))"
), py)
tok_paths <- file.path(tok_dir, tok_files)
tok_out <- tryCatch(
  suppressWarnings(system2("uv", c("run", "--quiet", "--with", "tiktoken", "python", "-I", py,
                                   shQuote(tok_paths)), stdout = TRUE, stderr = FALSE)),
  error = function(e) character()
)
tokens_ok <- length(tok_out) == length(tok_paths) && !anyNA(suppressWarnings(as.integer(tok_out)))
tokens <- data.frame(
  data = sub("\\|.*", "", names(tok_files)),
  format = sub(".*\\|", "", names(tok_files)),
  bytes = file.size(tok_paths),
  tokens = if (tokens_ok) as.integer(tok_out) else NA_integer_,
  stringsAsFactors = FALSE
)
tokens$format[tokens$format == "data.table"] <- "CSV (fwrite)"

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

fmt_time <- function(t) ifelse(is.na(t), "-", ifelse(t < 1, sprintf("%.0f ms", t * 1000),
                                                    sprintf("%.2f s", t)))
fmt_mem <- function(m) ifelse(is.na(m), "-", sprintf("%.0f MB", pmax(m, 0)))
fmt_bytes <- function(b) ifelse(is.na(b), "-", sprintf("%.1f MB", b / 1e6))

md_table <- function(df) {
  c(paste("|", paste(names(df), collapse = " | "), "|"),
    paste("|", paste(rep("---", ncol(df)), collapse = " | "), "|"),
    apply(df, 1, function(r) paste("|", paste(r, collapse = " | "), "|")))
}

si <- Sys.info()
cpu <- tryCatch(system("sysctl -n machdep.cpu.brand_string", intern = TRUE),
                error = function(e) "unknown CPU")
ram <- tryCatch(as.numeric(system("sysctl -n hw.memsize", intern = TRUE)) / 2^30,
                error = function(e) NA)
pkgs <- c("toonlite", "toon", "jsonlite", "data.table", "bench")
pkg_versions <- paste(sprintf("%s %s", pkgs, vapply(pkgs, function(p)
  as.character(packageVersion(p)), "")), collapse = ", ")
env <- c(
  sprintf("- Date: %s", format(Sys.Date())),
  sprintf("- Machine: %s, %s GB RAM, %s %s", cpu, ifelse(is.na(ram), "?", format(ram)),
          si[["sysname"]], si[["release"]]),
  sprintf("- %s", R.version.string),
  sprintf("- Packages: %s", pkg_versions),
  sprintf("- data.table threads: %d", getDTthreads()),
  if (tokens_ok) "- Tokens: tiktoken o200k_base" else "- Tokens: skipped (uv/tiktoken unavailable)"
)

op_table <- function(kind, n) {
  r <- res[res$data == kind & res$n == n, ]
  w <- r[r$op == "write", ]
  rd <- r[r$op == "read", ]
  data.frame(
    Format = w$format,
    Write = fmt_time(w$time), `Write mem` = fmt_mem(w$mem),
    Read = fmt_time(rd$time), `Read mem` = fmt_mem(rd$mem),
    `Read back` = ifelse(w$format == "toon", w$check, rd$check),
    check.names = FALSE, stringsAsFactors = FALSE
  )
}

out <- c("# toonlite benchmarks", "",
         "Generated by `Rscript bench/run.R`.", "", env, "",
         "Data: a data frame of int, double (full precision), double (2 dp), int,",
         "character and logical columns, 5% NA in the last three; and a list of",
         "records `{id, user: {name, age}, tags: [0-3 strings], score}`. Seed 42.",
         "Times are medians; memory is peak RSS above a fresh R session that has",
         "the package loaded (and, for writes, the data). \"Read back\" compares",
         "what each format reads back with the original; for `toon`, which has",
         "no reader, its output is decoded with toonlite.", "")
for (n in tab_sizes) {
  out <- c(out, sprintf("## Tabular, %s rows", fmt_n(n)), "", md_table(op_table("tab", n)), "")
}
for (n in nested_sizes) {
  out <- c(out, sprintf("## Nested records, %s items", fmt_n(n)), "",
           md_table(op_table("nested", n)), "")
}
out <- c(out, "## Streaming vs full read", "",
         md_table(data.frame(Data = stream$data, Method = stream$method,
                             Time = fmt_time(stream$time), `Peak mem` = fmt_mem(stream$mem),
                             check.names = FALSE)), "",
         "## Output size", "",
         md_table(data.frame(
           Data = c(rep(sprintf("Tabular, %s rows", fmt_n(max(tab_sizes))), nrow(size_tab)),
                    rep(sprintf("Nested, %s items", fmt_n(max(nested_sizes))), nrow(size_nested))),
           Format = c(size_tab$format, size_nested$format),
           Size = fmt_bytes(c(size_tab$bytes, size_nested$bytes)))), "",
         sprintf("## Tokens, %s rows / items", fmt_n(token_rows)), "",
         md_table(data.frame(
           Data = ifelse(tokens$data == "tab", "Tabular", "Nested"),
           Format = tokens$format, Bytes = fmt_n(tokens$bytes),
           Tokens = ifelse(is.na(tokens$tokens), "-", fmt_n(tokens$tokens)))), "",
         "JSON is jsonlite's compact output (`toJSON(digits = NA)`; NA fields are",
         "omitted). CSV cannot hold the nested records.")
writeLines(out, "bench/results.md")
saveRDS(list(res = res, stream = stream, tokens = tokens, sizes = rbind(size_tab, size_nested),
             env = env), file.path(dir, "results.rds"))

# ---------------------------------------------------------------------------
# Chart: read and write time, largest tabular size
# ---------------------------------------------------------------------------

r <- res[res$data == "tab" & res$n == max(tab_sizes), ]
fmts <- r$format[r$op == "write"]
tw <- r$time[r$op == "write"]
tr <- r$time[r$op == "read"]
ord <- order(tw)
fmts <- fmts[ord]
tw <- tw[ord]
tr <- tr[ord]
cols <- c(write = "#2a78d6", read = "#eb6834")
lo <- 10^floor(log10(min(c(tw, tr), na.rm = TRUE)))
hi <- 10^ceiling(log10(max(c(tw, tr), na.rm = TRUE)))
ticks <- 10^seq(log10(lo), log10(hi))
tick_lab <- ifelse(ticks < 1, paste(ticks * 1000, "ms"), paste(ticks, "s"))
dir.create("man/figures", showWarnings = FALSE, recursive = TRUE)
png("man/figures/bench-tabular.png", width = 1600, height = 160 * length(fmts) + 260, res = 200)
op <- par(mar = c(3, 6.5, 3.2, 1), family = "sans")
k <- length(fmts)
plot(NA, xlim = log10(c(lo, hi)), ylim = c(0.4, k + 0.6), axes = FALSE, xlab = "", ylab = "")
abline(v = log10(ticks), col = "#e6e6e6")
axis(1, at = log10(ticks), labels = tick_lab, col = NA, col.axis = "#555555", cex.axis = 0.8)
axis(2, at = rev(seq_len(k)), labels = fmts, las = 1, col = NA, col.axis = "#222222", cex.axis = 0.85)
h <- 0.3
for (i in seq_len(k)) {
  y <- k + 1 - i
  rect(log10(lo), y + 0.02, log10(tw[i]), y + 0.02 + h, col = cols[["write"]], border = NA)
  if (is.na(tr[i])) {
    text(log10(lo), y - 0.02 - h / 2, "  no reader", adj = 0, cex = 0.7, col = "#666666")
  } else {
    rect(log10(lo), y - 0.02 - h, log10(tr[i]), y - 0.02, col = cols[["read"]], border = NA)
  }
}
legend("topright", legend = c("write", "read"), fill = cols, border = NA, bty = "n", cex = 0.8)
title(main = sprintf("Median time, %s-row data frame (log scale)", fmt_n(max(tab_sizes))),
      adj = 0, font.main = 1, cex.main = 0.95, col.main = "#222222", line = 1.5)
par(op)
invisible(dev.off())

message("Wrote bench/results.md and man/figures/bench-tabular.png")
