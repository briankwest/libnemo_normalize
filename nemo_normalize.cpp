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
 *
 * No Thrax dependency — uses OpenFst FAR API directly.
 */

#include "nemo_normalize.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <regex>
#include <string>
#include <variant>
#include <vector>

#include <fst/fstlib.h>
#include <fst/extensions/far/far.h>

using fst::StdArc;
using fst::StdFst;
typedef fst::VectorFst<StdArc> StoredFst;

/* ========================================================================
 * FAR loader — replaces thrax::GrmManager
 * ======================================================================== */

static StoredFst *load_fst_from_far(const char *far_path, const char *rule_name) {
    std::unique_ptr<fst::FarReader<StdArc>> reader(
        fst::FarReader<StdArc>::Open(far_path));
    if (!reader) {
        std::cerr << "load_fst_from_far: cannot open " << far_path << std::endl;
        return nullptr;
    }

    while (!reader->Done()) {
        if (reader->GetKey() == rule_name) {
            const StdFst *fst = reader->GetFst();
            if (fst) return new StoredFst(*fst);
        }
        reader->Next();
    }

    std::cerr << "load_fst_from_far: rule '" << rule_name
              << "' not found in " << far_path << std::endl;
    return nullptr;
}

/* ========================================================================
 * Tag Parser  (port of token_parser.py)
 * ======================================================================== */

static const char EOS_CHAR = '\0';
static const std::string PRESERVE_ORDER_KEY = "preserve_order";

struct TagValue;
using TagDict = std::vector<std::pair<std::string, TagValue>>;

struct TagValue {
    bool is_string;
    std::string str_val;
    TagDict dict_val;

    static TagValue make_string(const std::string &s) {
        TagValue v; v.is_string = true; v.str_val = s; return v;
    }
    static TagValue make_dict(const TagDict &d) {
        TagValue v; v.is_string = false; v.dict_val = d; return v;
    }
    static TagValue make_bool() {
        TagValue v; v.is_string = true; v.str_val = "true"; return v;
    }
};

class TagParser {
public:
    void init(const std::string &text) {
        text_ = text; len_ = (int)text.size(); idx_ = 0;
        ch_ = len_ > 0 ? text_[0] : EOS_CHAR;
    }

    std::vector<TagDict> parse() {
        std::vector<TagDict> result;
        while (parse_ws()) {
            TagDict token;
            if (!parse_token(token)) break;
            result.push_back(token);
        }
        return result;
    }

private:
    std::string text_; int len_; int idx_; char ch_;

    bool read() {
        if (idx_ < len_ - 1) { idx_++; ch_ = text_[idx_]; return true; }
        ch_ = EOS_CHAR; return false;
    }
    bool parse_ws() {
        bool not_eos = (ch_ != EOS_CHAR);
        while (not_eos && ch_ == ' ') not_eos = read();
        return not_eos;
    }
    bool parse_char(char exp) {
        if (ch_ != exp) return false;
        read();
        return true;
    }
    bool parse_chars(const char *exp) {
        for (const char *p = exp; *p; p++) if (!parse_char(*p)) return false;
        return true;
    }
    bool parse_string_key(std::string &out) {
        if (ch_ == EOS_CHAR || ch_ == ' ' || ch_ == '\t' || ch_ == '\n') return false;
        out.clear();
        while ((ch_ >= 'a' && ch_ <= 'z') || (ch_ >= 'A' && ch_ <= 'Z') || ch_ == '_') {
            out.push_back(ch_); if (!read()) break;
        }
        return !out.empty();
    }
    bool parse_string_value(std::string &out) {
        if (ch_ == EOS_CHAR) return false;
        out.clear();
        while (true) {
            if (ch_ == '"') {
                char next = (idx_ + 1 < len_) ? text_[idx_ + 1] : ' ';
                if (next == ' ' || next == '}' || idx_ + 1 >= len_) break;
            }
            out.push_back(ch_);
            if (!read()) return false;
        }
        return true;
    }
    bool parse_token_value(TagValue &out) {
        if (ch_ == ':') {
            parse_char(':'); parse_ws();
            if (!parse_char('"')) return false;
            std::string val;
            if (!parse_string_value(val)) return false;
            if (!parse_char('"')) return false;
            out = TagValue::make_string(val); return true;
        } else if (ch_ == '{') {
            parse_char('{');
            auto list_dicts = parse();
            TagDict merged;
            for (auto &td : list_dicts) for (auto &kv : td) merged.push_back(kv);
            if (!parse_char('}')) return false;
            out = TagValue::make_dict(merged); return true;
        }
        return false;
    }
    bool parse_token(TagDict &out) {
        std::string key;
        if (!parse_string_key(key)) return false;
        parse_ws();
        TagValue val;
        if (key == PRESERVE_ORDER_KEY) {
            parse_char(':'); parse_ws(); parse_chars("true");
            val = TagValue::make_bool();
        } else {
            if (!parse_token_value(val)) return false;
        }
        out.push_back({key, val}); return true;
    }
};

