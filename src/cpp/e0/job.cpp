#include "constants.h"
#include "model.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#ifdef XGPU_UWP
#include <fileapifromapp.h>
#endif
#else
#include <openssl/evp.h>
#endif
namespace e0 {
static std::string sha256_stream(std::istream &input) {
  std::vector<char> bytes(1 << 20);
  unsigned char digest[32]{};
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0)
    throw std::runtime_error("SHA256 provider failed");
  if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    throw std::runtime_error("SHA256 hash creation failed");
  }
  try {
    while (input.read(bytes.data(), bytes.size()) || input.gcount())
      if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes.data()),
                         ULONG(input.gcount()), 0) < 0)
        throw std::runtime_error("SHA256 update failed");
    if (BCryptFinishHash(hash, digest, 32, 0) < 0)
      throw std::runtime_error("SHA256 finalize failed");
  } catch (...) {
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    throw;
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
#else
  auto *context = EVP_MD_CTX_new();
  if (!context)
    throw std::runtime_error("SHA256 context allocation failed");
  try {
    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
      throw std::runtime_error("SHA256 init failed");
    while (input.read(bytes.data(), bytes.size()) || input.gcount())
      if (EVP_DigestUpdate(context, bytes.data(), size_t(input.gcount())) != 1)
        throw std::runtime_error("SHA256 update failed");
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context, digest, &length) != 1 || length != 32)
      throw std::runtime_error("SHA256 finalize failed");
  } catch (...) {
    EVP_MD_CTX_free(context);
    throw;
  }
  EVP_MD_CTX_free(context);
