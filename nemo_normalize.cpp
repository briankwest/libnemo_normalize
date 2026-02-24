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
 * libnemo_normalize -- C++ implementation of NeMo text normalization via OpenFst.
 *
 * Pipeline (mirrors normalize.py):
 *   1. Compile input text -> FST
 *   2. Compose with tagger FST -> shortest path -> tagged text
 *   3. Parse tagged text (port of token_parser.py)
 *   4. Generate permutations of token orderings
 *   5. Compose each permutation with verbalizer FST -> shortest path -> output
 *   6. (Optional) compose with post-processing FST
 */

#include "nemo_normalize.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <regex>
#include <string>
#include <variant>
#include <vector>

#include <fst/fstlib.h>
#include <thrax/thrax.h>

using fst::StdArc;
using fst::StdFst;
using thrax::GrmManager;

/* Note: We intentionally do NOT use StdArcLookAheadFst (LookaheadFst).
 * LookaheadFst changes composition tiebreaking and produces incorrect
 * case-sensitive results (e.g. lowercasing brand names).
 * Using VectorFst<StdArc> copies instead gives correct results. */
typedef fst::VectorFst<StdArc> StoredFst;

/* ========================================================================
 * Workaround: OpenFst FAR needs these symbols at link time.
 * (Same pattern as alignment.cpp in the NeMo repo.)
 * ======================================================================== */
#include <fstream>
namespace fst {
#include <fst/extensions/far/stlist.h>
#include <fst/extensions/far/sttable.h>
#include <cstdint>
#include <ios>

bool IsSTList(const std::string &source) {
    std::ifstream strm(source, std::ios_base::in | std::ios_base::binary);
    if (!strm) return false;
    int32_t magic_number = 0;
    ReadType(strm, &magic_number);
    return magic_number == kSTListMagicNumber;
}

bool IsSTTable(const std::string &source) {
    std::ifstream strm(source);
    if (!strm.good()) return false;
    int32_t magic_number = 0;
    ReadType(strm, &magic_number);
    return magic_number == kSTTableMagicNumber;
}
} // namespace fst

/* ========================================================================
 * Tag Parser  (port of token_parser.py)
 *
 * Input example:
 *   tokens { electronic { protocol: "http" domain: "nvidia.com" } } tokens { name: "left" }
 *
 * Output: vector of TagNode (ordered dict), each key maps to either a string
 *         value or a nested ordered dict.
 * ======================================================================== */

static const char EOS_CHAR = '\0';
static const std::string PRESERVE_ORDER_KEY = "preserve_order";

struct TagValue;

/* Ordered map preserving insertion order (like Python OrderedDict). */
using TagDict = std::vector<std::pair<std::string, TagValue>>;

struct TagValue {
    /* A value is either a string literal or a nested dict. */
    bool is_string;
    std::string str_val;
    TagDict dict_val;

    static TagValue make_string(const std::string &s) {
        TagValue v;
        v.is_string = true;
        v.str_val = s;
        return v;
    }
    static TagValue make_dict(const TagDict &d) {
        TagValue v;
        v.is_string = false;
        v.dict_val = d;
        return v;
    }
    static TagValue make_bool() {
        /* preserve_order: true -- stored as a string "true" */
        TagValue v;
        v.is_string = true;
        v.str_val = "true";
        return v;
    }
};

class TagParser {
public:
    void init(const std::string &text) {
        text_ = text;
        len_ = (int)text.size();
        idx_ = 0;
        ch_ = len_ > 0 ? text_[0] : EOS_CHAR;
    }

    /* A -> ws F ws F ... ws */
    std::vector<TagDict> parse() {
        std::vector<TagDict> result;
        while (parse_ws()) {
            TagDict token;
            if (!parse_token(token))
                break;
            result.push_back(token);
        }
        return result;
    }

private:
    std::string text_;
    int len_;
    int idx_;
    char ch_;

