#include "qat_schedule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

bool AsF32(const Json& v, float& out) {
  if (v.type != Json::Number) {
    return false;
  }
  out = static_cast<float>(v.n);
  return true;
}

bool AsU32(const Json& v, std::uint32_t& out) {
  if (v.type != Json::Number || v.n < 0) {
    return false;
  }
  out = static_cast<std::uint32_t>(v.n);
  return true;
}

bool AsString(const Json& v, std::string& out) {
  if (v.type != Json::String) {
    return false;
  }
  out = v.s;
  return true;
}

bool Close(float a, float b) {
  return std::fabs(a - b) <= kAdr0002HyperTol;
}

float Lerp(float a, float b, std::uint32_t i, std::uint32_t n) {
  if (n <= 1) {
    return b;
  }
  return a + (b - a) * (static_cast<float>(i) / static_cast<float>(n - 1));
}

void PushCandidate(std::vector<std::filesystem::path>& candidates, std::filesystem::path p) {
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
}

}  // namespace

std::uint32_t WsdLength(const QatSchedule& sched) {
  return sched.warmup_steps + sched.stable_steps + sched.decay_steps;
}

bool ParseQatBitWidth(const std::string& text, QatBitWidth& out, std::string& err) {
  if (text == "ternary") {
    out = QatBitWidth::Ternary;
    return true;
  }
  if (text == "2" || text == "2bit") {
    out = QatBitWidth::Bits2;
    return true;
  }
  if (text == "4" || text == "4bit") {
    out = QatBitWidth::Bits4;
    return true;
  }
  err = "bit_width must be ternary|2|4 (got '" + text + "')";
  return false;
}

const char* QatBitWidthName(QatBitWidth w) {
  switch (w) {
    case QatBitWidth::Ternary:
      return "ternary";
    case QatBitWidth::Bits2:
      return "2";
    case QatBitWidth::Bits4:
      return "4";
  }
  return "unknown";
}

float WsdLrAt(const QatSchedule& sched, std::uint32_t step) {
  if (step < sched.warmup_steps) {
    return Lerp(0.0f, sched.base_lr, step, sched.warmup_steps);
  }
  if (step < sched.warmup_steps + sched.stable_steps) {
    return sched.base_lr;
  }
  if (step < WsdLength(sched)) {
    return Lerp(sched.base_lr, sched.min_lr, step - sched.warmup_steps - sched.stable_steps,
                sched.decay_steps);
  }
  return sched.min_lr;
}

bool CooldownIndexAt(const QatSchedule& sched, std::uint32_t step, std::size_t& index) {
  for (std::size_t i = 0; i < sched.cooldowns.size(); ++i) {
    const QatCooldown& cd = sched.cooldowns[i];
    if (step >= cd.start_step && step < cd.start_step + cd.steps) {
      index = i;
      return true;
    }
  }
  return false;
}

float LrAtStep(const QatSchedule& sched, std::uint32_t step) {
  std::size_t idx = 0;
  if (!CooldownIndexAt(sched, step, idx)) {
    return WsdLrAt(sched, step);
  }
  const QatCooldown& cd = sched.cooldowns[idx];
  const float start_lr = WsdLrAt(sched, cd.start_step);
  return Lerp(start_lr, cd.end_lr, step - cd.start_step, cd.steps);
}

std::string PhaseAt(const QatSchedule& sched, std::uint32_t step) {
  std::size_t idx = 0;
  if (CooldownIndexAt(sched, step, idx)) {
    return "cooldown:" + sched.cooldowns[idx].id;
  }
  if (step < sched.warmup_steps) {
    return "warmup";
  }
  if (step < sched.warmup_steps + sched.stable_steps) {
    return "stable";
  }
  if (step < WsdLength(sched)) {
    return "decay";
  }
  return "after-wsd";
}