#endif
  if (input.bad())
    throw std::runtime_error("SHA256 read failed");
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (auto v : digest)
    out << std::setw(2) << unsigned(v);
  return out.str();
}
std::string sha256_bytes(const std::string &bytes) {
  std::istringstream input(bytes);
  return sha256_stream(input);
}
std::string sha256_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot hash: " + path_text(path));
  return sha256_stream(input);
}
Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("missing JSON: " + path_text(path));
  return Json::parse(input);
}
namespace {
struct DeleteFile {
  std::filesystem::path path;
  ~DeleteFile() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

// A stable "<dest>.tmp" is truncated before the body exists. A reader of that
// name can hold the writer. Serialize first, then write a unique partial.
constexpr std::size_t kPhaseTraceBytes = 1u << 20;

void write_text_file(const std::filesystem::path &path, const std::string &text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
    throw std::runtime_error("JSON phase write failed");
  file << text;
  file.flush();
  if (!file)
    throw std::runtime_error("JSON phase write failed");
}

void write_new_body(const std::filesystem::path &path, const std::string &body) {
#ifdef _WIN32
#ifdef XGPU_UWP
  // AppContainer does not export CreateFileW. FromApp is the LocalState writer.
  HANDLE handle = CreateFileFromAppW(
#else
  HANDLE handle = CreateFileW(
#endif
      path.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    throw std::runtime_error("JSON write failed");
  struct Close {
    HANDLE handle;
    ~Close() { CloseHandle(handle); }
  } close{handle};
  for (size_t offset = 0; offset < body.size();) {
    const auto chunk = static_cast<DWORD>(std::min<size_t>(body.size() - offset, 1u << 20));
    DWORD wrote = 0;
    if (!WriteFile(handle, body.data() + offset, chunk, &wrote, nullptr) || wrote != chunk)
      throw std::runtime_error("JSON write failed");
    offset += wrote;
  }
  if (!FlushFileBuffers(handle))
    throw std::runtime_error("JSON flush failed");
#else
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
    throw std::runtime_error("JSON write failed");
  file.write(body.data(), static_cast<std::streamsize>(body.size()));
  file.flush();
  if (!file)
    throw std::runtime_error("JSON flush failed");
#endif
}

std::filesystem::path partial_path(const std::filesystem::path &path) {
  static std::atomic<uint64_t> sequence{0};
  const auto tick = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
  std::ostringstream name;
  name << path_text(path) << '.' << std::hex << tick << '-' << id << ".partial";
  return std::filesystem::path(name.str());
}
}  // namespace

void atomic_json(const std::filesystem::path &path, const Json &value) {
  // ExitProcess skips destructors, so a hung publish leaves this one line.
  DeleteFile phase{std::filesystem::path(path_text(path) + ".phase")};
  write_text_file(phase.path, "dumping\n");
  const std::string body = value.dump();
  const bool trace = body.size() > kPhaseTraceBytes;
  if (trace)
    write_text_file(phase.path, "writing " + std::to_string(body.size()) + '\n');
  else {
    std::error_code error;
    std::filesystem::remove(phase.path, error);
  }
  const auto temporary = partial_path(path);
  DeleteFile partial{temporary};
  write_new_body(temporary, body);
  if (trace)
    write_text_file(phase.path, "replacing\n");
#ifdef _WIN32
  DWORD error = ERROR_SUCCESS;
  for (unsigned attempt = 0; attempt < 40; ++attempt) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    bool published = false;
    if (attrs == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) {
      published = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
    } else {
#ifdef XGPU_UWP
      published = ReplaceFileFromAppW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr,
                                      nullptr);
#else
      published = ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr);
#endif
    }
    if (published)
      return;
    error = GetLastError();
    // Device Portal can briefly hold a reader without FILE_SHARE_DELETE.
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  throw std::runtime_error("JSON atomic replacement failed: " + path_text(path) +
                           " win32=" + std::to_string(error));
#else
  std::filesystem::rename(temporary, path);
#endif
}
void record_failure(const std::filesystem::path &job_file, const std::string &error) {
  const auto root = job_file.parent_path();
  const auto id = job_file.stem().stem();
  const auto result = root / "results" / id;
  std::filesystem::create_directories(result);
  const auto destination = std::filesystem::exists(result / "status.json")
                               ? root / (path_text(id) + ".rejected.json")
                               : result / "status.json";
  atomic_json(destination, {{"state", "failed"}, {"error", error}, {"job_id", path_text(id)}});
}
namespace {
std::filesystem::path asset(const std::filesystem::path &root,
                            const Json &descriptor) {
  std::filesystem::path relative = descriptor.at("path").get<std::string>();
  if (relative.is_absolute() || relative.empty())
    throw std::runtime_error("asset path must be relative");
  for (const auto &part : relative)
    if (part == "..")
      throw std::runtime_error("asset escapes job directory");
#ifdef XGPU_UWP
  // Canonicalization probes ancestors outside LocalState, which AppContainer
  // denies. Walk only the owned subtree and reject all reparse points,
  // including symlinks.
  auto path = (root / relative).lexically_normal();
  auto component = root;
  for (const auto &part : relative) {
    component /= part;
    const DWORD attrs = GetFileAttributesW(component.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("asset attributes denied: " +
                                 std::to_string(error));
    } else if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
      throw std::runtime_error("asset reparse point rejected");
  }
  auto prefix = root.lexically_normal();
#else
  auto path = std::filesystem::weakly_canonical(root / relative);
  auto prefix = std::filesystem::weakly_canonical(root);
#endif
  auto rel = path.lexically_relative(prefix);
  for (const auto &part : rel)
    if (part == "..")
      throw std::runtime_error("asset symlink escapes job directory");
  if (!std::filesystem::exists(path) && descriptor.contains("chunks")) {
    const auto temporary = path_text(path) + ".assembling";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output)
        throw std::runtime_error("asset assembly creation failed");
      std::vector<char> buffer(1 << 20);
      uint64_t total = 0;
      for (const auto &chunk : descriptor.at("chunks")) {
        if (chunk.contains("chunks"))
          throw std::runtime_error("nested asset chunks rejected");
        auto source = asset(root, chunk);
        std::ifstream input(source, std::ios::binary);
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
          output.write(buffer.data(), input.gcount());
          total += uint64_t(input.gcount());
        }
        if (input.bad() || !output)
          throw std::runtime_error("asset assembly I/O failed");
      }
      output.flush();
      if (!output || total != descriptor.at("bytes").get<uint64_t>())
        throw std::runtime_error("asset assembly size mismatch");
    }
    if (sha256_file(temporary) != descriptor.at("sha256").get<std::string>())
      throw std::runtime_error("assembled asset hash mismatch");
    std::filesystem::rename(temporary, path);
  }
  if (std::filesystem::file_size(path) !=
          descriptor.at("bytes").get<uint64_t>() ||
      sha256_file(path) != descriptor.at("sha256").get<std::string>())
    throw std::runtime_error("asset integrity failed: " + path_text(relative));
  return path;
}
void read_batch(std::ifstream &corpus, std::ifstream &indices, uint64_t step,
                uint32_t batch, uint32_t ctx, uint64_t corpus_size, Values &x,
                Values &y) {
  x.resize(uint64_t(batch) * ctx);
  y.resize(x.size());
  indices.clear();
  indices.seekg(std::streamoff(step * batch * 8));
  std::vector<uint8_t> window(ctx + 1);
  for (uint32_t b = 0; b < batch; ++b) {
    uint8_t encoded[8];
    indices.read(reinterpret_cast<char *>(encoded), 8);
    if (!indices)
      throw std::runtime_error("index plan exhausted");
    uint64_t offset = 0;
    for (unsigned i = 0; i < 8; ++i)
      offset |= uint64_t(encoded[i]) << (8 * i);
    if (offset > corpus_size || corpus_size - offset < ctx + 1)
      throw std::runtime_error("index out of corpus bounds");
    corpus.clear();
    corpus.seekg(std::streamoff(offset));
    corpus.read(reinterpret_cast<char *>(window.data()), window.size());
    if (!corpus)
      throw std::runtime_error("corpus read failed");
    for (uint32_t t = 0; t < ctx; ++t) {
      x[b * ctx + t] = float(window[t]);
      y[b * ctx + t] = float(window[t + 1]);
    }
  }
}
float learning_rate(uint64_t step, uint64_t warmup, float peak,
                    uint64_t cd_start = UINT64_MAX, uint64_t cd_len = 0) {
  if (step < warmup)
    return peak * float(step + 1) / float(warmup);
  if (step < cd_start)
    return peak;
  return peak * std::max(0.0f, 1 - float(step - cd_start + 1) / float(cd_len));
}

