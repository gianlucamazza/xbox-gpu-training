#include "../model.h"
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace {
int failures = 0;
void check(bool ok, const char *message) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
void rejects(e0::Command command, std::array<size_t, 5> sizes) {
  try { e0::validate_command(command, sizes); check(false, "invalid command accepted"); }
  catch (const std::runtime_error &) {}
}
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  e0::Command p{e0::Op::Add}; p.count = 1;
  rejects(p, {});
  e0::CpuKernel kernel;
  try { kernel.run(p, e0::Values{}, {}, {}, {}, {}); check(false, "empty CPU buffer accepted"); }
  catch (const std::runtime_error &) {}
  p.op = e0::Op::Linear; p.cols = 1; p.rows = 1;
  rejects(p, {1, 1, 0, 0, 0}); // out = 0 used to divide by zero.
  p.op = e0::Op::Rope; p.seq = 1; p.cols = 3; p.count = 3;
  rejects(p, {3, 0, 0, 0, 0}); // odd width reads an unpaired lane.
  p.op = e0::Op::Norm; p.cols = 1; p.count = 1; p.epsilon = 0;
  rejects(p, {1, 1, 0, 0, 0});
  p.op = e0::Op::Heads; p.batch = UINT32_MAX; p.seq = UINT32_MAX;
  p.cols = UINT32_MAX; p.heads = 1;
  rejects(p, {1, 0, 0, 0, 0});
  e0::Json fixture = {{"cases", {{{"id", "bad-token"},
      {"command", {{"op", uint32_t(e0::Op::Embed)}, {"count", 1}, {"rows", 1}, {"cols", 1}}},
      {"inputs", {{-1.0f}, {0.0f}, e0::Json::array(), e0::Json::array(), e0::Json::array()}}}}}};
  try { e0::kernel_fixture_report(fixture, kernel); check(false, "negative token accepted"); }
  catch (const std::runtime_error &) {}
  const auto root = std::filesystem::path(argv[1]);
  std::filesystem::create_directories(root / "results" / "duplicate");
  const auto original = e0::Json{{"state", "completed"}, {"job_sha256", "trusted"},
                               {"branches", {"original"}}};
  e0::atomic_json(root / "results/duplicate/status.json", original);
  e0::record_failure(root / "duplicate.job.json", "explicit resume required");
  check(e0::read_json(root / "results/duplicate/status.json") == original,
        "refusal changed committed status");
  check(e0::read_json(root / "duplicate.rejected.json").at("error") == "explicit resume required",
        "refusal diagnostic missing");
  e0::record_failure(root / "new.job.json", "bad input");
  check(e0::read_json(root / "results/new/status.json").at("state") == "failed",
        "early failure without previous execution not reported");
  return failures ? 1 : 0;
}
