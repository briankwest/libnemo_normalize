#!/usr/bin/env bash
# export_far.sh -- Export FAR grammar files from the Python NeMo environment.
#
# This runs pynini_export.py inside the nemo-tn-test Docker container
# to produce the .far files needed by libnemo_normalize.
#
# Exports ALL supported languages for both TN and ITN directions.
#
# Usage:
#   bash export_far.sh              # Export all languages
#   bash export_far.sh en de fr     # Export specific languages only
#
# Output directory: ./far_export/
#
# Prerequisites:
#   - Docker image 'nemo-tn-test' must exist (built from existing Dockerfile)
#   - NeMo-text-processing repo must be at ../NeMo-text-processing/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/far_export"

# --- Supported languages ---
# TN (Text Normalization) -- 12 languages
TN_LANGUAGES=(en de es fr hi hu it ja rw sv vi zh)

# ITN (Inverse Text Normalization) -- 16 languages
ITN_LANGUAGES=(en de es pt ru fr sv vi ar es_en zh he hi hy mr ja)

# If arguments are given, filter to only those languages
FILTER_LANGUAGES=("$@")

should_export() {
    local lang="$1"
    if [ ${#FILTER_LANGUAGES[@]} -eq 0 ]; then
        return 0  # No filter, export all
    fi
    for f in "${FILTER_LANGUAGES[@]}"; do
        if [ "$f" = "$lang" ]; then
            return 0
        fi
    done
    return 1
}

in_array() {
    local needle="$1"
    shift
    for item in "$@"; do
        if [ "$item" = "$needle" ]; then
            return 0
        fi
    done
    return 1
}

echo "=== NeMo FAR Grammar Export (All Languages) ==="
echo "  Repo:   $REPO_DIR"
echo "  Output: $OUTPUT_DIR"
if [ ${#FILTER_LANGUAGES[@]} -gt 0 ]; then
    echo "  Filter: ${FILTER_LANGUAGES[*]}"
fi
echo ""

mkdir -p "$OUTPUT_DIR"

TN_OK=()
TN_FAIL=()
ITN_OK=()
ITN_FAIL=()

# --- Export TN grammars for all supported languages ---
echo "======================================================================"
echo "  TEXT NORMALIZATION (TN) -- ${#TN_LANGUAGES[@]} languages"
echo "======================================================================"
echo ""

for lang in "${TN_LANGUAGES[@]}"; do
    if ! should_export "$lang"; then
        continue
    fi

    echo "--- Exporting TN: $lang ---"
    if docker run --platform linux/amd64 --rm \
        -v "$REPO_DIR/NeMo-text-processing:/workspace/NeMo-text-processing" \
        -v "$OUTPUT_DIR:/workspace/far_output" \
        nemo-tn-test \
        conda run -n nemo bash -c "
            cd /workspace/NeMo-text-processing && \
            python tools/text_processing_deployment/pynini_export.py \
                --output_dir=/workspace/far_output \
                --language=$lang \
                --grammars=tn_grammars \
                --input_case=cased
        "; then
        echo "  [OK] TN $lang"
        TN_OK+=("$lang")
    else
        echo "  [FAIL] TN $lang"
        TN_FAIL+=("$lang")
    fi
    echo ""
done

# --- Export ITN grammars for all supported languages ---
echo "======================================================================"
echo "  INVERSE TEXT NORMALIZATION (ITN) -- ${#ITN_LANGUAGES[@]} languages"
echo "======================================================================"
echo ""

for lang in "${ITN_LANGUAGES[@]}"; do
    if ! should_export "$lang"; then
        continue
    fi

    echo "--- Exporting ITN: $lang ---"
    if docker run --platform linux/amd64 --rm \
        -v "$REPO_DIR/NeMo-text-processing:/workspace/NeMo-text-processing" \
        -v "$OUTPUT_DIR:/workspace/far_output" \
        nemo-tn-test \
        conda run -n nemo bash -c "
            cd /workspace/NeMo-text-processing && \
            python tools/text_processing_deployment/pynini_export.py \
                --output_dir=/workspace/far_output \
                --language=$lang \
                --grammars=itn_grammars \
                --input_case=cased
        "; then
        echo "  [OK] ITN $lang"
        ITN_OK+=("$lang")
    else
        echo "  [FAIL] ITN $lang"
        ITN_FAIL+=("$lang")
    fi
    echo ""
done

# --- Summary ---
echo "======================================================================"
echo "  EXPORT SUMMARY"
echo "======================================================================"
echo ""
echo "  TN  succeeded: ${#TN_OK[@]}  (${TN_OK[*]:-none})"
if [ ${#TN_FAIL[@]} -gt 0 ]; then
    echo "  TN  failed:    ${#TN_FAIL[@]}  (${TN_FAIL[*]})"
fi
echo "  ITN succeeded: ${#ITN_OK[@]}  (${ITN_OK[*]:-none})"
if [ ${#ITN_FAIL[@]} -gt 0 ]; then
    echo "  ITN failed:    ${#ITN_FAIL[@]}  (${ITN_FAIL[*]})"
fi
echo ""
echo "  FAR files written to: $OUTPUT_DIR"
echo ""

# List all exported FAR files
find "$OUTPUT_DIR" -name '*.far' | sort | while read -r f; do
    echo "  $f"
done

echo ""
TOTAL_FAIL=$(( ${#TN_FAIL[@]} + ${#ITN_FAIL[@]} ))
if [ "$TOTAL_FAIL" -gt 0 ]; then
    echo "  WARNING: $TOTAL_FAIL export(s) failed. Check output above for details."
    exit 1
fi
echo "  All exports succeeded."
