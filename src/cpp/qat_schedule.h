#pragma once

// Fase 5 QAT + WSD schedule. Semantics: docs/qat-wsd.md.
// Isolated cooldowns overlay WSD; they are not merged into decay.
// English comments only. No CUDA. DirectML is not the trainer.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

constexpr const char* kQatWsdSchema = "xbox-gpu-training.qat.wsd.v1";
constexpr const char* kQatWsdDefaultName = "qat-wsd-smoke.json";

// ADR 0002 AdamW constants — dry-run rejects drift.
constexpr float kAdr0002BaseLr = 1.0e-3f;
constexpr float kAdr0002Beta1 = 0.9f;
constexpr float kAdr0002Beta2 = 0.999f;
constexpr float kAdr0002Eps = 1.0e-8f;
constexpr float kAdr0002WeightDecay = 0.01f;
constexpr float kAdr0002HyperTol = 1.0e-8f;

enum class QatBitWidth {
  Ternary = 0,
  Bits2 = 2,
  Bits4 = 4,
};

enum class WsdDecayKind {
  Linear = 0,
};

struct QatCooldown {
  std::string id;
  std::uint32_t start_step = 0;
  std::uint32_t steps = 0;
  float end_lr = 0.0f;
};

struct QatSchedule {
  std::string schema;
  std::string name;
  QatBitWidth bit_width = QatBitWidth::Ternary;
  float base_lr = kAdr0002BaseLr;
  float min_lr = 1.0e-4f;
  float beta1 = kAdr0002Beta1;
  float beta2 = kAdr0002Beta2;
  float eps = kAdr0002Eps;
  float weight_decay = kAdr0002WeightDecay;
  std::uint32_t warmup_steps = 0;
  std::uint32_t stable_steps = 0;
  std::uint32_t decay_steps = 0;
  WsdDecayKind decay = WsdDecayKind::Linear;
  std::vector<QatCooldown> cooldowns;
  std::uint32_t smoke_steps = 0;
};

struct LrSample {
  std::uint32_t step = 0;
  float lr = 0.0f;
  std::string phase;
  bool in_cooldown = false;
};

std::uint32_t WsdLength(const QatSchedule& sched);

bool ParseQatBitWidth(const std::string& text, QatBitWidth& out, std::string& err);
const char* QatBitWidthName(QatBitWidth w);

// WSD-only LR (ignores cooldown overlays).
float WsdLrAt(const QatSchedule& sched, std::uint32_t step);

// Effective LR: cooldown overlay if `step` is inside a window, else WSD.
float LrAtStep(const QatSchedule& sched, std::uint32_t step);

// "warmup" | "stable" | "decay" | "cooldown:<id>" | "after-wsd"
std::string PhaseAt(const QatSchedule& sched, std::uint32_t step);

bool CooldownIndexAt(const QatSchedule& sched, std::uint32_t step, std::size_t& index);

bool ValidateQatSchedule(const QatSchedule& sched, std::string& err);

// Require smoke.steps to enter at least one cooldown window.
bool ValidateSmokeSteps(const QatSchedule& sched, std::uint32_t steps, std::string& err);

void BuildLrTable(const QatSchedule& sched, std::uint32_t steps, std::vector<LrSample>& out);

bool LoadQatSchedule(const std::filesystem::path& path, QatSchedule& sched, std::string& err);

// Search hint, examples/, exe-adjacent, repo walk. Empty hint → qat-wsd-smoke.json.
std::filesystem::path ResolveQatSchedule(const std::filesystem::path& hint);
