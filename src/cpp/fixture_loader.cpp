#include "fixture_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace {

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;
};

class Parser {
 public:
  explicit Parser(std::string text) : text_(std::move(text)) {}

  bool Parse(Json& out, std::string& err) {
    Skip();
    if (!ParseValue(out, err)) {
      return false;
    }
    Skip();
    if (i_ != text_.size()) {
      err = "trailing JSON at " + std::to_string(i_);
      return false;
    }
    return true;
  }

 private:
  std::string text_;
  std::size_t i_ = 0;

  void Skip() {
    while (i_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[i_]))) {
      ++i_;
    }
  }

  bool Peek(char c) {
    Skip();
    return i_ < text_.size() && text_[i_] == c;
  }

  bool Consume(char c, std::string& err) {
    Skip();
    if (i_ >= text_.size() || text_[i_] != c) {
      err = std::string("expected '") + c + "' at " + std::to_string(i_);
      return false;
    }
    ++i_;
    return true;
  }

  bool ParseValue(Json& out, std::string& err) {
    Skip();
    if (i_ >= text_.size()) {
      err = "unexpected end of JSON";
      return false;
    }
    const char c = text_[i_];
    if (c == '{') {
      return ParseObject(out, err);
    }
    if (c == '[') {
      return ParseArray(out, err);
    }
    if (c == '"') {
      return ParseString(out, err);
    }
    if (c == 't' || c == 'f') {
      return ParseBool(out, err);
    }
    if (c == 'n') {
      return ParseNull(out, err);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      return ParseNumber(out, err);
    }
    err = std::string("unexpected '") + c + "' at " + std::to_string(i_);
    return false;
  }

  bool ParseObject(Json& out, std::string& err) {
    if (!Consume('{', err)) {
      return false;
    }
    out.type = Json::Object;
    Skip();
    if (Peek('}')) {
      ++i_;
      return true;
    }
    while (true) {
      Json key;
      if (!ParseString(key, err)) {
        return false;
      }
      if (!Consume(':', err)) {
        return false;
      }
      Json val;
      if (!ParseValue(val, err)) {
        return false;
      }
      out.o.emplace(key.s, std::move(val));
      Skip();
      if (Peek('}')) {
        ++i_;
        return true;
      }
      if (!Consume(',', err)) {
        return false;
      }
    }
  }

  bool ParseArray(Json& out, std::string& err) {
    if (!Consume('[', err)) {
      return false;
    }
    out.type = Json::Array;
    Skip();
    if (Peek(']')) {
      ++i_;
      return true;
    }
    while (true) {
      Json val;
      if (!ParseValue(val, err)) {
        return false;
      }
      out.a.push_back(std::move(val));
      Skip();
      if (Peek(']')) {
        ++i_;
        return true;
      }
      if (!Consume(',', err)) {
        return false;
      }
    }
  }

  bool ParseString(Json& out, std::string& err) {
    if (!Consume('"', err)) {
      return false;
    }
    out.type = Json::String;
    while (i_ < text_.size()) {
      const char c = text_[i_++];
      if (c == '"') {
        return true;
      }
      if (c == '\\' && i_ < text_.size()) {
        const char e = text_[i_++];
        if (e == '"' || e == '\\' || e == '/') {
          out.s.push_back(e);
        } else if (e == 'n') {
          out.s.push_back('\n');
        } else if (e == 't') {
          out.s.push_back('\t');
        } else {
          err = "unsupported JSON escape";
          return false;
        }
      } else {
        out.s.push_back(c);
      }
    }
    err = "unterminated string";
    return false;
  }

  bool ParseNumber(Json& out, std::string& err) {
    const std::size_t start = i_;
    if (text_[i_] == '-') {
      ++i_;
    }
    while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_]))) {
      ++i_;
    }
    if (i_ < text_.size() && text_[i_] == '.') {
      ++i_;
      while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_]))) {
        ++i_;
      }
    }
    if (i_ < text_.size() && (text_[i_] == 'e' || text_[i_] == 'E')) {
      ++i_;
      if (i_ < text_.size() && (text_[i_] == '+' || text_[i_] == '-')) {
        ++i_;
      }
      while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_]))) {
        ++i_;
      }
    }
    out.type = Json::Number;
    try {
      out.n = std::stod(text_.substr(start, i_ - start));
    } catch (...) {
      err = "invalid number at " + std::to_string(start);
      return false;
    }
    return true;
  }

  bool ParseBool(Json& out, std::string& err) {
    if (text_.compare(i_, 4, "true") == 0) {
      i_ += 4;
      out.type = Json::Bool;
      out.b = true;
      return true;
    }
    if (text_.compare(i_, 5, "false") == 0) {
      i_ += 5;
      out.type = Json::Bool;
      out.b = false;
      return true;
    }
    err = "invalid bool at " + std::to_string(i_);
    return false;
  }

  bool ParseNull(Json& out, std::string& err) {
    if (text_.compare(i_, 4, "null") == 0) {
      i_ += 4;
      out.type = Json::Null;
      return true;
    }
    err = "invalid null at " + std::to_string(i_);
    return false;
  }
};