bool ValidateQatSchedule(const QatSchedule& sched, std::string& err) {
  if (sched.schema != kQatWsdSchema) {
    err = "unsupported schedule schema: " + sched.schema;
    return false;
  }
  if (sched.name.empty()) {
    err = "schedule name must be non-empty";
    return false;
  }
  if (sched.base_lr <= 0.0f || !Close(sched.base_lr, kAdr0002BaseLr)) {
    err = "optimizer.base_lr must stay ADR 0002 1e-3";
    return false;
  }
  if (!Close(sched.beta1, kAdr0002Beta1) || !Close(sched.beta2, kAdr0002Beta2) ||
      !Close(sched.eps, kAdr0002Eps) || !Close(sched.weight_decay, kAdr0002WeightDecay)) {
    err = "AdamW beta1/beta2/eps/weight_decay must stay ADR 0002";
    return false;
  }
  if (sched.warmup_steps < 1 || sched.stable_steps < 1 || sched.decay_steps < 1) {
    err = "wsd warmup/stable/decay steps must be >= 1";
    return false;
  }
  if (sched.decay != WsdDecayKind::Linear) {
    err = "wsd.decay must be linear";
    return false;
  }
  if (sched.min_lr < 0.0f || sched.min_lr > sched.base_lr) {
    err = "wsd.min_lr must be in [0, base_lr]";
    return false;
  }
  if (sched.cooldowns.empty()) {
    err = "cooldowns must be a non-empty array (isolation is mandatory)";
    return false;
  }
  const std::uint32_t length = WsdLength(sched);
  struct Win {
    std::uint32_t lo;
    std::uint32_t hi;
    std::string id;
  };
  std::vector<Win> wins;
  wins.reserve(sched.cooldowns.size());
  for (const QatCooldown& cd : sched.cooldowns) {
    if (cd.steps < 1) {
      err = "cooldown " + cd.id + ": steps must be >= 1";
      return false;
    }
    if (cd.end_lr < 0.0f) {
      err = "cooldown " + cd.id + ": end_lr must be >= 0";
      return false;
    }
    if (cd.start_step + cd.steps > length) {
      err = "cooldown " + cd.id + ": window exceeds WsdLength";
      return false;
    }
    wins.push_back({cd.start_step, cd.start_step + cd.steps, cd.id});
  }
  std::sort(wins.begin(), wins.end(), [](const Win& a, const Win& b) { return a.lo < b.lo; });
  for (std::size_t i = 1; i < wins.size(); ++i) {
    if (wins[i - 1].hi > wins[i].lo) {
      err = "cooldown overlap: " + wins[i - 1].id + " vs " + wins[i].id;
      return false;
    }
  }
  if (sched.smoke_steps < 1) {
    err = "smoke.steps must be >= 1";
    return false;
  }
  if (sched.smoke_steps > length) {
    err = "smoke.steps exceeds WsdLength";
    return false;
  }
  return ValidateSmokeSteps(sched, sched.smoke_steps, err);
}

bool ValidateSmokeSteps(const QatSchedule& sched, std::uint32_t steps, std::string& err) {
  if (steps < 1) {
    err = "steps must be >= 1";
    return false;
  }
  if (steps > WsdLength(sched)) {
    err = "steps exceeds WsdLength";
    return false;
  }
  bool entered = false;
  for (const QatCooldown& cd : sched.cooldowns) {
    if (cd.start_step < steps) {
      entered = true;
      break;
    }
  }
  if (!entered) {
    err = "steps must cross at least one cooldown start_step";
    return false;
  }
  return true;
}

void BuildLrTable(const QatSchedule& sched, std::uint32_t steps, std::vector<LrSample>& out) {
  out.clear();
  out.reserve(steps);
  for (std::uint32_t s = 0; s < steps; ++s) {
    LrSample row;
    row.step = s;
    row.lr = LrAtStep(sched, s);
    row.phase = PhaseAt(sched, s);
    std::size_t idx = 0;
    row.in_cooldown = CooldownIndexAt(sched, s, idx);
    out.push_back(std::move(row));
  }
}

