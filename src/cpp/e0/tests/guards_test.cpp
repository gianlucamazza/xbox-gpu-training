#include "../model.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

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

  const auto published = root / "results" / "publish" / "status.json";
  std::filesystem::create_directories(published.parent_path());
  e0::atomic_json(published, e0::Json{{"n", 1}});
  e0::atomic_json(published, e0::Json{{"n", 2}});
  check(e0::read_json(published).at("n") == 2, "second publish missing");
  const auto stable_tmp = std::filesystem::path(e0::path_text(published) + ".tmp");
  const auto phase = std::filesystem::path(e0::path_text(published) + ".phase");
  check(!std::filesystem::exists(stable_tmp), "stable tmp left behind");
  check(!std::filesystem::exists(phase), "phase left behind");
  for (const auto &entry : std::filesystem::directory_iterator(published.parent_path()))
    check(entry.path().filename().string().find(".partial") == std::string::npos,
          "partial left behind");
  const auto kept = e0::Json{{"state", "kept"}};
  e0::atomic_json(published, kept);
  bool rejected = false;
  try {
    e0::atomic_json(published, e0::Json(std::string("\xC3\x28")));
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "invalid text was published");
  check(e0::read_json(published) == kept, "failed publish replaced the previous file");
  check(!std::filesystem::exists(stable_tmp), "failed publish left a stable tmp");
  const std::string big(1024 * 1024 + 1, 'a');
  e0::atomic_json(published, e0::Json{{"blob", big}});
  check(e0::read_json(published).at("blob").get<std::string>().size() == big.size(),
        "large body mismatch");
  check(!std::filesystem::exists(phase), "large publish left a phase file");
#ifndef _WIN32
  // NAME_MAX is 255. A 244-byte leaf keeps "<leaf>.phase" (250) inside the limit.
  // The unique partial adds ".<tick>-<id>.partial" (at least 12 bytes), so the body
  // open fails after the phase file has already been removed.
  const auto open_dir = root / "results" / "open-fail";
  std::filesystem::create_directories(open_dir);
  const auto overlong = open_dir / std::string(244, 'n');
  bool opened = false;
  std::string open_error;
  try {
    e0::atomic_json(overlong, e0::Json{{"n", 1}});
    opened = true;
  } catch (const std::exception &error) {
    open_error = error.what();
  }
  check(!opened, "overlong partial open succeeded");
  check(open_error.rfind("JSON write failed: ", 0) == 0, "open failure omitted the path diagnostic");
  check(open_error.find(overlong.filename().string()) != std::string::npos,
        "open failure omitted the partial path");
  check(open_error.find("errno=" + std::to_string(ENAMETOOLONG)) != std::string::npos,
        "open failure was not ENAMETOOLONG");
  check(!std::filesystem::exists(std::filesystem::path(e0::path_text(overlong) + ".phase")),
        "failed open left a phase file");
#endif
  return failures ? 1 : 0;
}