uint64_t json_u64(const Json &j, const char *key, uint64_t fallback = 0) {
  if (!j.contains(key) || !j.at(key).is_number_integer())
    return fallback;
  const auto &v = j.at(key);
  if (v.is_number_unsigned())
    return v.get<uint64_t>();
  return v.get<int64_t>() >= 0 ? uint64_t(v.get<int64_t>()) : fallback;
}

// Same rule as e0ui::LossHistory: one point per distinct trunk step, halved
// when full. Display-only; never invent samples for unrecorded steps.
constexpr size_t kLossSeriesCapacity = 512;
struct LossSeries {
  std::vector<std::pair<uint64_t, double>> points;
  void add(uint64_t step, double loss) {
    if (!std::isfinite(loss))
      return;
    if (!points.empty() && step <= points.back().first) {
      if (step == points.back().first)
        points.back().second = loss;
      return;
    }
    points.push_back({step, loss});
    if (points.size() > kLossSeriesCapacity) {
      std::vector<std::pair<uint64_t, double>> kept;
      kept.reserve(points.size() / 2 + 1);
      for (size_t i = 0; i < points.size(); i += 2)
        kept.push_back(points[i]);
      if (kept.back().first != points.back().first)
        kept.push_back(points.back());
      points.swap(kept);
    }
  }
  void load(const Json &previous) {
    if (previous.contains("loss_series") && previous.at("loss_series").is_array()) {
      for (const auto &item : previous.at("loss_series")) {
        if (!item.is_object() || !item.contains("step") || !item.contains("loss"))
          continue;
        if (!item.at("loss").is_number() || !item.at("step").is_number_integer())
          continue;
        add(json_u64(item, "step"), item.at("loss").get<double>());
      }
    }
    if (points.empty() && previous.contains("last_loss") &&
        previous.at("last_loss").is_number())
      add(json_u64(previous, "trunk_step"), previous.at("last_loss").get<double>());
  }
  Json json() const {
    Json out = Json::array();
    for (const auto &[step, loss] : points)
      out.push_back({{"step", step}, {"loss", loss}});
    return out;
  }
};
} // namespace
std::filesystem::path verified_asset(const std::filesystem::path &root, const Json &descriptor) {
  return asset(root, descriptor);
}
Json run_job(const std::filesystem::path &job_file, Kernel &kernel,
             uint64_t stop_after) {
  kernel.dispatches = kernel.transfer_bytes = kernel.peak_memory_bytes = 0;
  kernel.gpu_seconds = 0;
  kernel.observe();
  const auto root = std::filesystem::absolute(job_file).parent_path();
  kernel.require_healthy();
  Json job = read_json(job_file);
  uint64_t probe_step = 0;
  if (job.contains("runtime_fault_probe")) {
    if (job.value("purpose", "") != "functional")
      throw std::runtime_error("runtime fault probes are forbidden for scientific jobs");
    const auto &probe = job.at("runtime_fault_probe");
    if (!probe.is_object() || probe.size() != 2 ||
        probe.value("kind", "") != "published_fence_stall" ||
        !probe.contains("after_checkpoint_step") ||
        !probe.at("after_checkpoint_step").is_number() ||
        probe.at("after_checkpoint_step") <= 0 ||
        job.contains("resume") || job.contains("stop_after") || stop_after)
      throw std::runtime_error("invalid functional runtime fault probe");
    const auto &requested_step = probe.at("after_checkpoint_step");
    if (requested_step.is_number_float()) {
      const auto value = requested_step.get<double>();
      if (!std::isfinite(value) || std::floor(value) != value ||
          value >= std::ldexp(1.0, 64))
        throw std::runtime_error("invalid functional runtime fault probe step");
    }
    probe_step = requested_step.get<uint64_t>();
    if (!kernel.published_fence_stall)
      throw std::runtime_error("published-fence probe requires an independent watchdog worker");
  }
  const std::string id = job.at("job_id");
  if (id.empty() ||
      id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::runtime_error("invalid job id");
  const auto result = root / "results" / id;
  std::filesystem::create_directories(result.parent_path());
  if (job.at("schema") != "floppylm.e0.job.v1")
    throw std::runtime_error("unsupported E0 job schema");
  const auto initial_path = asset(root, job.at("initialization"));
  Json initial = read_json(initial_path);
  if (initial.at("config") != job.at("config"))
    throw std::runtime_error("initialization config mismatch");
  job["tensors"] = initial.at("tensors");
  job["initialization_sha256"] = job.at("initialization").at("sha256");
  const auto corpus_path = asset(root, job.at("data"));
  const auto indices_path = asset(root, job.at("indices"));
  Model model(job);
  const auto &spec = job.at("spec");
  const auto batch = spec.at("batch").get<uint32_t>();
  const auto tokens = spec.at("tokens").get<uint64_t>();
  if (!batch || !tokens || spec.at("branches") != constants::kBranches ||
      spec.at("warmup_frac") != constants::kWarmupFrac ||
      spec.at("cooldown_frac") != constants::kCooldownFrac)
    throw std::runtime_error(
        "E0 requires the declared three-branch WSD protocol");
  float lr = spec.at("lr"), wd = spec.at("wd");
  if (!std::isfinite(lr) || lr <= 0 || !std::isfinite(wd) || wd < 0)
    throw std::runtime_error("invalid optimizer recipe");
  uint64_t T =
      std::max<uint64_t>(1, tokens / (uint64_t(batch) * model.config.ctx));
  uint64_t warmup = std::max<uint64_t>(1, uint64_t(constants::kWarmupFrac * double(T))), step = 0;
  if (job.at("indices").at("bytes").get<uint64_t>() != 4 * T * batch * 8)
    throw std::runtime_error("index plan length mismatch");
  // Branch b cools down over the last 10% of its T * 2^b steps.
  uint64_t ends[3], cooldowns[3], starts[3];
  for (unsigned b = 0; b < 3; ++b) {
    ends[b] = T * (uint64_t(1) << b);
    cooldowns[b] = std::max<uint64_t>(1, uint64_t(constants::kCooldownFrac * double(ends[b])));
    starts[b] = ends[b] - cooldowns[b];
  }
  if (probe_step > starts[2])
    throw std::runtime_error("runtime fault probe checkpoint is unreachable");
  const bool resume = job.contains("resume");
  if (!resume && !std::filesystem::create_directory(result))
    throw std::runtime_error(
        "job result already exists; explicit resume required");
  Json report = {{"schema", "floppylm.e0.result.v1"},
                 {"job_id", job.at("job_id")},
                 {"job_sha256", sha256_file(job_file)},
                 {"hardware_gpu", kernel.hardware()},
                 {"adapter", kernel.adapter()},
                 {"branches", Json::array()},
                 {"state", "running"},
                 {"schedule",
                  {{"T", T},
                   {"warmup", warmup},
                   {"tokens_per_step", uint64_t(batch) * model.config.ctx},
                   {"ends", {ends[0], ends[1], ends[2]}},
                   {"cooldown_starts", {starts[0], starts[1], starts[2]}}}},
                 {"phase", "trunk"}};
  auto begin = std::chrono::steady_clock::now();
  std::ifstream corpus(corpus_path, std::ios::binary),
      indices(indices_path, std::ios::binary);
  bool publication_started = false;
  LossSeries series;
  auto status = [&](const std::string &state) {
    report["state"] = state;
    report["trunk_step"] = step;
    report["dispatches"] = kernel.dispatches;
    report["gpu_seconds"] = kernel.gpu_seconds;
    report["transfer_bytes"] = kernel.transfer_bytes;
    report["peak_memory_bytes"] = kernel.peak_memory_bytes;
    report["wall_seconds"] =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
            .count();
    if (std::filesystem::exists(result / "checkpoint.json"))
      report["checkpoint"] = {
          {"path", "results/" + id + "/checkpoint.json"},
          {"bytes", std::filesystem::file_size(result / "checkpoint.json")},
          {"sha256", sha256_file(result / "checkpoint.json")}};
    if (!series.points.empty())
      report["loss_series"] = series.json();
    else
      report.erase("loss_series");
    atomic_json(result / "status.json", report);
    publication_started = true;
  };
  try {
    if (resume) {
      auto ckpt = read_json(asset(root, job.at("resume")));
      model.restore(ckpt, job, step);
      const auto previous = read_json(result / "status.json");
      if (previous.at("job_id") != job.at("job_id"))
        throw std::runtime_error("resume status job mismatch");
      report["branches"] = previous.at("branches");
      if (previous.contains("last_loss") && previous.at("last_loss").is_number())
        report["last_loss"] = previous.at("last_loss");
      series.load(previous);
      for (const auto &b : report["branches"])
        asset(root, b.at("artifact"));
    } else
      atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
    status("running");
    Values x, y;
    uint64_t executed = 0;
    for (unsigned branch = 0; branch < 3; ++branch) {
      const uint64_t end = ends[branch], cd = cooldowns[branch],
                     start = starts[branch];
      bool published = false;
      for (const auto &prior : report["branches"])
        if (prior.at("end_step") == end) published = true;
      // Resumed published branches were verified before entering this loop.
      if (published || step > start)
        continue;
      while (step < start) {
        if (std::filesystem::exists(root / (id + ".cancel"))) {
          atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
          status("interrupted");
          return report;
        }
        read_batch(corpus, indices, step, batch, model.config.ctx,
                   job.at("data").at("bytes"), x, y);
        auto metric = model.step(kernel, x, y, batch,
                                 learning_rate(step, warmup, lr), wd, step + 1);
        ++step;
        ++executed;
        if (kernel.progress_callback) kernel.progress_callback("trunk", step, 0);
        report["last_loss"] = metric.at("loss");
        series.add(step, metric.at("loss").get<double>());
        if (probe_step && step == probe_step) {
          atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
          status("running");
          kernel.published_fence_stall();
          throw std::runtime_error("published-fence stall hook unexpectedly returned");
        }
        if (step % 64 == 0) {
          atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
          status("running");
        }
        if (stop_after && executed >= stop_after) {
          atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
          status("interrupted");
          return report;
        }
      }
      atomic_json(result / "checkpoint.json", model.checkpoint(step, job));
      const auto cooldown_begin = std::chrono::steady_clock::now();
      Model copy = model;
      report["phase"] = "cooldown";
      report["cooldown_end"] = end;
      report["cooldown_step"] = start;
      status("running");
      for (uint64_t s = start; s < end; ++s) {
        if (std::filesystem::exists(root / (id + ".cancel"))) {
          status("interrupted");
          return report;
        }
        if (s > start && (s - start) % 64 == 0) {
          report["cooldown_step"] = s;
          status("running");
        }
        read_batch(corpus, indices, s, batch, model.config.ctx,
                   job.at("data").at("bytes"), x, y);
        copy.step(kernel, x, y, batch, learning_rate(s, warmup, lr, start, cd),
                  wd, s + 1);
        if (kernel.progress_callback) kernel.progress_callback("cooldown", step, s + 1);
      }
      const std::string name = "branch-" + std::to_string(end) + ".json";
      const auto candidate = result / (name + ".candidate");
      atomic_json(candidate, {{"schema", "floppylm.e0.weights.v1"},
                               {"config", job.at("config")},
                               {"tensors", copy.tensors()}});
      Json b = {{"end_step", end},
                {"tokens_seen", end * batch * model.config.ctx},
                {"cooldown_start", start},
                {"cooldown_steps", cd},
                {"cooldown_seconds",
                 std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - cooldown_begin)
                     .count()},
                {"artifact",
                 {{"path", "results/" + id + "/" + name},
                  {"bytes", std::filesystem::file_size(candidate)},
                  {"sha256", sha256_file(candidate)}}}};
      auto &bs = report["branches"];
      bool found = false;
      for (auto &old : bs)
        if (old.at("end_step") == end) {
          if (old.at("artifact") != b.at("artifact"))
            throw std::runtime_error(
                "resumed branch differs from previous artifact");
          found = true;
        }
      if (!found) {
        if (std::filesystem::exists(result / name))
          throw std::runtime_error("unreported branch artifact already exists");
        std::filesystem::rename(candidate, result / name);
        bs.push_back(b);
      } else {
        // Preserve both the trusted original and any divergent candidate.
        std::filesystem::remove(candidate);
      }
      report["phase"] = "trunk";
      report.erase("cooldown_end");
      report.erase("cooldown_step");
      status("running");
    }
    if (report["branches"].size() != 3)
      throw std::runtime_error("incomplete branch collection");
    status("completed");
    return report;
  } catch (const std::exception &error) {
    if (publication_started) {
      report["error"] = error.what();
      if (kernel.poisoned()) {
        const auto &f = kernel.runtime_fault;
        report["runtime_fault"] = {{"kind", f.kind}, {"error", f.error},
          {"requested_fence", f.requested_fence}, {"completed_fence", f.completed_fence},
          {"elapsed_ms", f.elapsed_ms}};
        // The last atomically published checkpoint is the only recoverable state.
        // Never serialize the potentially partial optimizer state after a GPU fault.
      }
      status("failed");
    } else {
      record_failure(job_file, error.what());
    }
    throw;
  }
}
} // namespace e0