    bool read() {
        if (idx_ < len_ - 1) {
            idx_++;
            ch_ = text_[idx_];
            return true;
        }
        ch_ = EOS_CHAR;
        return false;
    }

    bool parse_ws() {
        bool not_eos = (ch_ != EOS_CHAR);
        while (not_eos && ch_ == ' ')
            not_eos = read();
        return not_eos;
    }

    bool parse_char(char exp) {
        if (ch_ != exp) return false;
        read();
        return true;
    }

    bool parse_chars(const char *exp) {
        for (const char *p = exp; *p; p++) {
            if (!parse_char(*p)) return false;
        }
        return true;
    }

    /* Parse key: ascii letters and '_' */
    bool parse_string_key(std::string &out) {
        if (ch_ == EOS_CHAR || ch_ == ' ' || ch_ == '\t' || ch_ == '\n')
            return false;
        out.clear();
        while ((ch_ >= 'a' && ch_ <= 'z') || (ch_ >= 'A' && ch_ <= 'Z') || ch_ == '_') {
            out.push_back(ch_);
            if (!read()) break;
        }
        return !out.empty();
    }

    /* Parse value string between quotes. Ends at quote followed by space (or EOS). */
    bool parse_string_value(std::string &out) {
        if (ch_ == EOS_CHAR) return false;
        out.clear();
        /* The Python version: while self.char != '"' or self.text[self.index+1] != ' ' */
        while (true) {
            if (ch_ == '"') {
                /* Check if next char is space, '}', or EOS (end of value) */
                char next = (idx_ + 1 < len_) ? text_[idx_ + 1] : ' ';
                if (next == ' ' || next == '}' || idx_ + 1 >= len_)
                    break;
            }
            out.push_back(ch_);
            if (!read()) return false;
        }
        return true;
    }

    /* G -> : "value" | { A } */
    bool parse_token_value(TagValue &out) {
        if (ch_ == ':') {
            parse_char(':');
            parse_ws();
            if (!parse_char('"')) return false;
            std::string val;
            if (!parse_string_value(val)) return false;
            if (!parse_char('"')) return false;
            out = TagValue::make_string(val);
            return true;
        } else if (ch_ == '{') {
            parse_char('{');
            auto list_dicts = parse();
            /* Flatten: merge all dicts into one TagDict (like Python version) */
            TagDict merged;
            for (auto &td : list_dicts) {
                for (auto &kv : td) {
                    merged.push_back(kv);
                }
            }
            if (!parse_char('}')) return false;
            out = TagValue::make_dict(merged);
            return true;
        }
        return false;
    }

    /* F -> key G */
    bool parse_token(TagDict &out) {
        std::string key;
        if (!parse_string_key(key))
            return false;
        parse_ws();
        TagValue val;
        if (key == PRESERVE_ORDER_KEY) {
            parse_char(':');
            parse_ws();
            parse_chars("true");
            val = TagValue::make_bool();
        } else {
            if (!parse_token_value(val))
                return false;
        }
        out.push_back({key, val});
        return true;
    }
};

/* ========================================================================
 * Permutation Generator  (port of normalize.py _permute / generate_permutations)
 *
 * If the dict contains "preserve_order", we don't permute.
 * Otherwise, generate all permutations of the dict keys and recursively
 * permute nested dicts.
 * ======================================================================== */

static bool has_preserve_order(const TagDict &d) {
    for (auto &kv : d) {
        if (kv.first == PRESERVE_ORDER_KEY) return true;
    }
    return false;
}

/* Serialize a TagDict to tagged-text string. */
static std::vector<std::string> permute_dict(const TagDict &d);