const Json* Get(const Json& o, const char* key) {
  if (o.type != Json::Object) {
    return nullptr;
  }
  const auto it = o.o.find(key);
  if (it == o.o.end()) {
    return nullptr;
  }
  return &it->second;
}

bool AsU32(const Json& v, std::uint32_t& out) {
  if (v.type != Json::Number || v.n < 0) {
    return false;
  }
  out = static_cast<std::uint32_t>(v.n);
  return true;
}

bool AsF32(const Json& v, float& out) {
  if (v.type != Json::Number) {
    return false;
  }
  out = static_cast<float>(v.n);
  return true;
}

bool AsString(const Json& v, std::string& out) {
  if (v.type != Json::String) {
    return false;
  }
  out = v.s;
  return true;
}

bool AsU32Array(const Json& v, std::vector<std::uint32_t>& out) {
  if (v.type != Json::Array) {
    return false;
  }
  out.clear();
  out.reserve(v.a.size());
  for (const Json& e : v.a) {
    std::uint32_t u = 0;
    if (!AsU32(e, u)) {
      return false;
    }
    out.push_back(u);
  }
  return true;
}

bool AsF32Array(const Json& v, std::vector<float>& out) {
  if (v.type != Json::Array) {
    return false;
  }
  out.clear();
  out.reserve(v.a.size());
  for (const Json& e : v.a) {
    float f = 0.0f;
    if (!AsF32(e, f)) {
      return false;
    }
    out.push_back(f);
  }
  return true;
}

bool LoadCoded(const Json& o, Flp2Coded& coded, std::string& err) {
  const Json* rows = Get(o, "rows");
  const Json* cols = Get(o, "cols");
  const Json* levels = Get(o, "levels");
  const Json* symbols = Get(o, "symbols");
  const Json* scales = Get(o, "scales");
  if (!rows || !cols || !levels || !symbols || !scales) {
    err = "coded tensor missing rows/cols/levels/symbols/scales";
    return false;
  }
  if (!AsU32(*rows, coded.rows) || !AsU32(*cols, coded.cols) || !AsU32(*levels, coded.levels) ||
      !AsU32Array(*symbols, coded.symbols) || !AsF32Array(*scales, coded.scales)) {
    err = "coded tensor has invalid types";
    return false;
  }
  return true;
}

}  // namespace

