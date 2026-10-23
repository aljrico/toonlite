## Update

* Fixes the installation ERROR on the macOS check flavors: 0.1.0 used
  `std::from_chars` for doubles, which Apple's libc++ does not provide. Double
  parsing now falls back to `strtod` where the standard library lacks it.
* Implements version 4.4 of the TOON specification and passes its official
  conformance fixtures. This changes the output format; the changes are listed
  in NEWS.md.

## Test environments

* local macOS (aarch64), R 4.5.1
* GitHub Actions: macOS, Windows (release); Ubuntu (devel, release, oldrel-1)

## R CMD check results

0 errors | 0 warnings | 0 notes
