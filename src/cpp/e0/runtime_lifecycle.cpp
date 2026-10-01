#include "runtime_lifecycle.h"
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#ifdef XGPU_UWP
#include <fileapifromapp.h>
#endif
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace e0 {
WorkerLock::WorkerLock(const std::filesystem::path &path) {
#ifdef _WIN32
#ifdef XGPU_UWP
  handle_ =
      CreateFileFromAppW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
  handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#endif
  if (handle_ == INVALID_HANDLE_VALUE) {
    handle_ = nullptr;
    throw std::runtime_error("worker_lock_unavailable");
  }
#else
  handle_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (handle_ < 0 || flock(handle_, LOCK_EX | LOCK_NB) != 0) {
    if (handle_ >= 0)
      close(handle_);
    handle_ = -1;
    throw std::runtime_error("worker_lock_unavailable");
  }
#endif
}
WorkerLock::~WorkerLock() {
#ifdef _WIN32
  if (handle_)
    CloseHandle(handle_);
#else
  if (handle_ >= 0)
    close(handle_);
#endif
}
RuntimeLifecycle::RuntimeLifecycle(std::filesystem::path local,
                                   std::string worker_id, uint64_t pid,
                                   std::string package, std::string commit)
    : local_(std::move(local)), state_{{"schema", "floppylm.worker.v1"},
                                       {"worker_id", worker_id},
                                       {"pid", pid},
                                       {"package", package},
                                       {"commit", commit},
                                       {"heartbeat_seq", 0},
                                       {"state", "starting"},
                                       {"active_job", nullptr},
                                       {"fault", nullptr},
                                       {"progress",
                                        {{"sequence", 0},
                                         {"phase", "starting"},
                                         {"trunk_step", 0},
                                         {"cooldown_step", 0},
                                         {"operation", ""},
                                         {"completed_fence", 0}}}} {}
RuntimeLifecycle::~RuntimeLifecycle() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    stop_ = true;
  }
  changed_.notify_all();
  if (heartbeat_.joinable())
    heartbeat_.join();
}
void RuntimeLifecycle::start() {
  publish();
  heartbeat_ = std::thread([this] {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!changed_.wait_for(lock, std::chrono::seconds(5),
                              [this] { return stop_; })) {
      lock.unlock();
      try {
        publish();
      } catch (const std::exception &error) {
        fail({{"kind", "heartbeat_publish_failed"},
              {"error", error.what()},
              {"requested_fence", 0},
              {"completed_fence", 0},
              {"elapsed_ms", 0}});
      }
      lock.lock();
    }
  });
}
void RuntimeLifecycle::publish() {
  // Serialize writers: atomic_json uses one temporary name per destination.
  std::lock_guard<std::mutex> writer(publish_mutex_);
  Json copy;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    state_["heartbeat_seq"] = state_["heartbeat_seq"].get<uint64_t>() + 1;
    copy = state_;
  }
  atomic_json(local_ / "worker.json", copy);
}
Json RuntimeLifecycle::snapshot() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return state_;
}
void RuntimeLifecycle::ready() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (state_["state"] == "failed")
    return;
  state_["state"] = "ready";
  state_["active_job"] = nullptr;
  state_["progress"]["phase"] = "idle";
}
void RuntimeLifecycle::running(const Json &claim) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (state_["state"] == "failed")
    throw std::runtime_error("worker_failed");
  state_["state"] = "running";
  state_["active_job"] = {{"job_id", claim.at("job_id")},
                          {"job_sha256", claim.at("job_sha256")}};
  state_["progress"]["phase"] = "initializing";
  state_["progress"]["trunk_step"] = 0;
  state_["progress"]["cooldown_step"] = 0;
}
void RuntimeLifecycle::progress(const std::string &phase, uint64_t trunk,
                                uint64_t cooldown) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto &p = state_["progress"];
  p["sequence"] = p["sequence"].get<uint64_t>() + 1;
  p["phase"] = phase;
  p["trunk_step"] = trunk;
  p["cooldown_step"] = cooldown;
}
void RuntimeLifecycle::gpu_progress(uint64_t fence,
                                    const std::string &operation) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto &p = state_["progress"];
  p["sequence"] = p["sequence"].get<uint64_t>() + 1;
  p["completed_fence"] = fence;
  p["operation"] = operation;
}
void RuntimeLifecycle::fail(const Json &fault) {
  std::lock_guard<std::mutex> guard(mutex_);
  // The first fault is causal evidence; a later failed heartbeat write must
  // not erase the original GPU/runtime failure.
  if (state_["fault"].is_null())
    state_["fault"] = fault;
  state_["state"] = "failed";
}
namespace {
void valid_id(const std::string &id) {
  if (id.empty() ||
      id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::runtime_error("claim_invalid_job_id");
}
std::string bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("claim_missing_payload");
  std::ostringstream out;
  out << stream.rdbuf();
  if (stream.bad())
    throw std::runtime_error("claim_payload_read_failed");
  return out.str();
}
bool recoverable_fault(const Json &status) {
  if (!status.contains("runtime_fault") ||
      !status.at("runtime_fault").is_object())
    return false;
  const auto kind = status.at("runtime_fault").value("kind", "");
  return kind == "gpu_wait_timeout" || kind == "gpu_wait_failed" ||
         kind == "gpu_device_removed" || kind == "gpu_fence_error";
}
void verify_orphan(const std::filesystem::path &inbox, const Json &owner,
                   const Json &worker, Json &status) {
  const auto id = owner.at("job_id").get<std::string>();
  valid_id(id);
  const auto payload = owner.at("job_payload").get<std::string>();
  if (owner.at("schema") != "floppylm.claim.v1" ||
      owner.at("package") != worker.at("package") ||
      owner.at("commit") != worker.at("commit") ||
      owner.at("worker_id").get<std::string>().empty() ||
      sha256_bytes(payload) != owner.at("job_sha256").get<std::string>())
    throw std::runtime_error("orphan_owner_binding_mismatch");
  Json job = Json::parse(payload);
  if (job.at("job_id") != id || job.at("schema") != "floppylm.e0.job.v1" ||
      status.at("job_id") != id ||
      status.at("job_sha256") != owner.at("job_sha256"))
    throw std::runtime_error("orphan_result_binding_mismatch");
  auto initial = read_json(verified_asset(inbox, job.at("initialization")));
  if (initial.at("config") != job.at("config"))
    throw std::runtime_error("orphan_initialization_config_mismatch");
  job["tensors"] = initial.at("tensors");
  job["initialization_sha256"] = job.at("initialization").at("sha256");
  verified_asset(inbox, job.at("data"));
  verified_asset(inbox, job.at("indices"));
  const auto expected = "results/" + id + "/checkpoint.json";
  if (status.at("checkpoint").at("path") != expected)
    throw std::runtime_error("orphan_checkpoint_path_mismatch");
  const auto checkpoint =
      read_json(verified_asset(inbox, status.at("checkpoint")));
  Model model(job);
  uint64_t step = 0;
  model.restore(checkpoint, job, step);
  const uint64_t batch = job.at("spec").at("batch").get<uint64_t>();
  const uint64_t tokens = job.at("spec").at("tokens").get<uint64_t>();
  if (!batch || !tokens)
    throw std::runtime_error("orphan_invalid_schedule");
  const uint64_t T = std::max<uint64_t>(1, tokens / (batch * model.config.ctx));
  const uint64_t max_step =
      4 * T -
      std::max<uint64_t>(1, uint64_t(constants::kCooldownFrac * double(4 * T)));
  if (step > max_step || status.at("branches").size() > 3)
    throw std::runtime_error("orphan_schedule_position_mismatch");
  std::set<uint64_t> ends;
  for (const auto &branch : status.at("branches")) {
    const auto end = branch.at("end_step").get<uint64_t>();
    const uint64_t cooldown =
        std::max<uint64_t>(1, uint64_t(constants::kCooldownFrac * double(end)));
    if ((end != T && end != 2 * T && end != 4 * T) || end - cooldown > step ||
        branch.at("cooldown_start") != end - cooldown ||
        branch.at("cooldown_steps") != cooldown ||
        branch.at("tokens_seen") != end * batch * model.config.ctx ||
        !ends.insert(end).second ||
        branch.at("artifact").at("path") !=
            "results/" + id + "/branch-" + std::to_string(end) + ".json")
      throw std::runtime_error("orphan_branch_binding_mismatch");
    const auto weights =
        read_json(verified_asset(inbox, branch.at("artifact")));
    if (weights.at("schema") != "floppylm.e0.weights.v1" ||
        weights.at("config") != job.at("config"))
      throw std::runtime_error("orphan_branch_config_mismatch");
    Model verified_weights(weights);
  }
  for (const auto end : {T, 2 * T, 4 * T}) {
    const auto start =
        end -
        std::max<uint64_t>(1, uint64_t(constants::kCooldownFrac * double(end)));
    if (step > start && !ends.count(end))
      throw std::runtime_error("orphan_missing_completed_branch");
  }
  status["state"] = "interrupted";
  status["trunk_step"] = step;
  status.erase("error");
}
} // namespace
std::filesystem::path persist_claim(const std::filesystem::path &inbox,
                                    const std::string &id, const Json &worker,
                                    std::string *submitted_hash) {
  valid_id(id);
  const auto payload = bytes(inbox / (id + ".job.json"));
  const auto payload_hash = sha256_bytes(payload);
  if (submitted_hash)
    *submitted_hash = payload_hash;
  const auto job = Json::parse(payload);
  if (job.contains("job_id") && job.at("job_id") != id)
    throw std::runtime_error("claim_job_id_mismatch");
  const auto status_path = inbox / "results" / id / "status.json";
  if (std::filesystem::exists(status_path)) {
    auto previous = read_json(status_path);
    if (previous.value("state", "") != "interrupted" || !job.contains("resume"))
      throw std::runtime_error("claim_requires_verified_interrupted_resume");
    const auto previous_owner = read_json(inbox / (id + ".owner.json"));
    verify_orphan(inbox, previous_owner, worker, previous);
    auto original =
        Json::parse(previous_owner.at("job_payload").get<std::string>());
    auto submitted = job;
    original.erase("resume");
    // The host removes a functional stop limit for the continued segment.
    // A resumed submission may not introduce another stop limit.
    original.erase("stop_after");
    submitted.erase("resume");
    if (original != submitted || job.at("resume") != previous.at("checkpoint"))
      throw std::runtime_error("claim_resume_binding_mismatch");
  } else if (std::filesystem::exists(inbox / "results" / id)) {
    throw std::runtime_error("claim_existing_result_without_status");
  }
  const auto path = inbox / (id + ".owned.job.json");
  // Owner is the durable source of truth, published before any execution.
  const Json owner{{"schema", "floppylm.claim.v1"},
                   {"worker_id", worker.at("worker_id")},
                   {"package", worker.at("package")},
                   {"commit", worker.at("commit")},
                   {"job_id", id},
                   {"job_sha256", payload_hash},
                   {"job_payload", payload}};
  const auto archive =
      inbox /
      (id + ".owner." + owner.at("job_sha256").get<std::string>() + ".json");
  if (std::filesystem::exists(archive)) {
    auto archived = read_json(archive);
    auto binding = owner;
    archived.erase("worker_id");
    binding.erase("worker_id");
    if (archived != binding)
      throw std::runtime_error("claim_archive_conflict");
  } else
    atomic_json(archive, owner);
  atomic_json(inbox / (id + ".owner.json"), owner);
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(payload.data(), std::streamsize(payload.size()));
    stream.flush();
    if (!stream)
      throw std::runtime_error("claim_snapshot_write_failed");
  }
  if (sha256_file(path) != owner.at("job_sha256").get<std::string>())
    throw std::runtime_error("claim_snapshot_hash_mismatch");
  return path;
}
void reconcile_claims(const std::filesystem::path &inbox, const Json &worker) {
  for (const auto &entry : std::filesystem::directory_iterator(inbox)) {
    std::string id;
    try {
      if (entry.path().extension() != ".claimed")
        continue;
      id = entry.path().stem().string();
      valid_id(id);
      const auto pending_ready = inbox / (id + ".ready");
      if (std::filesystem::exists(pending_ready)) {
        const auto quarantine =
            inbox / (id + ".ready.quarantined." +
                     worker.at("worker_id").get<std::string>());
        std::filesystem::rename(pending_ready, quarantine);
      }
      const auto result = inbox / "results" / id;
      Json status;
      try {
        status = read_json(result / "status.json");
      } catch (...) {
      }
      // Fixtures, kernels and optimizer publish .actual.json and never write
      // results/<id>/status.json. Their leftover .claimed files are success.
      if (!status.is_object() &&
          std::filesystem::exists(inbox / (id + ".actual.json")))
        continue;
      // A completed record is immutable even when old lifecycle metadata is
      // absent.
      if (status.is_object() && status.value("state", "") == "completed")
        continue;
      if (status.is_object() && status.value("state", "") == "failed" &&
          !recoverable_fault(status))
        continue;
      auto owner = read_json(inbox / (id + ".owner.json"));
      // A crash after publishing a replacement owner but before its first
      // result must retain the previous submission's immutable binding.
      if (status.is_object() && status.contains("job_sha256") &&
          status.at("job_sha256") != owner.at("job_sha256")) {
        const auto hash = status.at("job_sha256").get<std::string>();
        if (hash.size() != 64 ||
            hash.find_first_not_of("0123456789abcdef") != std::string::npos)
          throw std::runtime_error("orphan_invalid_result_hash");
        owner = read_json(inbox / (id + ".owner." + hash + ".json"));
      }
      if (owner.at("worker_id") == worker.at("worker_id"))
        continue;
      if (!status.is_object())
        throw std::runtime_error("orphan_missing_result");
      verify_orphan(inbox, owner, worker, status);
      if (read_json(inbox / (id + ".owner.json")) != owner)
        atomic_json(inbox / (id + ".owner.json"), owner);
      std::filesystem::create_directories(result);
      atomic_json(result / "status.json", status);
    } catch (const std::exception &error) {
      if (id.empty())
        continue;
      try {
        valid_id(id);
      } catch (...) {
        continue;
      }
      if (std::filesystem::exists(inbox / (id + ".actual.json")))
        continue;
      Json status = Json::object();
      const auto result = inbox / "results" / id;
      try {
        status = read_json(result / "status.json");
      } catch (...) {
      }
      if (!status.is_object())
        status = Json::object();
      status["state"] = "failed";
      status["job_id"] = id;
      status["error"] =
          std::string("orphan_verification_failed: ") + error.what();
      // Integrity failures cannot inherit a retryable GPU classification.
      status.erase("runtime_fault");
      std::filesystem::create_directories(result);
      atomic_json(result / "status.json", status);
    }
  }
}
} // namespace e0
