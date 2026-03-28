/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * libnemo_normalize -- Pure C API for NeMo text normalization via OpenFst.
 *
 * Usage:
 *   NemoNormalizer *n = nemo_normalizer_create(
 *       "far_export/en_tn_grammars_cased/classify/tokenize_and_classify.far",
 *       "far_export/en_tn_grammars_cased/verbalize/verbalize.far",
 *       NULL  // optional post-processing FAR
 *   );
 *   char buf[4096];
 *   int rc = nemo_normalize(n, "cdf@abc.edu", buf, sizeof(buf));
 *   // buf now contains "cdf at abc dot edu"
 *   nemo_normalizer_destroy(n);
 */

#ifndef NEMO_NORMALIZE_H
#define NEMO_NORMALIZE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NemoNormalizer NemoNormalizer;

/*
 * Create a normalizer by loading FAR grammar files.
 *
 * classify_far_path:     Path to tokenize_and_classify.far
 * verbalize_far_path:    Path to verbalize.far
 * post_process_far_path: Path to post_processing.far (may be NULL to skip)
 *
 * Returns a normalizer handle, or NULL on failure.
 */
NemoNormalizer *nemo_normalizer_create(
    const char *classify_far_path,
    const char *verbalize_far_path,
    const char *post_process_far_path
);

/*
 * Normalize a text string (written form -> spoken form).
 *
 * norm:           Normalizer handle from nemo_normalizer_create().
 * input:          Input text (UTF-8, null-terminated).
 * output:         Buffer to receive the normalized output.
 * output_max_len: Size of the output buffer in bytes.
 *
 * Returns 0 on success, -1 on error (output is set to empty string).
 */
int nemo_normalize(
    NemoNormalizer *norm,
    const char *input,
    char *output,
    int output_max_len
);

/*
 * Free all resources associated with a normalizer.
 */
void nemo_normalizer_destroy(NemoNormalizer *norm);

#ifdef __cplusplus
}
#endif

#endif /* NEMO_NORMALIZE_H */