bool LoadQatSchedule(const std::filesystem::path& path, QatSchedule& sched, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "could not open schedule " + path.u8string();
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
    err = "schedule root must be an object";
    return false;
  }

  const Json* schema = Get(root, "schema");
  const Json* name = Get(root, "name");
  const Json* qat = Get(root, "qat");
  const Json* opt = Get(root, "optimizer");
  const Json* wsd = Get(root, "wsd");
  const Json* cds = Get(root, "cooldowns");
  const Json* smoke = Get(root, "smoke");
  if (!schema || !name || !qat || !opt || !wsd || !cds || !smoke) {
    err = "schedule missing schema/name/qat/optimizer/wsd/cooldowns/smoke";
    return false;
  }
  if (!AsString(*schema, sched.schema) || !AsString(*name, sched.name)) {
    err = "schema/name must be strings";
    return false;
  }

  const Json* master = Get(*qat, "master");
  std::string master_s;
  if (!master || !AsString(*master, master_s) || master_s != "fp32") {
    err = "qat.master must be \"fp32\"";
    return false;
  }
  const Json* bw = Get(*qat, "bit_width");
  std::string bw_s;
  if (!bw || !AsString(*bw, bw_s) || !ParseQatBitWidth(bw_s, sched.bit_width, err)) {
    if (err.empty()) {
      err = "qat.bit_width missing or invalid";
    }
    return false;
  }
  if (const Json* sg = Get(*qat, "scale_grad")) {
    if (sg->type != Json::Bool || sg->b) {
      err = "qat.scale_grad must be false";
      return false;
    }
  }

  const Json* otype = Get(*opt, "type");
  std::string otype_s;
  if (!otype || !AsString(*otype, otype_s) || otype_s != "adamw") {
    err = "optimizer.type must be adamw";
    return false;
  }
  if (const Json* dev = Get(*opt, "device")) {
    std::string dev_s;
    if (!AsString(*dev, dev_s) || dev_s != "host") {
      err = "optimizer.device must be host";
      return false;
    }
  }
  if (!Get(*opt, "base_lr") || !AsF32(*Get(*opt, "base_lr"), sched.base_lr) ||
      !Get(*opt, "beta1") || !AsF32(*Get(*opt, "beta1"), sched.beta1) ||
      !Get(*opt, "beta2") || !AsF32(*Get(*opt, "beta2"), sched.beta2) || !Get(*opt, "eps") ||
      !AsF32(*Get(*opt, "eps"), sched.eps) || !Get(*opt, "weight_decay") ||
      !AsF32(*Get(*opt, "weight_decay"), sched.weight_decay)) {
    err = "optimizer missing numeric ADR 0002 fields";
    return false;
  }

  if (!Get(*wsd, "warmup_steps") || !AsU32(*Get(*wsd, "warmup_steps"), sched.warmup_steps) ||
      !Get(*wsd, "stable_steps") || !AsU32(*Get(*wsd, "stable_steps"), sched.stable_steps) ||
      !Get(*wsd, "decay_steps") || !AsU32(*Get(*wsd, "decay_steps"), sched.decay_steps) ||
      !Get(*wsd, "min_lr") || !AsF32(*Get(*wsd, "min_lr"), sched.min_lr)) {
    err = "wsd missing warmup/stable/decay/min_lr";
    return false;
  }
  const Json* decay = Get(*wsd, "decay");
  std::string decay_s;
  if (!decay || !AsString(*decay, decay_s) || decay_s != "linear") {
    err = "wsd.decay must be \"linear\"";
    return false;
  }
  sched.decay = WsdDecayKind::Linear;

  if (cds->type != Json::Array || cds->a.empty()) {
    err = "cooldowns must be a non-empty array";
    return false;
  }
  sched.cooldowns.clear();
  for (std::size_t i = 0; i < cds->a.size(); ++i) {
    const Json& c = cds->a[i];
    QatCooldown cd;
    if (const Json* id = Get(c, "id")) {
      if (!AsString(*id, cd.id)) {
        err = "cooldown id must be a string";
        return false;
      }
    }
    if (cd.id.empty()) {
      cd.id = "cooldown-" + std::to_string(i);
    }
    if (!Get(c, "start_step") || !AsU32(*Get(c, "start_step"), cd.start_step) ||
        !Get(c, "steps") || !AsU32(*Get(c, "steps"), cd.steps) || !Get(c, "end_lr") ||
        !AsF32(*Get(c, "end_lr"), cd.end_lr)) {
      err = "cooldown " + cd.id + " needs start_step, steps, end_lr";
      return false;
    }
    sched.cooldowns.push_back(std::move(cd));
  }

  if (!Get(*smoke, "steps") || !AsU32(*Get(*smoke, "steps"), sched.smoke_steps)) {
    err = "smoke.steps missing or invalid";
    return false;
  }
  return ValidateQatSchedule(sched, err);
}

std::filesystem::path ResolveQatSchedule(const std::filesystem::path& hint) {
  std::vector<std::filesystem::path> candidates;
  PushCandidate(candidates, hint);
  const std::string leaf = hint.empty() ? std::string(kQatWsdDefaultName) : hint.filename().string();

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
    if (!hint.empty()) {
      PushCandidate(candidates, root / hint);
    }
    PushCandidate(candidates, root / "examples" / leaf);
    PushCandidate(candidates, root / leaf);
  }

  for (const auto& path : candidates) {
    std::error_code exists_ec;
    if (std::filesystem::is_regular_file(path, exists_ec) && !exists_ec) {
      return path;
    }
  }
  return {};
}