static std::vector<std::string> permute_dict(const TagDict &d) {
    /* Build index vector for permutation */
    std::vector<int> indices(d.size());
    std::iota(indices.begin(), indices.end(), 0);

    bool preserve = has_preserve_order(d);

    std::vector<std::string> results;

    /* If preserve_order, only one ordering; otherwise all permutations. */
    do {
        /* For this permutation, build cross product of serialized sub-parts */
        std::vector<std::string> sub = {""};

        for (int i : indices) {
            const auto &key = d[i].first;
            const auto &val = d[i].second;

            if (key == PRESERVE_ORDER_KEY) {
                /* Emit: preserve_order: true */
                std::vector<std::string> new_sub;
                for (auto &prefix : sub) {
                    new_sub.push_back(prefix + "preserve_order: true ");
                }
                sub = std::move(new_sub);
            } else if (val.is_string) {
                /* Emit: key: "value" */
                std::string frag = key + ": \"" + val.str_val + "\" ";
                std::vector<std::string> new_sub;
                for (auto &prefix : sub) {
                    new_sub.push_back(prefix + frag);
                }
                sub = std::move(new_sub);
            } else {
                /* Nested dict: recursively permute */
                auto rec = permute_dict(val.dict_val);
                std::vector<std::string> new_sub;
                for (auto &prefix : sub) {
                    for (auto &r : rec) {
                        new_sub.push_back(prefix + " " + key + " { " + r + " } ");
                    }
                }
                sub = std::move(new_sub);
            }
        }
        for (auto &s : sub)
            results.push_back(s);
    } while (!preserve && std::next_permutation(indices.begin(), indices.end()));

    return results;
}

/* Generate all permutations for a list of token dicts (cross product). */
static void generate_permutations_helper(
    const std::vector<TagDict> &tokens,
    size_t idx,
    const std::string &prefix,
    std::vector<std::string> &out
) {
    if (idx == tokens.size()) {
        out.push_back(prefix);
        return;
    }
    auto opts = permute_dict(tokens[idx]);
    for (auto &opt : opts) {
        generate_permutations_helper(tokens, idx + 1, prefix + opt, out);
    }
}

/* ========================================================================
 * FST helpers
 * ======================================================================== */

/* Compile a string to a linear-chain FST using the grammar's symbol table. */
static bool string_to_fst(const std::string &input,
                           GrmManager::MutableTransducer *fst_out) {
    fst::StringCompiler<StdArc> compiler;
    return compiler(input, fst_out);
}

/* Walk a shortest-path linear FST and extract the output string. */
static std::string fst_output_string(const StdFst &fst) {
    std::string result;
    auto state = fst.Start();
    if (state == fst::kNoStateId) return result;

    while (fst.Final(state) == StdArc::Weight::Zero()) {
        fst::ArcIterator<StdFst> aiter(fst, state);
        if (aiter.Done()) break;
        const auto &arc = aiter.Value();
        if (arc.olabel != 0) {
            result.push_back((char)arc.olabel);
        }
        state = arc.nextstate;
    }
    return result;
}

/* Escape special OpenFst characters in input string (mirrors pynini.escape).
 * Brackets [ ] and backslash need escaping. */
static std::string fst_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '[' || c == ']' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

/* Collapse multiple spaces into one. */
static std::string collapse_spaces(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool prev_space = false;
    for (char c : s) {
        if (c == ' ') {
            if (!prev_space) out.push_back(c);
            prev_space = true;
        } else {
            out.push_back(c);
            prev_space = false;
        }
    }
    return out;
}

/* Trim leading/trailing spaces. */
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(' ');
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(' ');
    return s.substr(start, end - start + 1);
}

/* ========================================================================
 * NemoNormalizer implementation
 * ======================================================================== */

struct NemoNormalizer {
    std::unique_ptr<GrmManager> tagger_grm;
    std::unique_ptr<GrmManager> verbalizer_grm;
    std::unique_ptr<GrmManager> post_process_grm;

    std::unique_ptr<StoredFst> tagger_fst;
    std::unique_ptr<StoredFst> verbalizer_fst;
    std::unique_ptr<StoredFst> post_process_fst;

    bool has_post_process;
};

