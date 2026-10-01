#pragma once
#include "model.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace e0 {
// Held until process exit on the worker path, including fault quarantine.
class WorkerLock {
public:
  explicit WorkerLock(const std::filesystem::path &);
  ~WorkerLock();
  WorkerLock(const WorkerLock &) = delete;
  WorkerLock &operator=(const WorkerLock &) = delete;

private:
#ifdef _WIN32
  void *handle_ = nullptr;
#else
  int handle_ = -1;
#endif
};
class RuntimeLifecycle {
public:
  RuntimeLifecycle(std::filesystem::path local, std::string worker_id,
                   uint64_t pid, std::string package, std::string commit);
  ~RuntimeLifecycle();
  void start();
  void publish();
  Json snapshot() const;
  void ready();
  void running(const Json &claim);
  void progress(const std::string &phase, uint64_t trunk, uint64_t cooldown);
  void gpu_progress(uint64_t fence, const std::string &operation);
  void fail(const Json &fault);

private:
  std::filesystem::path local_;
  mutable std::mutex mutex_;
  std::mutex publish_mutex_;
  std::condition_variable changed_;
  Json state_;
  bool stop_ = false;
  std::thread heartbeat_;
};
// All calls below run under WorkerLock. Returned path contains exact captured
// bytes; run_job must consume that path, never the replaceable upload path.
std::filesystem::path persist_claim(const std::filesystem::path &inbox,
                                    const std::string &id, const Json &worker,
                                    std::string *submitted_hash = nullptr);
void reconcile_claims(const std::filesystem::path &inbox, const Json &worker);
} // namespace e0
