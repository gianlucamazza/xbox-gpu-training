#include "../runtime_lifecycle.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using e0::Json;
namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool ok, const char *label) {
  if (!ok) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}
template <class F> bool throws(F &&f) {
  try {
    f();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}
void raw(const fs::path &p, const std::string &s) {
  std::ofstream out(p, std::ios::binary);
  out << s;
}
Json descriptor(const fs::path &root, const fs::path &p) {
  return {{"path", p.lexically_relative(root).generic_string()},
          {"bytes", fs::file_size(p)},
          {"sha256", e0::sha256_file(p)}};
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 2;
  const auto root =
      fs::temp_directory_path() /
      ("xgpu-lifecycle-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  try {
    {
      e0::WorkerLock lock(root / "worker.lock");
      check(throws([&] { e0::WorkerLock duplicate(root / "worker.lock"); }),
            "exclusive lock excludes a second owner");
    }
    {
      e0::WorkerLock reacquired(root / "worker.lock");
    }
    e0::RuntimeLifecycle live(root, "new-worker", 1, "package", "commit");
    live.publish();
    auto first = live.snapshot();
    live.publish();
    auto second = live.snapshot();
    check(second.at("heartbeat_seq") > first.at("heartbeat_seq"),
          "heartbeat ticks");
    check(second.at("progress") == first.at("progress"),
          "heartbeat never fabricates progress");
    std::thread observer([&] {
      for (unsigned n = 0; n < 1000; ++n)
        (void)live.snapshot();
    });
    for (unsigned n = 1; n <= 1000; ++n)
      live.gpu_progress(n, "readback");
    observer.join();
    check(live.snapshot().at("progress").at("sequence") == 1000,
          "completed GPU work counted safely");
    live.progress("cooldown", 9, 12);
    check(live.snapshot().at("progress").at("cooldown_step") == 12,
          "cooldown progress tracked");
    const auto inbox = root / "inbox";
    fs::create_directory(inbox);
    const auto result = inbox / "results" / "trial";
    auto initial = e0::read_json(
        fs::path(argv[1]) /
        "contracts/floppylm/fixtures/valid/floppylm.e0.weights.v1--tiny.json");
    e0::atomic_json(inbox / "initial.json", initial);
    raw(inbox / "data.bin", "abc");
    raw(inbox / "indices.bin", std::string(32, '\0'));
    Json job{{"schema", "floppylm.e0.job.v1"},
             {"job_id", "trial"},
             {"config", initial.at("config")},
             {"initialization", descriptor(inbox, inbox / "initial.json")},
             {"data", descriptor(inbox, inbox / "data.bin")},
             {"indices", descriptor(inbox, inbox / "indices.bin")},
             {"spec",
              {{"batch", 1},
               {"tokens", 8},
               {"branches", 3},
               {"warmup_frac", 0.02},
               {"cooldown_frac", 0.1},
               {"lr", 0.001},
               {"wd", 0.0}}}};
    job["stop_after"] = 1;
    const auto payload = job.dump(2) + "\n";
    raw(inbox / "trial.job.json", payload);
    raw(inbox / "trial.claimed", "");
    auto old = live.snapshot();
    old["worker_id"] = "old-worker";
    const auto owned = e0::persist_claim(inbox, "trial", old);
    check(e0::sha256_file(owned) == e0::sha256_bytes(payload),
          "exact submitted bytes preserved");
    auto owner = e0::read_json(inbox / "trial.owner.json");
    check(owner.at("job_payload") == payload, "claim stores exact payload");
    if (argc == 3) {
      fs::create_directories(argv[2]);
      e0::atomic_json(fs::path(argv[2]) / "worker.json", live.snapshot());
      e0::atomic_json(fs::path(argv[2]) / "claim.json", owner);
    }
    job["tensors"] = initial.at("tensors");
    job["initialization_sha256"] = job.at("initialization").at("sha256");
    fs::create_directories(result);
    e0::Model model(job);
    const auto checkpoint = model.checkpoint(0, job);
    e0::atomic_json(result / "checkpoint.json", checkpoint);
    Json status{{"state", "running"},
                {"job_id", "trial"},
                {"job_sha256", owner.at("job_sha256")},
                {"branches", Json::array()},
                {"checkpoint", descriptor(inbox, result / "checkpoint.json")}};
    auto reconcile = [&] {
      e0::reconcile_claims(inbox, live.snapshot());
      return e0::read_json(result / "status.json");
    };
    // An uploaded replacement is deliberately invalid: only immutable owner
    // bytes count.
    raw(inbox / "trial.job.json", "pending incomplete replacement");
    e0::atomic_json(result / "status.json", status);
    raw(inbox / "trial.ready", "pending resume");
    const auto reconciled = reconcile();
    if (reconciled.at("state") != "interrupted")
      std::cerr << reconciled.dump() << '\n';
    check(reconciled.at("state") == "interrupted",
          "orphan uses immutable payload, not replacement upload");
    check(!fs::exists(inbox / "trial.ready") &&
              fs::exists(inbox / "trial.ready.quarantined.new-worker"),
          "startup quarantines pending ready without executing it");
    const auto once = e0::sha256_file(result / "status.json");
    reconcile();
    check(e0::sha256_file(result / "status.json") == once,
          "repeated reconciliation is idempotent");
    auto resume_job = Json::parse(payload);
    resume_job["resume"] = status.at("checkpoint");
    resume_job.erase("stop_after");
    raw(inbox / "trial.job.json", resume_job.dump());
    const auto resume_owned =
        e0::persist_claim(inbox, "trial", live.snapshot());
    check(e0::sha256_file(resume_owned) == e0::sha256_bytes(resume_job.dump()),
          "explicit verified resume captures new payload");
    check(reconcile().at("state") == "interrupted",
          "resume publication crash retains original result binding");
    check(e0::read_json(inbox / "trial.owner.json") == owner,
          "old verified owner restored after publication crash");
    auto another = live.snapshot();
    another["worker_id"] = "another-worker";
    e0::persist_claim(inbox, "trial", another);
    check(reconcile().at("state") == "interrupted",
          "same checkpoint retry across worker instances is valid");
    auto completed = status;
    completed["state"] = "completed";
    e0::atomic_json(result / "status.json", completed);
    const auto completed_hash = e0::sha256_file(result / "status.json");
    reconcile();
    raw(inbox / "trial.job.json", payload);
    check(throws([&] { e0::persist_claim(inbox, "trial", live.snapshot()); }),
          "completed job cannot be claimed again");
    check(e0::read_json(inbox / "trial.owner.json") == owner,
          "duplicate claim preserves original binding");
    check(e0::sha256_file(result / "status.json") == completed_hash,
          "completed result untouched");
    auto failed = status;
    failed["state"] = "failed";
    failed["error"] = "nonfinite";
    e0::atomic_json(result / "status.json", failed);
    check(reconcile() == failed, "numerical failure never reclassified");
    auto fault = Json{{"kind", "gpu_wait_failed"},
                      {"error", "WAIT_FAILED"},
                      {"requested_fence", 4},
                      {"completed_fence", 3},
                      {"elapsed_ms", 5}};
    failed["runtime_fault"] = fault;
    e0::atomic_json(result / "status.json", failed);
    check(reconcile().at("state") == "interrupted",
          "verified runtime failure recoverable on fresh worker");
    auto same = live.snapshot();
    same["worker_id"] = "old-worker";
    e0::atomic_json(result / "status.json", status);
    e0::reconcile_claims(inbox, same);
    check(e0::read_json(result / "status.json") == status,
          "current owner never reconciled");
    owner["job_payload"] = payload + " ";
    e0::atomic_json(inbox / "trial.owner.json", owner);
    check(reconcile().at("state") == "failed",
          "owner payload hash mismatch refused");
    owner["job_payload"] = payload;
    e0::atomic_json(inbox / "trial.owner.json", owner);
    e0::atomic_json(result / "status.json", status);
    auto bad = checkpoint;
    bad["stream_position"] = 999;
    e0::atomic_json(result / "checkpoint.json", bad);
    status["checkpoint"] = descriptor(inbox, result / "checkpoint.json");
    e0::atomic_json(result / "status.json", status);
    check(reconcile().at("state") == "failed",
          "restored stream position independently checked");
    e0::atomic_json(result / "checkpoint.json", checkpoint);
    status["checkpoint"] = descriptor(inbox, result / "checkpoint.json");
    e0::atomic_json(result / "branch-1.json", initial);
    status["branches"] = Json::array(
        {{{"end_step", 1},
          {"cooldown_start", 0},
          {"cooldown_steps", 1},
          {"tokens_seen", 8},
          {"artifact", descriptor(inbox, result / "branch-1.json")}}});
    e0::atomic_json(result / "status.json", status);
    check(reconcile().at("state") == "interrupted",
          "valid completed branch preserved");
    e0::atomic_json(result / "status.json", status);
    raw(result / "branch-1.json", "corrupt");
    check(reconcile().at("state") == "failed",
          "published branch corruption refused");
    fs::remove(inbox / "trial.owner.json");
    e0::atomic_json(result / "status.json", status);
    check(reconcile().at("state") == "failed", "missing claim fails closed");
    raw(inbox / "unstarted.claimed", "");
    e0::reconcile_claims(inbox, live.snapshot());
    check(e0::read_json(inbox / "results" / "unstarted" / "status.json")
                  .at("state") == "failed",
          "claim-before-owner crash fails with durable diagnostic");
    check(!fs::exists(inbox / "trial.ready"),
          "reconciliation never enqueues work");
    live.fail(fault);
    live.ready();
    check(live.snapshot().at("state") == "failed",
          "failed worker cannot become ready");
    check(throws([&] { live.running(owner); }),
          "failed worker refuses new claims");
    check(fs::exists(result / "checkpoint.json"),
          "checkpoint preserved on all rejected recovery paths");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    ++failures;
  }
  fs::remove_all(root);
  return failures ? 1 : 0;
}