/* ========================================================================
 * Permutation Generator
 * ======================================================================== */

static bool has_preserve_order(const TagDict &d) {
    for (auto &kv : d) if (kv.first == PRESERVE_ORDER_KEY) return true;
    return false;
}

static std::vector<std::string> permute_dict(const TagDict &d) {
    std::vector<int> indices(d.size());
    std::iota(indices.begin(), indices.end(), 0);
    bool preserve = has_preserve_order(d);
    std::vector<std::string> results;

    do {
        std::vector<std::string> sub = {""};
        for (int i : indices) {
            const auto &key = d[i].first;
            const auto &val = d[i].second;
            if (key == PRESERVE_ORDER_KEY) {
                std::vector<std::string> new_sub;
                for (auto &prefix : sub)
                    new_sub.push_back(prefix + "preserve_order: true ");
                sub = std::move(new_sub);
            } else if (val.is_string) {
                std::string frag = key + ": \"" + val.str_val + "\" ";
                std::vector<std::string> new_sub;
                for (auto &prefix : sub) new_sub.push_back(prefix + frag);
                sub = std::move(new_sub);
            } else {
                auto rec = permute_dict(val.dict_val);
                std::vector<std::string> new_sub;
                for (auto &prefix : sub)
                    for (auto &r : rec)
                        new_sub.push_back(prefix + " " + key + " { " + r + " } ");
                sub = std::move(new_sub);
            }
        }
        for (auto &s : sub) results.push_back(s);
    } while (!preserve && std::next_permutation(indices.begin(), indices.end()));

    return results;
}

static void generate_permutations_helper(
    const std::vector<TagDict> &tokens, size_t idx,
    const std::string &prefix, std::vector<std::string> &out) {
    if (idx == tokens.size()) { out.push_back(prefix); return; }
    auto opts = permute_dict(tokens[idx]);
    for (auto &opt : opts)
        generate_permutations_helper(tokens, idx + 1, prefix + opt, out);
}

/* ========================================================================
 * FST helpers
 * ======================================================================== */

static bool string_to_fst(const std::string &input, StoredFst *fst_out) {
    fst::StringCompiler<StdArc> compiler;
    return compiler(input, fst_out);
}

static std::string fst_output_string(const StdFst &fst) {
    std::string result;
    auto state = fst.Start();
    if (state == fst::kNoStateId) return result;
    while (fst.Final(state) == StdArc::Weight::Zero()) {
        fst::ArcIterator<StdFst> aiter(fst, state);
        if (aiter.Done()) break;
        const auto &arc = aiter.Value();
        if (arc.olabel != 0) result.push_back((char)arc.olabel);
        state = arc.nextstate;
    }
    return result;
}