extern "C" {

NemoNormalizer *nemo_normalizer_create(
    const char *classify_far_path,
    const char *verbalize_far_path,
    const char *post_process_far_path
) {
    auto *norm = new (std::nothrow) NemoNormalizer();
    if (!norm) return nullptr;

    norm->has_post_process = false;

    /* Load tagger FAR */
    norm->tagger_grm.reset(new GrmManager());
    if (!norm->tagger_grm->LoadArchive(classify_far_path)) {
        std::cerr << "nemo_normalizer_create: failed to load classify FAR: "
                  << classify_far_path << std::endl;
        delete norm;
        return nullptr;
    }
    const StdFst *t = norm->tagger_grm->GetFst("TOKENIZE_AND_CLASSIFY");
    if (!t) {
        std::cerr << "nemo_normalizer_create: FAR missing rule TOKENIZE_AND_CLASSIFY" << std::endl;
        delete norm;
        return nullptr;
    }
    norm->tagger_fst.reset(new StoredFst(*t));

    /* Load verbalizer FAR */
    norm->verbalizer_grm.reset(new GrmManager());
    if (!norm->verbalizer_grm->LoadArchive(verbalize_far_path)) {
        std::cerr << "nemo_normalizer_create: failed to load verbalize FAR: "
                  << verbalize_far_path << std::endl;
        delete norm;
        return nullptr;
    }
    const StdFst *v = norm->verbalizer_grm->GetFst("ALL");
    if (!v) {
        std::cerr << "nemo_normalizer_create: FAR missing rule ALL" << std::endl;
        delete norm;
        return nullptr;
    }
    norm->verbalizer_fst.reset(new StoredFst(*v));

    /* Optionally load post-processing FAR */
    if (post_process_far_path && post_process_far_path[0] != '\0') {
        norm->post_process_grm.reset(new GrmManager());
        if (!norm->post_process_grm->LoadArchive(post_process_far_path)) {
            std::cerr << "nemo_normalizer_create: failed to load post-process FAR: "
                      << post_process_far_path << std::endl;
            /* Non-fatal: continue without post-processing */
        } else {
            const StdFst *pp = norm->post_process_grm->GetFst("POSTPROCESSOR");
            if (pp) {
                norm->post_process_fst.reset(new StoredFst(*pp));
                norm->has_post_process = true;
            }
        }
    }

    return norm;
}

int nemo_normalize(
    NemoNormalizer *norm,
    const char *input,
    char *output,
    int output_max_len
) {
    if (!norm || !input || !output || output_max_len <= 0) {
        if (output && output_max_len > 0) output[0] = '\0';
        return -1;
    }

    std::string text(input);

    /* Strip leading/trailing whitespace */
    text = trim(text);
    if (text.empty()) {
        output[0] = '\0';
        return 0;
    }

    /* ---- Step 1: Tag (classify) ---- */
    /* Note: StringCompiler uses byte mode, so no escaping needed.
     * pynini.escape() is for text-format symbol tables, not byte mode. */
    GrmManager::MutableTransducer input_fst;
    if (!string_to_fst(text, &input_fst)) {
        output[0] = '\0';
        return -1;
    }

    fst::ComposeFst<StdArc> tagged_lattice(input_fst, *norm->tagger_fst);
    GrmManager::MutableTransducer tagged_shortest;
    fst::ShortestPath(tagged_lattice, &tagged_shortest);

    if (tagged_shortest.Start() == fst::kNoStateId) {
        /* Tagger produced no output -- return original text */
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    std::string tagged_text = fst_output_string(tagged_shortest);
    if (tagged_text.empty()) {
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    /* ---- Step 2: Parse tagged text ---- */
    TagParser parser;
    parser.init(tagged_text);
    auto tokens = parser.parse();

    if (tokens.empty()) {
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    /* ---- Step 3: Verbalize each token individually ---- */
    /*
     * The exported FAR has VerbalizeFst (per-token), not VerbalizeFinalFst.
     * So we process each top-level "tokens" entry separately:
     *   1. Extract the inner content (without "tokens { ... }" wrapper)
     *   2. Generate permutations of the inner dict
     *   3. Compose each permutation with the verbalizer
     *   4. Use the first permutation that succeeds
     */
    std::string verbalized;

    for (auto &token_dict : tokens) {
        /* Each token_dict should have one entry: ("tokens", inner_value) */
        TagDict inner;
        for (auto &kv : token_dict) {
            if (kv.first == "tokens" && !kv.second.is_string) {
                inner = kv.second.dict_val;
            }
        }

        if (inner.empty()) {
            /* Fallback: try to serialize the token dict directly */
            auto perms = permute_dict(token_dict);
            bool token_ok = false;
            for (auto &perm : perms) {
                std::string trimmed = trim(perm);
                if (trimmed.empty()) continue;
                GrmManager::MutableTransducer perm_fst;
                if (!string_to_fst(trimmed, &perm_fst)) continue;
                fst::ComposeFst<StdArc> vlat(perm_fst, *norm->verbalizer_fst);
                GrmManager::MutableTransducer vsp;
                fst::ShortestPath(vlat, &vsp);
                if (vsp.Start() == fst::kNoStateId) continue;
                std::string r = fst_output_string(vsp);
                if (!r.empty()) {
                    if (!verbalized.empty()) verbalized += " ";
                    verbalized += r;
                    token_ok = true;
                    break;
                }
            }
            if (!token_ok) {
                strncpy(output, input, output_max_len - 1);
                output[output_max_len - 1] = '\0';
                return -1;
            }
            continue;
        }

        /* Generate permutations of the inner content */
        auto perms = permute_dict(inner);
        bool token_ok = false;

        for (auto &perm : perms) {
            std::string trimmed = trim(perm);
            if (trimmed.empty()) continue;

            GrmManager::MutableTransducer perm_fst;
            if (!string_to_fst(trimmed, &perm_fst))
                continue;

            fst::ComposeFst<StdArc> verb_lattice(perm_fst, *norm->verbalizer_fst);
            GrmManager::MutableTransducer verb_shortest;
            fst::ShortestPath(verb_lattice, &verb_shortest);

            if (verb_shortest.Start() == fst::kNoStateId)
                continue;

            std::string result = fst_output_string(verb_shortest);
            if (!result.empty()) {
                if (!verbalized.empty()) verbalized += " ";
                verbalized += result;
                token_ok = true;
                break;
            }
        }

        if (!token_ok) {
            strncpy(output, input, output_max_len - 1);
            output[output_max_len - 1] = '\0';
            return -1;
        }
    }

    if (verbalized.empty()) {
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    /* Collapse duplicate spaces, trim */
    verbalized = collapse_spaces(trim(verbalized));

    /* ---- Step 4: Post-processing ---- */
    if (norm->has_post_process && !verbalized.empty()) {
        std::string esc_verb = fst_escape(verbalized);
        GrmManager::MutableTransducer pp_input_fst;
        if (string_to_fst(esc_verb, &pp_input_fst)) {
            fst::ComposeFst<StdArc> pp_lattice(pp_input_fst, *norm->post_process_fst);
            GrmManager::MutableTransducer pp_shortest;
            fst::ShortestPath(pp_lattice, &pp_shortest);

            if (pp_shortest.Start() != fst::kNoStateId &&
                pp_shortest.NumStates() > 0) {
                std::string pp_result = fst_output_string(pp_shortest);
                if (!pp_result.empty()) {
                    verbalized = collapse_spaces(trim(pp_result));
                }
            }
        }
    }

    /* Copy result to output buffer */
    if ((int)verbalized.size() >= output_max_len) {
        strncpy(output, verbalized.c_str(), output_max_len - 1);
        output[output_max_len - 1] = '\0';
    } else {
        strcpy(output, verbalized.c_str());
    }

    return 0;
}

void nemo_normalizer_destroy(NemoNormalizer *norm) {
    delete norm;
}

} /* extern "C" */
