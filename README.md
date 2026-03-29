# libnemo_normalize

A standalone C library for NeMo text normalization (TN) and inverse text normalization (ITN) via OpenFst. No Python at runtime -- loads pre-exported `.far` grammar files and runs entirely on CPU.

Supports **19 languages** across 28 grammar sets (12 TN + 16 ITN).

## Table of Contents

- [What it does](#what-it-does)
- [Supported Languages](#supported-languages)
- [Architecture](#architecture)
- [Files](#files)
- [Quick Start (Docker)](#quick-start)
  - [Prerequisites](#prerequisites)
  - [Step 1: Export FAR Grammar Files](#step-1-export-far-grammar-files-one-time)
  - [Step 2: Build the Library and Test Program](#step-2-build-the-library-and-test-program)
  - [Step 3: Run the Tests](#step-3-run-the-tests)
- [Building with Autotools (native)](#building-with-autotools-native)
  - [Prerequisites](#autotools-prerequisites)
  - [Bootstrap](#bootstrap)
  - [Configure](#configure)
  - [Build](#build)
  - [Test](#test)
  - [Install](#install)
  - [Verify Installation](#verify-installation)
  - [LTO Compatibility Note](#lto-compatibility-note)
  - [Cross-Compilation](#cross-compilation)
- [FAR Grammar Data Files](#far-grammar-data-files)
- [Debian Packaging](#debian-packaging)
- [C API Reference](#c-api-reference)
  - [Header: nemo_normalize.h](#header-nemo_normalizeh)
  - [Usage Example: English TN](#usage-example-english-text-normalization-tn)
  - [Usage Example: English ITN](#usage-example-english-inverse-text-normalization-itn)
  - [Usage Example: Multi-Language](#usage-example-multi-language)
  - [Compiling Your Program](#compiling-your-program)
- [FAR Export Details](#far-export-details)
  - [How It Works](#how-it-works)
  - [Export Matrix](#export-matrix)
  - [FAR File Contents](#far-file-contents)
  - [Selective Export](#selective-export)
- [Test Cases (English)](#test-cases-english)
  - [Text Normalization (TN)](#text-normalization-tn----151-cases)
  - [Inverse Text Normalization (ITN)](#inverse-text-normalization-itn----88-cases)
- [How It Works](#how-it-works-1)
  - [Pipeline](#pipeline)
  - [Key Implementation Details](#key-implementation-details)
- [Build Details (Docker)](#build-details)
- [Performance](#performance)
- [License](#license)

## What it does

**Text Normalization (TN)** converts written text to spoken form (for TTS):

| Input | Output |
|-------|--------|
| `$2,450.99` | `two thousand four hundred and fifty dollars ninety nine cents` |
| `Dr. Johnson prescribed 500 mg` | `doctor Johnson prescribed five hundred milligrams` |
| `Flight UA2491 departs at 6:45 AM` | `Flight UA two thousand four hundred ninety one departs at six forty five AM` |
| `Contact help@company.com` | `Contact help at company dot com` |
| `The 3rd quarter earnings of $4.7 billion` | `The third quarter earnings of four point seven billion dollars` |
| `350°F for 25 minutes` | `three hundred and fifty degrees Fahrenheit for twenty five minutes` |

**Inverse Text Normalization (ITN)** converts spoken form back to written text (for ASR):

| Input | Output |
|-------|--------|
| `two thousand four hundred and fifty dollars ninety nine cents` | `$2450.99` |
| `doctor Johnson prescribed five hundred milligrams` | `dr. Johnson prescribed 500 mg` |
| `six forty five a m` | `06:45 a.m.` |
| `events at nvidia dot com` | `events@nvidia.com` |
| `one thousand four hundred and fifty four feet` | `1454 ft` |
| `five hundred and sixty two kilometers` | `562 km` |

Both directions handle full sentences with mixed semiotic classes -- numbers, money, dates, times, emails, phones, addresses, measures, ordinals, and more.

## Supported Languages

19 languages are supported. Each language may support TN, ITN, or both:

| Language | Code | TN | ITN | Post-processing |
|----------|------|:--:|:---:|:---------------:|
| English | `en` | Y | Y | TN |
| German | `de` | Y | Y | |
| Spanish | `es` | Y | Y | |
| French | `fr` | Y | Y | |
| Hindi | `hi` | Y | Y | TN |
| Hungarian | `hu` | Y | | |
| Italian | `it` | Y | | |
| Japanese | `ja` | Y | Y | TN, ITN |
| Kinyarwanda | `rw` | Y | | |
| Swedish | `sv` | Y | Y | |
| Vietnamese | `vi` | Y | Y | TN |
| Mandarin Chinese | `zh` | Y | Y | TN |
| Arabic | `ar` | | Y | |
| Armenian | `hy` | | Y | |
| Hebrew | `he` | | Y | |
| Marathi | `mr` | | Y | |
| Portuguese | `pt` | | Y | |
| Russian | `ru` | | Y | |
| Spanish-English | `es_en` | | Y | |

**Totals:** 12 TN languages, 16 ITN languages, 28 grammar sets.

The same C API works for every language -- just load the appropriate FAR files for that language.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Your C program                                              │
│    NemoNormalizer *tn = nemo_normalizer_create(...)          │
│    nemo_normalize(tn, "cdf@abc.edu", buf, sizeof(buf))       │
│    // buf = "cdf at abc dot edu"                             │
└──────────────┬───────────────────────────────────────────────┘
               │ extern "C" API
┌──────────────▼───────────────────────────────────────────────┐
│  libnemo_normalize.so  (C++ compiled, C API exposed)         │
│                                                              │
│  1. Compile input string → FST (StringCompiler, byte mode)   │
│  2. Compose with tagger FST → shortest path → tagged text    │
│  3. Parse tagged text (recursive descent parser)             │
│  4. Generate permutations of token orderings                 │
│  5. Compose with verbalizer FST → shortest path → output     │
│  6. Optional post-processing FST                             │
└──────────────────────────────────────────────────────────────┘
               │ links against
    ┌──────────┴──────────┐
    │  libfst  libfstfar  │  (OpenFst C++ libraries via conda)
    │  libthrax           │
    └─────────────────────┘
               │ loads at runtime
    ┌──────────┴──────────┐
    │  .far grammar files │  (exported once from Python)
    └─────────────────────┘
```

The same C API is used for both TN and ITN, and for all languages -- you just load different FAR files.

## Files

| File | Purpose |
|------|---------|
| `nemo_normalize.h` | Public C API header (3 functions) |
| `nemo_normalize.cpp` | C++ implementation (~680 lines) |
| `test_normalize.c` | Pure C test program (239 English test cases) |
| `configure.ac` | Autoconf input (OpenFst detection, C++17 check) |
| `Makefile.am` | Automake input (library, test, data install rules) |
| `nemo_normalize.pc.in` | pkg-config template |
| `Makefile` | Legacy build rules (Docker workflow) |
| `Dockerfile` | Docker build environment (OpenFst + Thrax via conda) |
| `export_far.sh` | One-time script to export FAR files for all languages |
| `debian/` | Debian packaging files |

## Quick Start

> This section covers the **Docker-based** workflow for exporting grammars and running tests in an isolated environment. For building natively on your host, see [Building with Autotools (native)](#building-with-autotools-native).

### Prerequisites

- Docker
- The `nemo-tn-test` Docker image (built from the NeMo-text-processing repo Dockerfile)
- The NeMo-text-processing repo cloned at `../NeMo-text-processing/`

### Step 1: Export FAR Grammar Files (one-time)

The FAR files contain the compiled finite-state grammars. This step runs Python inside Docker to produce them.

Export **all languages**:

```bash
bash export_far.sh
```

Or export **specific languages only**:

```bash
bash export_far.sh en de fr    # Just English, German, French
bash export_far.sh en          # Just English (fastest)
```

This exports grammars to `far_export/` with one subdirectory per language+direction:

```
far_export/
├── en_tn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/
│       ├── verbalize.far
│       └── post_process.far
├── en_itn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/verbalize.far
├── de_tn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/verbalize.far
├── de_itn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/verbalize.far
├── es_tn_grammars_cased/
│   └── ...
├── fr_tn_grammars_cased/
│   └── ...
├── ...
├── pt_itn_grammars_cased/       # Portuguese: ITN only
│   └── ...
├── ru_itn_grammars_cased/       # Russian: ITN only
│   └── ...
└── rw_tn_grammars_cased/        # Kinyarwanda: TN only
    └── ...
```

The naming convention is `{lang}_{tn|itn}_grammars_cased/`.

### Step 2: Build the Library and Test Program

```bash
docker build -t nemo-c-test .
```

This installs OpenFst + Thrax via conda, compiles `libnemo_normalize.so` and the test program.

### Step 3: Run the Tests

```bash
docker run --rm nemo-c-test
```

You should see all 239 tests pass:

```
NeMo Text Processing - C Library Test Suite (TN + ITN)
======================================================================

Loading TN normalizer...
  TN normalizer ready in 156ms

Loading ITN normalizer...
  ITN normalizer ready in 52ms

  ...

======================================================================
  OVERALL SUMMARY
======================================================================
  Tests passed:          239
  Tests failed:          0
  Total test cases:      239 (151 TN + 88 ITN)
  Avg per normalization: 18.1ms
  Throughput:            55.1 normalizations/sec
======================================================================
```

## Building with Autotools (native)

The project uses a standard autotools build system (`configure.ac` + `Makefile.am`) so you can build and install natively without Docker.

### Autotools Prerequisites

Install the build toolchain and OpenFst development headers:

**Debian / Ubuntu:**

```bash
sudo apt install autoconf automake libtool pkg-config g++ libfst-dev
```

**From source / conda:** If your distribution does not package `libfst-dev`, you can install OpenFst from source or via conda (`conda install -c conda-forge thrax=1.3.4`), then point configure at it with `--with-openfst-prefix=DIR`.

A **C++17-capable compiler** is required (GCC 7+ or Clang 5+).

### Bootstrap

Generate the configure script from `configure.ac`:

```bash
autoreconf -fi
```

This creates `configure`, `Makefile.in`, and the supporting `build-aux/` and `m4/` scaffolding.

### Configure

```bash
./configure
```

Configure auto-detects OpenFst via pkg-config. If OpenFst is installed in a non-standard location (e.g., under a conda prefix), specify it explicitly:

```bash
./configure --with-openfst-prefix=/opt/conda
```

If `libfst-dev` is installed system-wide but does not ship a `.pc` file (common on Debian), configure falls back to direct header and library detection automatically.

### Build

```bash
make
```

This compiles `libnemo_normalize.so` (via libtool) and the `test_normalize` test binary.

### Test

```bash
make check
```

(Or the convenience alias `make test`.)

This runs `test_normalize` against the FAR grammar files in `far_export/`. The FAR files must be present in the source tree for tests to pass -- see [FAR Grammar Data Files](#far-grammar-data-files).

### Install

```bash
sudo make install
```

This installs:

| Artifact | Default location |
|----------|-----------------|
| `libnemo_normalize.so` | `/usr/local/lib/` |
| `nemo_normalize.h` | `/usr/local/include/` |
| `nemo_normalize.pc` | `/usr/local/lib/pkgconfig/` |
| FAR grammar data | `/usr/local/share/nemo-normalize/far_export/` |

### Verify Installation

After installing, confirm that pkg-config can find the library:

```bash
pkg-config --cflags --libs nemo_normalize
```

Expected output (paths may vary):

```
-I/usr/local/include -L/usr/local/lib -lnemo_normalize
```

### LTO Compatibility Note

Link-Time Optimization (LTO) must be **disabled** when building against OpenFst. OpenFst's static symbols cause relocation errors on aarch64 (and potentially other architectures) when LTO merges translation units. The Debian packaging rules explicitly set `optimize=-lto` for this reason. If you are building manually with custom `CXXFLAGS`, avoid passing `-flto`.

### Cross-Compilation

Standard autotools cross-compilation works. Specify a `--host` triplet and ensure a cross-toolchain plus cross-built OpenFst libraries are available:

```bash
./configure --host=aarch64-linux-gnu --with-openfst-prefix=/path/to/aarch64-sysroot/usr
make
```

The Debian packaging also supports cross-building via `dpkg-buildpackage -a<arch>` (e.g., `-aarm64`).

## FAR Grammar Data Files

The compiled FAR (Finite-State Archive) files contain all language-specific grammar logic. They are produced once from the Python NeMo-text-processing pipeline (see [FAR Export Details](#far-export-details)) and then used at runtime by the C library.

When installed via `make install`, the FAR files are placed under:

```
<prefix>/share/nemo-normalize/far_export/
```

The directory structure mirrors the export layout:

```
far_export/
├── en_tn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/
│       ├── verbalize.far
│       └── post_process.far
├── en_itn_grammars_cased/
│   ├── classify/tokenize_and_classify.far
│   └── verbalize/verbalize.far
├── de_tn_grammars_cased/
│   └── ...
└── ... (28 grammar sets total)
```

Each grammar set contains:

| File | Purpose |
|------|---------|
| `classify/tokenize_and_classify.far` | Tagger FST -- segments and classifies input tokens |
| `verbalize/verbalize.far` | Verbalizer FST -- converts classified tokens to output form |
| `verbalize/post_process.far` | Post-processing FST (only present for select languages: en, hi, vi, zh, ja for TN; ja for ITN) |

When using the Debian packages, the `libnemo-normalize-data` package installs the FAR files to `/usr/share/nemo-normalize/far_export/`.

## Debian Packaging

Two Debian packages are produced from this source:

| Package | Contents |
|---------|----------|
| `libnemo-normalize` | Shared library (`.so`), header, pkg-config file |
| `libnemo-normalize-data` | FAR grammar data files (arch-independent) |

The library package depends on the data package at the same source version.

### Building Debian Packages

From the source directory (with `far_export/` present):

```bash
# Install build dependencies
sudo apt install debhelper dh-autoreconf autoconf automake libtool \
                 pkg-config g++ libfst-dev

# Build binary packages (unsigned)
dpkg-buildpackage -us -uc -b
```

The resulting `.deb` files are placed in the parent directory:

```
../libnemo-normalize_0.1.0_<arch>.deb
../libnemo-normalize-data_0.1.0_all.deb
```

Install them with:

```bash
sudo dpkg -i ../libnemo-normalize_0.1.0_*.deb ../libnemo-normalize-data_0.1.0_all.deb
sudo apt-get -f install   # resolve any missing dependencies
```

### Package Details

- Tests are skipped during package build (`override_dh_auto_test`) because the FAR files are split into the separate `-data` package and are not available at library test time.
- LTO is explicitly disabled in `debian/rules` (`DEB_BUILD_MAINT_OPTIONS = optimize=-lto`) for OpenFst compatibility. See [LTO Compatibility Note](#lto-compatibility-note).
- The `-data` package is `Architecture: all` since the FAR files are platform-independent binary grammars.

## C API Reference

### Header: `nemo_normalize.h`

```c
#include "nemo_normalize.h"

/* Opaque normalizer handle */
typedef struct NemoNormalizer NemoNormalizer;

/* Create a normalizer by loading FAR grammar files.
 * classify_far_path:     Path to tokenize_and_classify.far
 * verbalize_far_path:    Path to verbalize.far
 * post_process_far_path: Path to post_process.far (NULL to skip)
 * Returns: normalizer handle, or NULL on failure. */
NemoNormalizer *nemo_normalizer_create(
    const char *classify_far_path,
    const char *verbalize_far_path,
    const char *post_process_far_path
);

/* Normalize a text string.
 * Returns 0 on success, -1 on error. */
int nemo_normalize(
    NemoNormalizer *norm,
    const char *input,
    char *output,
    int output_max_len
);

/* Free all resources. */
void nemo_normalizer_destroy(NemoNormalizer *norm);
```

### Usage Example: English Text Normalization (TN)

```c
#include <stdio.h>
#include "nemo_normalize.h"

int main() {
    NemoNormalizer *tn = nemo_normalizer_create(
        "far_export/en_tn_grammars_cased/classify/tokenize_and_classify.far",
        "far_export/en_tn_grammars_cased/verbalize/verbalize.far",
        "far_export/en_tn_grammars_cased/verbalize/post_process.far"
    );
    if (!tn) return 1;

    char buf[4096];
    int rc = nemo_normalize(tn, "$42,990", buf, sizeof(buf));
    if (rc == 0)
        printf("TN: %s\n", buf);
    // Output: "forty two thousand nine hundred and ninety dollars"

    nemo_normalizer_destroy(tn);
    return 0;
}
```

### Usage Example: English Inverse Text Normalization (ITN)

```c
#include <stdio.h>
#include "nemo_normalize.h"

int main() {
    /* ITN uses different FAR files, no post-processing */
    NemoNormalizer *itn = nemo_normalizer_create(
        "far_export/en_itn_grammars_cased/classify/tokenize_and_classify.far",
        "far_export/en_itn_grammars_cased/verbalize/verbalize.far",
        NULL
    );
    if (!itn) return 1;

    char buf[4096];
    int rc = nemo_normalize(itn, "forty two thousand nine hundred and ninety dollars", buf, sizeof(buf));
    if (rc == 0)
        printf("ITN: %s\n", buf);
    // Output: "$42990"

    nemo_normalizer_destroy(itn);
    return 0;
}
```

### Usage Example: Multi-Language

```c
#include <stdio.h>
#include "nemo_normalize.h"

int main() {
    char buf[4096];

    /* German TN */
    NemoNormalizer *de_tn = nemo_normalizer_create(
        "far_export/de_tn_grammars_cased/classify/tokenize_and_classify.far",
        "far_export/de_tn_grammars_cased/verbalize/verbalize.far",
        NULL  /* no post-processing for German */
    );
    if (de_tn) {
        nemo_normalize(de_tn, "42", buf, sizeof(buf));
        printf("German TN: %s\n", buf);
        nemo_normalizer_destroy(de_tn);
    }

    /* French ITN */
    NemoNormalizer *fr_itn = nemo_normalizer_create(
        "far_export/fr_itn_grammars_cased/classify/tokenize_and_classify.far",
        "far_export/fr_itn_grammars_cased/verbalize/verbalize.far",
        NULL
    );
    if (fr_itn) {
        nemo_normalize(fr_itn, "quarante deux", buf, sizeof(buf));
        printf("French ITN: %s\n", buf);
        nemo_normalizer_destroy(fr_itn);
    }

    /* Japanese TN (has post-processing) */
    NemoNormalizer *ja_tn = nemo_normalizer_create(
        "far_export/ja_tn_grammars_cased/classify/tokenize_and_classify.far",
        "far_export/ja_tn_grammars_cased/verbalize/verbalize.far",
        "far_export/ja_tn_grammars_cased/verbalize/post_process.far"
    );
    if (ja_tn) {
        nemo_normalize(ja_tn, "42", buf, sizeof(buf));
        printf("Japanese TN: %s\n", buf);
        nemo_normalizer_destroy(ja_tn);
    }

    return 0;
}
```

### Compiling Your Program

```bash
gcc -o my_program my_program.c -L. -lnemo_normalize -Wl,-rpath,.
```

At runtime, `libnemo_normalize.so` must be in the library search path, along with `libfst`, `libfstfar`, and `libthrax` (provided by conda in the Docker container).

## FAR Export Details

### How It Works

The `export_far.sh` script runs `pynini_export.py` from the NeMo-text-processing repo inside the `nemo-tn-test` Docker container. For each language, it calls:

```bash
# TN grammars (for languages that support TN)
python pynini_export.py --output_dir=/workspace/far_output \
    --language=LANG --grammars=tn_grammars --input_case=cased

# ITN grammars (for languages that support ITN)
python pynini_export.py --output_dir=/workspace/far_output \
    --language=LANG --grammars=itn_grammars --input_case=cased
```

### Export Matrix

The script exports these grammar sets:

| Language | TN Export | ITN Export |
|----------|-----------|------------|
| en (English) | `en_tn_grammars_cased/` | `en_itn_grammars_cased/` |
| de (German) | `de_tn_grammars_cased/` | `de_itn_grammars_cased/` |
| es (Spanish) | `es_tn_grammars_cased/` | `es_itn_grammars_cased/` |
| fr (French) | `fr_tn_grammars_cased/` | `fr_itn_grammars_cased/` |
| hi (Hindi) | `hi_tn_grammars_cased/` | `hi_itn_grammars_cased/` |
| hu (Hungarian) | `hu_tn_grammars_cased/` | -- |
| it (Italian) | `it_tn_grammars_cased/` | -- |
| ja (Japanese) | `ja_tn_grammars_cased/` | `ja_itn_grammars_cased/` |
| rw (Kinyarwanda) | `rw_tn_grammars_cased/` | -- |
| sv (Swedish) | `sv_tn_grammars_cased/` | `sv_itn_grammars_cased/` |
| vi (Vietnamese) | `vi_tn_grammars_cased/` | `vi_itn_grammars_cased/` |
| zh (Chinese) | `zh_tn_grammars_cased/` | `zh_itn_grammars_cased/` |
| ar (Arabic) | -- | `ar_itn_grammars_cased/` |
| es_en (Spanish-English) | -- | `es_en_itn_grammars_cased/` |
| he (Hebrew) | -- | `he_itn_grammars_cased/` |
| hy (Armenian) | -- | `hy_itn_grammars_cased/` |
| mr (Marathi) | -- | `mr_itn_grammars_cased/` |
| pt (Portuguese) | -- | `pt_itn_grammars_cased/` |
| ru (Russian) | -- | `ru_itn_grammars_cased/` |

**Total: 28 grammar sets** (12 TN + 16 ITN).

### FAR File Contents

Each exported directory contains compiled finite-state transducers:

| FAR File | Rule Name | Purpose |
|----------|-----------|---------|
| `classify/tokenize_and_classify.far` | `TOKENIZE_AND_CLASSIFY` | Tagger |
| `verbalize/verbalize.far` | `ALL` | Verbalizer |
| `verbalize/post_process.far` | `POSTPROCESSOR` | Post-processing (some languages only) |

Post-processing FARs are generated automatically for languages that have them (en, hi, vi, zh, ja for TN; ja for ITN). Pass the post-process path as the third argument to `nemo_normalizer_create()`, or `NULL` to skip.

### Selective Export

To save time, export only the languages you need:

```bash
bash export_far.sh en           # English only (~2 min)
bash export_far.sh en de fr es  # Four languages (~8 min)
bash export_far.sh              # All 19 languages (~30+ min)
```

The script reports success/failure for each language and provides a summary at the end.

## Test Cases (English)

The test suite covers 239 English cases across 24 test sections. Every test shows `"input" -> "output"` so you can see exactly what the normalizer produces.

### Text Normalization (TN) -- 151 cases

#### Email (13 cases)

| Input | Output |
|-------|--------|
| `cdf@abc.edu` | `cdf at abc dot edu` |
| `abc@gmail.com` | `abc at gmail dot com` |
| `abs@nvidia.com` | `abs at NVIDIA dot com` |
| `asdf123@abc.com` | `asdf one two three at abc dot com` |
| `a1b2@abc.com` | `a one b two at abc dot com` |
| `ab3.sdd.3@gmail.com` | `ab three dot sdd dot three at gmail dot com` |
| `enterprise-services@nvidia.com` | `enterprise dash services at NVIDIA dot com` |
| `nvidia.com` | `nvidia dot com` |
| `test.com` | `test dot com` |
| `http://www.ourdailynews.com.sm` | `HTTP colon slash slash WWW dot ourdailynews dot com dot SM` |
| `https://www.ourdailynews.com.sm` | `HTTPS colon slash slash WWW dot ourdailynews dot com dot SM` |
| `www.ourdailynews.com/123-sm` | `WWW dot ourdailynews dot com slash one two three dash SM` |
| `email is abc1@gmail.com` | `email is abc one at gmail dot com` |

#### Phone (12 cases)

| Input | Output |
|-------|--------|
| `+1 123-123-5678` | `plus one, one two three, one two three, five six seven eight` |
| `123-123-5678` | `one two three, one two three, five six seven eight` |
| `+1-123-123-5678` | `plus one, one two three, one two three, five six seven eight` |
| `+1 (123)-123-5678` | `plus one, one two three, one two three, five six seven eight` |
| `(123) 123-5678` | `one two three, one two three, five six seven eight` |
| `123-123-5678-1111` | `one two three, one two three, five six seven eight, one one one one` |
| `1-800-GO-U-HAUL` | `one, eight hundred, GO U HAUL` |
| `123.123.0.40` | `one two three dot one two three dot zero dot four zero` |
| `111-11-1111` | `one one one, one one, one one one one` |
| `call me at +1 123-123-5678` | `call me at plus one, one two three, one two three, five six seven eight` |
| `555.555.5555` | `five five five, five five five, five five five five` |
| `(555)555-5555` | `five five five, five five five, five five five five` |

#### Address (8 cases)

| Input | Output |
|-------|--------|
| `2788 San Tomas Expy, Santa Clara, CA 95051` | `twenty seven eighty eight San Tomas Expressway, Santa Clara, California nine five zero five one` |
| `2 San Tomas hwy, Santa, FL, 95051` | `two San Tomas Highway, Santa, Florida, nine five zero five one` |
| `123 Smth St, City, NY` | `one twenty three Smth Street, City, New York` |
| `123 Laguna Ct, Town` | `one twenty three Laguna Court, Town` |
| `1211 E Arques Ave` | `twelve eleven East Arques Avenue` |
| `708 N 1st St, San City` | `seven zero eight North first Street, San City` |
| `12 S 1st st` | `twelve South first Street` |
| `Nancy lived at 1428 Elm St. It was a strange place.` | `Nancy lived at fourteen twenty eight Elm Street. It was a strange place.` |

#### Cardinal (19 cases)

| Input | Output |
|-------|--------|
| `1` | `one` |
| `12` | `twelve` |
| `123` | `one hundred and twenty three` |
| `1234` | `twelve thirty four` |
| `100` | `one hundred` |
| `1000` | `one thousand` |
| `21` | `twenty one` |
| `99` | `ninety nine` |
| `101` | `one hundred and one` |
| `999` | `nine hundred and ninety nine` |
| `1001` | `one thousand one` |
| `0` | `zero` |
| `-1` | `minus one` |
| `-42` | `minus forty two` |
| `-100` | `minus one hundred` |
| `1,000` | `one thousand` |
| `1,000,000` | `one million` |
| `10,500` | `ten thousand five hundred` |
| `999,999` | `nine hundred ninety nine thousand nine hundred and ninety nine` |

#### Ordinal (15 cases)

| Input | Output |
|-------|--------|
| `1st` | `first` |
| `2nd` | `second` |
| `3rd` | `third` |
| `4th` | `fourth` |
| `5th` | `fifth` |
| `10th` | `tenth` |
| `11th` | `eleventh` |
| `12th` | `twelfth` |
| `13th` | `thirteenth` |
| `21st` | `twenty first` |
| `22nd` | `twenty second` |
| `23rd` | `twenty third` |
| `100th` | `one hundredth` |
| `101st` | `one hundred first` |
| `1000th` | `one thousandth` |

#### Money (13 cases)

| Input | Output |
|-------|--------|
| `$1` | `one dollar` |
| `$10` | `ten dollars` |
| `$100` | `one hundred dollars` |
| `$1000` | `one thousand dollars` |
| `$1,000,000` | `one million dollars` |
| `$1.50` | `one dollar fifty cents` |
| `$99.99` | `ninety nine dollars ninety nine cents` |
| `$0.99` | `ninety nine cents` |
| `$0.01` | `one cent` |
| `$1.5 million` | `one point five million dollars` |
| `$3.2 billion` | `three point two billion dollars` |
| `€100` | `one hundred euros` |
| `£50` | `fifty pounds` |

#### Date (9 cases)

| Input | Output |
|-------|--------|
| `Jan. 1, 2020` | `january first, twenty twenty` |
| `February 14, 2023` | `february fourteenth, twenty twenty three` |
| `March 3rd, 1999` | `march third, nineteen ninety nine` |
| `Dec 25, 2000` | `december twenty fifth, two thousand` |
| `01/01/2020` | `january first twenty twenty` |
| `12/31/1999` | `december thirty first nineteen ninety nine` |
| `2020-01-15` | `january fifteenth twenty twenty` |
| `1990` | `nineteen ninety` |
| `2000` | `two thousand` |

#### Time (7 cases)

| Input | Output |
|-------|--------|
| `1:00 a.m.` | `one AM` |
| `12:30 p.m.` | `twelve thirty PM` |
| `3:45 PM` | `three forty five PM` |
| `11:59 AM` | `eleven fifty nine AM` |
| `12:00` | `twelve o'clock` |
| `0:00` | `zero o'clock` |
| `23:59` | `twenty three fifty nine` |

#### Measure (8 cases)

| Input | Output |
|-------|--------|
| `100 kg` | `one hundred kilograms` |
| `5.5 km` | `five point five kilometers` |
| `3 ft` | `three feet` |
| `10 lbs` | `ten pounds` |
| `72°F` | `seventy two degrees Fahrenheit` |
| `100 mph` | `one hundred miles per hour` |
| `50 cm` | `fifty centimeters` |
| `2.5 GHz` | `two point five gigahertz` |

#### Decimal & Fraction (10 cases)

| Input | Output |
|-------|--------|
| `0.5` | `zero point five` |
| `3.14` | `three point one four` |
| `100.001` | `one hundred point zero zero one` |
| `-2.5` | `minus two point five` |
| `0.001` | `zero point zero zero one` |
| `1/2` | `one half` |
| `3/4` | `three quarters` |
| `1/3` | `one third` |
| `2/3` | `two thirds` |
| `7/8` | `seven eighths` |

#### Mixed Sentences (20 cases)

| Input | Output |
|-------|--------|
| `I have $5 and 3 apples.` | `I have five dollars and three apples.` |
| `The meeting is at 3:00 p.m. on Jan. 15, 2024.` | `The meeting is at three PM on january fifteenth, twenty twenty four.` |
| `She ran 5 km in 23 minutes.` | `She ran five kilometers in twenty three minutes.` |
| `The temperature is 72°F today.` | `The temperature is seventy two degrees Fahrenheit today.` |
| `He weighs 180 lbs and is 6 ft tall.` | `He weighs one hundred and eighty pounds and is six feet tall.` |
| `Call 1-800-555-0199 for more info.` | `Call one, eight hundred, five five five, zero one nine nine for more info.` |
| `Visit us at 123 Main St, Apt 4B.` | `Visit us at one twenty three Main Street, Apt four B.` |
| `The price dropped from $100 to $79.99.` | `The price dropped from one hundred dollars to seventy nine dollars ninety nine cents.` |
| `Flight AA123 departs at 6:45 AM.` | `Flight AA one hundred twenty three departs at six forty five AM.` |
| `Born on July 4, 1776.` | `Born on july fourth, seventeen seventy six.` |
| `Pi is approximately 3.14159.` | `Pi is approximately three point one four one five nine.` |
| `The recipe calls for 3/4 cup of sugar.` | `The recipe calls for three quarters cup of sugar.` |
| `Dr. Smith lives at 42 Oak Ave.` | `doctor Smith lives at forty two Oak Avenue` |
| `The year 2000 was a leap year.` | `The year two thousand was a leap year.` |
| `We need 2.5 GHz processor.` | `We need two point five gigahertz processor.` |
| `The 1st and 2nd floors are closed.` | `The first and second floors are closed.` |
| `I owe you $3.50.` | `I owe you three dollars fifty cents.` |
| `The concert is on 12/25/2024.` | `The concert is on december twenty fifth twenty twenty four.` |
| `He scored 99 out of 100.` | `He scored ninety nine out of one hundred.` |
| `Add 1,000 to the total.` | `Add one thousand to the total.` |

#### Long Sentences (17 cases)

| Input | Output |
|-------|--------|
| `The invoice total is $2,450.99 and payment is due by March 15, 2025.` | `The invoice total is two thousand four hundred and fifty dollars ninety nine cents and payment is due by march fifteenth, twenty twenty five.` |
| `Dr. Johnson prescribed 500 mg of ibuprofen to be taken 3 times daily for 7 days.` | `doctor Johnson prescribed five hundred milligrams of ibuprofen to be taken three times daily for seven days.` |
| `Flight UA2491 departs from gate B12 at 6:45 AM and arrives at 11:30 p.m.` | `Flight UA two thousand four hundred ninety one departs from gate B twelve at six forty five AM and arrives at eleven thirty PM` |
| `The property at 1842 N Highland Ave, Los Angeles, CA 90028 sold for $1.2 million.` | `The property at eighteen forty two North Highland Avenue, Los Angeles, California nine zero zero two eight sold for one point two million dollars.` |
| `Contact support at help@company.com or call +1 800-555-0123 for assistance.` | `Contact support at help at company dot com or call plus one, eight hundred, five five five, zero one two three for assistance.` |
| `The 3rd quarter earnings report shows revenue of $4.7 billion, up 12% from 2023.` | `The third quarter earnings report shows revenue of four point seven billion dollars, up twelve percent from twenty twenty three.` |
| `Mix 3/4 cup flour with 1.5 tsp baking powder and 2 eggs at 350°F for 25 minutes.` | `Mix three quarters cup flour with one point five tsp baking powder and two eggs at three hundred and fifty degrees Fahrenheit for twenty five minutes.` |
| `The marathon runner completed the 26.2 mile course in 2:45:33 on April 21, 2024.` | `The marathon runner completed the twenty six point two mile course in two hours forty five minutes and thirty three seconds on april twenty first, twenty twenty four.` |
| `Server load reached 95% at 3:12 AM on 01/15/2025, affecting 1,200 users.` | `Server load reached ninety five percent at three twelve AM on january fifteenth twenty twenty five, affecting one thousand two hundred users.` |
| `The Tesla Model 3 accelerates from 0 to 60 mph in 3.1 seconds and costs $42,990.` | `The Tesla Model three accelerates from zero to sixty miles per hour in three point one seconds and costs forty two thousand nine hundred and ninety dollars.` |
| `Send your RSVP to events@nvidia.com by Dec 31, 2024 for the Jan. 15, 2025 gala.` | `Send your RSVP to events at NVIDIA dot com by december thirty first, twenty twenty four for the january fifteenth, twenty twenty five gala.` |
| `The 2nd amendment was ratified on December 15, 1791, over 230 years ago.` | `The second amendment was ratified on december fifteenth, seventeen ninety one, over two hundred and thirty years ago.` |
| `The recipe yields 12 servings of 350 calories each with 8.5 g of protein per serving.` | `The recipe yields twelve servings of three hundred and fifty calories each with eight point five G of protein per serving.` |
| `On July 20, 1969 at 10:56 p.m. EDT, Neil Armstrong took his 1st steps on the moon.` | `On july twentieth, nineteen sixty nine at ten fifty six PM EDT, Neil Armstrong took his first steps on the moon.` |
| `The warehouse at 500 Industrial Blvd ships 10,000 packages daily via 25 trucks.` | `The warehouse at five hundred Industrial Boulevard ships ten thousand packages daily via twenty five trucks.` |
| `The building is 1,454 ft tall with 110 floors and was completed on April 4, 1973.` | `The building is one thousand four hundred and fifty four feet tall with one hundred and ten floors and was completed on april fourth, nineteen seventy three.` |
| `Train 4521 departs Penn Station at 7:15 AM, stops at 3 stations, and arrives at 9:42 AM.` | `Train four thousand five hundred and twenty one departs Penn Station at seven fifteen AM, stops at three stations, and arrives at nine forty two AM.` |

### Inverse Text Normalization (ITN) -- 88 cases

#### Email ITN (7 cases)

| Input | Output |
|-------|--------|
| `a dot b c at g mail dot com` | `a.bc@gmail.com` |
| `c d f at a b c dot e d u` | `cdf@abc.edu` |
| `a b c at a b c dot com` | `abc@abc.com` |
| `a at nvidia dot com` | `a@nvidia.com` |
| `a s d f one two three at a b c dot com` | `asdf123@abc.com` |
| `abc at gmail dot com` | `abc@gmail.com` |
| `n vidia dot com` | `nvidia.com` |

#### Phone ITN (5 cases)

| Input | Output |
|-------|--------|
| `one two three one two three five six seven eight` | `123-123-5678` |
| `plus nine one one two three one two three five six seven eight` | `+91 123-123-5678` |
| `plus forty four one two three one two three five six seven eight` | `+44 123-123-5678` |
| `one two three dot one two three dot o dot four o` | `123.123.0.40` |
| `ssn is seven double nine one two three double one three` | `ssn is 799-12-3113` |

#### Cardinal ITN (12 cases)

| Input | Output |
|-------|--------|
| `one` | `one` |
| `twelve` | `twelve` |
| `twenty one` | `21` |
| `ninety nine` | `99` |
| `one hundred` | `100` |
| `one hundred and twenty three` | `123` |
| `one thousand` | `1000` |
| `one million` | `1 million` |
| `ten thousand five hundred` | `10500` |
| `zero` | `zero` |
| `minus forty two` | `-42` |
| `minus one hundred` | `-100` |

Note: Single-word numbers (`one`, `twelve`, `zero`) pass through unchanged. `one million` becomes `1 million` (not `1000000`). This matches the NeMo ITN grammar behavior.

#### Ordinal ITN (6 cases)

| Input | Output |
|-------|--------|
| `first` | `1st` |
| `second` | `2nd` |
| `third` | `3rd` |
| `tenth` | `10th` |
| `twenty first` | `21st` |
| `one hundredth` | `100th` |

#### Money ITN (7 cases)

| Input | Output |
|-------|--------|
| `one dollar` | `$1` |
| `ten dollars` | `$10` |
| `one hundred dollars` | `$100` |
| `one dollar fifty cents` | `$1.50` |
| `ninety nine cents` | `$0.99` |
| `five dollars and thirty two cents` | `$5.32` |
| `one million dollars` | `$1 million` |

#### Date ITN (5 cases)

| Input | Output |
|-------|--------|
| `january first twenty twenty` | `january 1 2020` |
| `february fourteenth twenty twenty three` | `february 14 2023` |
| `december twenty fifth two thousand` | `december 25 2000` |
| `march third nineteen ninety nine` | `march 3 1999` |
| `july fourth seventeen seventy six` | `july 4 1776` |

#### Time ITN (4 cases)

| Input | Output |
|-------|--------|
| `twelve thirty p m` | `12:30 p.m.` |
| `three forty five a m` | `03:45 a.m.` |
| `one a m` | `01:00 a.m.` |
| `eleven fifty nine p m` | `11:59 p.m.` |

#### Measure ITN (6 cases)

| Input | Output |
|-------|--------|
| `one hundred kilograms` | `100 kg` |
| `five point five kilometers` | `5.5 km` |
| `three feet` | `3 ft` |
| `ten pounds` | `ten pounds` |
| `fifty centimeters` | `50 cm` |
| `one hundred miles per hour` | `100 mph` |

Note: `ten pounds` passes through unchanged -- the ITN grammar does not convert ambiguous units.

#### Decimal ITN (4 cases)

| Input | Output |
|-------|--------|
| `zero point five` | `0.5` |
| `three point one four` | `3.14` |
| `minus two point five` | `-2.5` |
| `one hundred point zero zero one` | `100.001` |

#### Fraction ITN (5 cases)

| Input | Output |
|-------|--------|
| `one half` | `one half` |
| `three quarters` | `three quarters` |
| `one third` | `one 3rd` |
| `two thirds` | `two thirds` |
| `seven eighths` | `seven eighths` |

Note: Most fractions pass through unchanged. The NeMo ITN grammar has limited fraction support.

#### Mixed Sentences ITN (8 cases)

| Input | Output |
|-------|--------|
| `I have five dollars and three apples` | `I have $5 and three apples` |
| `She ran five kilometers in twenty three minutes` | `She ran 5 km in 23 min` |
| `the first and second floors are closed` | `the 1st and 2nd floors are closed` |
| `he scored ninety nine out of one hundred` | `he scored 99 out of 100` |
| `add one thousand to the total` | `add 1000 to the total` |
| `the year two thousand was a leap year` | `the year 2000 was a leap year` |
| `I owe you three dollars fifty cents` | `I owe you $3.50` |
| `pi is approximately three point one four` | `pi is approximately 3.14` |

#### Long Sentences ITN (19 cases)

| Input | Output |
|-------|--------|
| `The invoice total is two thousand four hundred and fifty dollars ninety nine cents and payment is due by march fifteenth twenty twenty five.` | `The invoice total is $2450.99 and payment is due by march 15 2025 .` |
| `doctor Johnson prescribed five hundred milligrams of ibuprofen to be taken three times daily for seven days.` | `dr. Johnson prescribed 500 mg of ibuprofen to be taken three times daily for seven days.` |
| `Flight UA twenty four ninety one departs from gate B twelve at six forty five a m and arrives at eleven thirty p m.` | `Flight UA 2491 departs from gate B twelve at 06:45 a.m. and arrives at 11:30 p.m. .` |
| `The property at eighteen forty two North Highland Avenue sold for one point two million dollars.` | `The property at 1842 North Highland Avenue sold for $1.2 million .` |
| `The third quarter earnings report shows revenue of four point seven billion dollars up twelve percent from twenty twenty three.` | `The 3rd quarter earnings report shows revenue of $4.7 billion up 12 % from 2023 .` |
| `Mix three quarters cup flour with one point five teaspoons baking powder and two eggs at three hundred and fifty degrees Fahrenheit for twenty five minutes.` | `Mix three quarters cup flour with 1.5 teaspoons baking powder and two eggs at 350 °F for 25 min .` |
| `The marathon runner completed the twenty six point two mile course in two hours forty five minutes on april twenty first twenty twenty four.` | `The marathon runner completed the 26.2 mi course in 2 h 45 min on april 21 2024 .` |
| `Server load reached ninety five percent at three twelve a m on january fifteenth twenty twenty five affecting one thousand two hundred users.` | `Server load reached 95 % at 03:12 a.m. on january 15 2025 affecting 1200 users.` |
| `The Tesla Model three accelerates from zero to sixty miles per hour in three point one seconds and costs forty two thousand nine hundred and ninety dollars.` | `The Tesla Model three accelerates from zero to 60 mph in 3.1 s and costs $42990 .` |
| `Send your RSVP to events at nvidia dot com by december thirty first twenty twenty four for the january fifteenth twenty twenty five gala.` | `Send your RSVP to events@nvidia.com by december 31 2024 for the january 15 2025 gala.` |
| `The second amendment was ratified on december fifteenth seventeen ninety one over two hundred and thirty years ago.` | `The 2nd amendment was ratified on december 15 1791 over 230 years ago.` |
| `On july twentieth nineteen sixty nine at ten fifty six p m Neil Armstrong took his first steps on the moon.` | `On july 20 1969 at 10:56 p.m. Neil Armstrong took his 1st steps on the moon.` |
| `The warehouse at five hundred Industrial Boulevard ships ten thousand packages daily via twenty five trucks.` | `The warehouse at 500 Industrial Boulevard ships 10000 packages daily via 25 trucks.` |
| `The building is one thousand four hundred and fifty four feet tall with one hundred and ten floors and was completed on april fourth nineteen seventy three.` | `The building is 1454 ft tall with 110 floors and was completed on april 4 1973 .` |
| `Train forty five twenty one departs Penn Station at seven fifteen a m stops at three stations and arrives at nine forty two a m.` | `Train 4521 departs Penn Station at 07:15 a.m. stops at three stations and arrives at 09:42 a.m. .` |
| `She earned her PhD at twenty five and published one hundred and twelve papers by age forty two.` | `She earned her PhD at 25 and published 112 papers by age 42 .` |
| `The city has a population of eight point four million people spread across three hundred and two square miles.` | `The city has a population of 8.4 million people spread across 302 sq mi .` |
| `We drove five hundred and sixty two kilometers from Munich to Paris averaging one hundred and ten kilometers per hour.` | `We drove 562 km from Munich to Paris averaging 110 km/h .` |
| `The concert tickets cost seventy five dollars each and the venue holds twelve thousand five hundred people.` | `The concert tickets cost $75 each and the venue holds 12500 people.` |

## How It Works

### Pipeline

The library implements the same pipeline as the Python `Normalizer` class in `normalize.py`:

1. **Compile input** -- The input string is compiled into a linear-chain FST using `fst::StringCompiler` in byte mode.

2. **Tag (classify)** -- The input FST is composed with the tagger FST (`TOKENIZE_AND_CLASSIFY` rule from the FAR). `fst::ShortestPath` extracts the best tagged output. For example, `$42,990` becomes:
   ```
   tokens { money { integer_part: "forty two thousand nine hundred and ninety" currency: "$" } }
   ```

3. **Parse tags** -- A recursive descent parser (ported from `token_parser.py`) parses the tagged text into a tree of key-value pairs. The grammar is:
   ```
   A -> ws F ws F ...
   F -> key G
   G -> : "value" | { A }
   ```

4. **Permute** -- The parser output may need key reordering for the verbalizer. If the token has `preserve_order: true`, the original order is kept. Otherwise, all permutations are tried. In practice, most tokens are deterministic and need only one pass.

5. **Verbalize** -- Each token's inner content (without the `tokens { }` wrapper) is composed with the verbalizer FST (`ALL` rule). The first permutation that succeeds is used. Results are concatenated with spaces.

6. **Post-process** -- Optionally, the output is composed with a post-processing FST (`POSTPROCESSOR` rule) for cleanup like punctuation spacing.

### Key Implementation Details

- **VectorFst, not LookaheadFst** -- The library copies FSTs into `fst::VectorFst<StdArc>` instead of using `StdArcLookAheadFst`. LookaheadFst changes composition tiebreaking and produces incorrect case-sensitive results (e.g., lowercasing brand names like NVIDIA).

- **Per-token verbalization** -- The exported FAR contains `VerbalizeFst` (per-token), not `VerbalizeFinalFst`. The library strips the `tokens { }` wrapper and verbalizes each token individually, then concatenates the results.

- **Byte mode** -- `StringCompiler` uses byte mode by default, so no escaping is needed for the tagger input. The `fst_escape` function is only used for the post-processing step.

- **Thread safety** -- A `NemoNormalizer` instance is not thread-safe. Create separate instances for concurrent use.

- **Language-agnostic** -- The C library has no language-specific code. All language-specific logic is encoded in the FST grammars. The same binary works for all 19 languages.

## Build Details

> This section documents the Docker-based build. For native autotools builds, see [Building with Autotools (native)](#building-with-autotools-native).

### Dependencies

| Library | Version | Source |
|---------|---------|--------|
| OpenFst | 1.7.2+ | `apt install libfst-dev` or `conda install thrax` |
| Thrax | 1.3.4 | `conda install -c conda-forge thrax=1.3.4` (Docker only) |
| gcc/g++ | 7+ (C++17) | `apt install build-essential` |

For native builds, also install: `autoconf`, `automake`, `libtool`, `pkg-config`.

### Makefile Targets

```bash
make all       # Build libnemo_normalize.so and test_normalize
make test      # Run the test suite (alias for make check with autotools)
make clean     # Remove build artifacts
```

### Dockerfile

The Dockerfile builds on `continuumio/miniconda3` (linux/amd64):

1. Installs `build-essential` and `pkg-config`
2. Installs `thrax=1.3.4` from conda-forge (brings OpenFst + headers)
3. Configures the linker to find conda libraries
4. Copies source files and pre-exported FAR files
5. Runs `make all` to compile

### Platform Note

The conda `thrax=1.3.4` package is only available for `linux/amd64`. On Apple Silicon (ARM), Docker runs the container under x86_64 emulation via `--platform=linux/amd64`. This is handled automatically by the Dockerfile.

## Performance

Measured inside Docker on Apple Silicon (x86_64 emulation via Rosetta):

| Metric | Value |
|--------|-------|
| TN normalizer load time | ~156ms |
| ITN normalizer load time | ~52ms |
| Average normalization | ~18ms |
| Long sentence normalization | ~40-90ms |
| Throughput | ~55 normalizations/sec |

Native x86_64 Linux will be significantly faster without the emulation overhead.

## License

Apache License 2.0. See the license headers in source files.