static std::string fst_escape(const std::string &s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c == '[' || c == ']' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

static std::string collapse_spaces(const std::string &s) {
    std::string out; out.reserve(s.size());
    bool prev_space = false;
    for (char c : s) {
        if (c == ' ') { if (!prev_space) out.push_back(c); prev_space = true; }
        else { out.push_back(c); prev_space = false; }
    }
    return out;
}

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

    /* Load tagger FST from FAR */
    norm->tagger_fst.reset(load_fst_from_far(classify_far_path, "TOKENIZE_AND_CLASSIFY"));
    if (!norm->tagger_fst) { delete norm; return nullptr; }

    /* Load verbalizer FST from FAR */
    norm->verbalizer_fst.reset(load_fst_from_far(verbalize_far_path, "ALL"));
    if (!norm->verbalizer_fst) { delete norm; return nullptr; }

    /* Optionally load post-processing FST */
    if (post_process_far_path && post_process_far_path[0] != '\0') {
        StoredFst *pp = load_fst_from_far(post_process_far_path, "POSTPROCESSOR");
        if (pp) {
            norm->post_process_fst.reset(pp);
            norm->has_post_process = true;
        }
    }

    return norm;
}

int nemo_normalize(
    NemoNormalizer *norm, const char *input,
    char *output, int output_max_len
) {
    if (!norm || !input || !output || output_max_len <= 0) {
        if (output && output_max_len > 0) output[0] = '\0';
        return -1;
    }

    std::string text = trim(std::string(input));
    if (text.empty()) { output[0] = '\0'; return 0; }

    /* Step 1: Tag (classify) */
    StoredFst input_fst;
    if (!string_to_fst(text, &input_fst)) { output[0] = '\0'; return -1; }

    fst::ComposeFst<StdArc> tagged_lattice(input_fst, *norm->tagger_fst);
    StoredFst tagged_shortest;
    fst::ShortestPath(tagged_lattice, &tagged_shortest);

    if (tagged_shortest.Start() == fst::kNoStateId) {
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

    /* Step 2: Parse tagged text */
    TagParser parser;
    parser.init(tagged_text);
    auto tokens = parser.parse();
    if (tokens.empty()) {
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    /* Step 3: Verbalize each token */
    std::string verbalized;
    for (auto &token_dict : tokens) {
        TagDict inner;
        for (auto &kv : token_dict)
            if (kv.first == "tokens" && !kv.second.is_string)
                inner = kv.second.dict_val;

        TagDict &to_permute = inner.empty() ? token_dict : inner;
        auto perms = permute_dict(to_permute);
        bool token_ok = false;

        for (auto &perm : perms) {
            std::string trimmed = trim(perm);
            if (trimmed.empty()) continue;
            StoredFst perm_fst;
            if (!string_to_fst(trimmed, &perm_fst)) continue;
            fst::ComposeFst<StdArc> vlat(perm_fst, *norm->verbalizer_fst);
            StoredFst vsp;
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
    }

    if (verbalized.empty()) {
        strncpy(output, input, output_max_len - 1);
        output[output_max_len - 1] = '\0';
        return -1;
    }

    verbalized = collapse_spaces(trim(verbalized));

    /* Step 4: Post-processing */
    if (norm->has_post_process && !verbalized.empty()) {
        std::string esc_verb = fst_escape(verbalized);
        StoredFst pp_input_fst;
        if (string_to_fst(esc_verb, &pp_input_fst)) {
            fst::ComposeFst<StdArc> pp_lattice(pp_input_fst, *norm->post_process_fst);
            StoredFst pp_shortest;
            fst::ShortestPath(pp_lattice, &pp_shortest);
            if (pp_shortest.Start() != fst::kNoStateId && pp_shortest.NumStates() > 0) {
                std::string pp_result = fst_output_string(pp_shortest);
                if (!pp_result.empty()) verbalized = collapse_spaces(trim(pp_result));
            }
        }
    }

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