bool LoadFlp2Fixture(const std::filesystem::path& path, Flp2Fixture& fx, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "could not open fixture " + path.u8string();
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  Parser parser(ss.str());
  Json root;
  if (!parser.Parse(root, err)) {
    return false;
  }
  if (root.type != Json::Object) {
    err = "fixture root must be an object";
    return false;
  }

  const Json* schema = Get(root, "schema");
  const Json* name = Get(root, "name");
  const Json* cfg = Get(root, "config");
  const Json* tokens = Get(root, "tokens");
  const Json* emb = Get(root, "emb");
  const Json* blocks = Get(root, "blocks");
  const Json* norm = Get(root, "norm");
  const Json* expected = Get(root, "expected");
  if (!schema || !name || !cfg || !tokens || !emb || !blocks || !norm || !expected) {
    err = "fixture missing required keys (schema/name/config/tokens/emb/blocks/norm/expected)";
    return false;
  }
  if (!AsString(*schema, fx.schema) || !AsString(*name, fx.name)) {
    err = "schema/name must be strings";
    return false;
  }
  if (fx.schema != "xbox-gpu-training.fixture.flp2.v1") {
    err = "unsupported fixture schema: " + fx.schema;
    return false;
  }

  auto need_u = [&](const char* k, std::uint32_t& dst) -> bool {
    const Json* v = Get(*cfg, k);
    if (!v || !AsU32(*v, dst)) {
      err = std::string("config.") + k + " missing or invalid";
      return false;
    }
    return true;
  };
  auto need_f = [&](const char* k, float& dst) -> bool {
    const Json* v = Get(*cfg, k);
    if (!v || !AsF32(*v, dst)) {
      err = std::string("config.") + k + " missing or invalid";
      return false;
    }
    return true;
  };
  if (!need_u("seq", fx.cfg.seq) || !need_u("d", fx.cfg.d) || !need_u("n_heads", fx.cfg.n_heads) ||
      !need_u("d_ff", fx.cfg.d_ff) || !need_u("vocab", fx.cfg.vocab) ||
      !need_u("levels", fx.cfg.levels) || !need_f("rms_eps", fx.cfg.eps) ||
      !need_f("rope_theta", fx.cfg.rope_theta)) {
    return false;
  }

  const Json* mlp = Get(*cfg, "mlp");
  std::string mlp_s;
  if (!mlp || !AsString(*mlp, mlp_s) || mlp_s != "relu2") {
    err = "this Fase 2 fixture requires config.mlp == \"relu2\" (unambiguous)";
    return false;
  }
  const Json* policy = Get(*cfg, "scale_policy");
  std::string policy_s;
  if (!policy || !AsString(*policy, policy_s) || policy_s != "row16") {
    err = "this Fase 2 fixture requires config.scale_policy == \"row16\"";
    return false;
  }

  if (!AsU32Array(*tokens, fx.tokens)) {
    err = "tokens must be a uint array";
    return false;
  }
  if (!LoadCoded(*emb, fx.emb, err)) {
    return false;
  }
  if (blocks->type != Json::Array || blocks->a.size() != 1) {
    err = "fixture must contain exactly one block (tiny net)";
    return false;
  }
  const Json& blk = blocks->a[0];
  const Json* n1 = Get(blk, "norm1");
  const Json* n2 = Get(blk, "norm2");
  const Json* qkv = Get(blk, "qkv");
  const Json* proj = Get(blk, "proj");
  const Json* fc = Get(blk, "fc");
  const Json* fc2 = Get(blk, "fc2");
  if (!n1 || !n2 || !qkv || !proj || !fc || !fc2) {
    err = "block missing norm1/norm2/qkv/proj/fc/fc2";
    return false;
  }
  if (!AsF32Array(*n1, fx.norm1) || !AsF32Array(*n2, fx.norm2) || !AsF32Array(*norm, fx.norm)) {
    err = "norm vectors must be float arrays";
    return false;
  }
  if (!LoadCoded(*qkv, fx.qkv, err) || !LoadCoded(*proj, fx.proj, err) ||
      !LoadCoded(*fc, fx.fc, err) || !LoadCoded(*fc2, fx.fc2, err)) {
    return false;
  }

  const Json* logits = Get(*expected, "logits");
  if (!logits || !AsF32Array(*logits, fx.expected_logits)) {
    err = "expected.logits must be a float array";
    return false;
  }
  return true;
}

std::filesystem::path ResolveFlp2Fixture(const std::filesystem::path& hint) {
  std::vector<std::filesystem::path> candidates;
  auto push = [&](std::filesystem::path p) {
    if (p.empty()) {
      return;
    }
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    if (ec) {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
      candidates.push_back(std::move(p));
    }
  };

  push(hint);
  const std::string leaf = hint.empty() ? std::string("tiny_flp2.json") : hint.filename().string();

  std::vector<std::filesystem::path> roots;
  auto add_root = [&](std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    if (!ec && std::find(roots.begin(), roots.end(), p) == roots.end()) {
      roots.push_back(std::move(p));
    }
  };
  add_root(std::filesystem::current_path());
  std::error_code ec;
  std::filesystem::path walk = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !ec; ++i) {
    add_root(walk);
    const auto parent = walk.parent_path();
    if (parent == walk) {
      break;
    }
    walk = parent;
  }

  for (const auto& root : roots) {
    push(root / hint);
    push(root / "fixtures" / leaf);
    push(root / "benchmarks" / "fixtures" / leaf);
    push(root / "benchmarks" / leaf);
  }

  for (const auto& path : candidates) {
    std::error_code exists_ec;
    if (std::filesystem::exists(path, exists_ec) && !exists_ec) {
      return path;
    }
  }
  return {};
}
