// Host unit tests for the read-only E0 dashboard view model.
#include "../dashboard.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
// Schedule of trial e0-20261001T090514Z-4236fd-000 as run_job publishes it.
e0ui::Json status(const char *phase, uint64_t step, size_t branches) {
  e0ui::Json s = {{"job_id", "j"},
                  {"state", "running"},
                  {"trunk_step", step},
                  {"wall_seconds", 100.0},
                  {"branches", e0ui::Json::array()},
                  {"phase", phase},
                  {"schedule",
                   {{"T", 879},
                    {"warmup", 17},
                    {"tokens_per_step", 8192},
                    {"ends", {879, 1758, 3516}},
                    {"cooldown_starts", {792, 1583, 3165}}}}};
  for (size_t b = 0; b < branches; ++b)
    s["branches"].push_back(e0ui::Json::object());
  return s;
}
} // namespace

int main() {
  check(!e0ui::schedule_from_status({{"state", "running"}}),
        "no schedule without the published field");
  const auto s = *e0ui::schedule_from_status(status("trunk", 0, 0));
  check(s.T == 879 && s.tokens_per_step == 8192 && s.warmup == 17,
        "schedule fields are read, not derived");
  check(s.cooldown(0) == 87 && s.cooldown(1) == 175 && s.cooldown(2) == 351,
        "cooldown lengths");
  check(s.total_steps() == 3165 + 87 + 175 + 351, "total optimizer steps");

  auto trunk = status("trunk", 512, 0);
  check(e0ui::executed_steps(trunk, s) == 512, "trunk progress");
  check(e0ui::phase(trunk, s) == "trunk · next cooldown at step 792",
        "trunk phase names the next cooldown");
  check(e0ui::phase(status("trunk", 8, 0), s) == "warmup", "warmup phase");

  auto cool = status("cooldown", 792, 0);
  cool["cooldown_end"] = 879;
  cool["cooldown_step"] = 856;
  check(e0ui::executed_steps(cool, s) == 792 + 64, "cooldown progress counts");
  check(e0ui::phase(cool, s) == "cooldown · branch 1 of 3, ends at step 879",
        "cooldown phase comes from the published fields");

  auto stray = status("trunk", 512, 0);
  stray["cooldown_end"] = 879;
  stray["cooldown_step"] = 856;
  check(e0ui::executed_steps(stray, s) == 512 &&
            e0ui::phase(stray, s).rfind("trunk", 0) == 0,
        "only the published phase starts a cooldown");

  auto after = status("trunk", 1583, 2);
  check(e0ui::executed_steps(after, s) == 1583 + 87 + 175,
        "finished cooldowns count");
  check(e0ui::phase(
            {{"state", "interrupted"}, {"trunk_step", 461}, {"branches", {}}},
            s) == "interrupted · checkpoint at step 461",
        "interrupted names the checkpoint");

  // Throughput keeps working while only the cooldown step advances.
  auto later = cool;
  later["cooldown_step"] = 879;
  later["wall_seconds"] = 123.0;
  auto r = e0ui::rate(cool, later, s);
  check(r.valid && std::abs(r.tokens_per_second - 23 * 8192 / 23.0) < 1e-6,
        "rate during a cooldown");
  auto resumed = later;
  resumed["cooldown_step"] = 900;
  resumed["wall_seconds"] = 3.0;
  check(!e0ui::rate(later, resumed, s).valid,
        "a resumed segment restarts the wall clock");
  check(std::abs(e0ui::eta_seconds(after, s, 8192.0) -
                 double(s.total_steps() - (1583 + 87 + 175))) < 1e-6,
        "ETA counts every remaining optimizer step");
  check(e0ui::eta_seconds({{"state", "completed"}}, s, 8192.0) < 0,
        "no ETA unless running");

  e0ui::LossHistory h(4);
  for (uint64_t step : {64, 128, 128, 64, 192, 256, 320})
    h.add(step, 2.0 - step / 1000.0);
  h.add(384, std::nan(""));
  check(h.points().size() <= 4, "history stays bounded");
  check(h.points().back().step == 320, "newest point survives decimation");
  for (size_t i = 1; i < h.points().size(); ++i)
    check(h.points()[i].step > h.points()[i - 1].step, "steps strictly grow");
  const auto xy = e0ui::plot(h.points(), e0ui::ema(h.points()), s, 600, 200);
  check(xy.size() == h.points().size(), "one plot point per sample");
  for (auto [x, y] : xy)
    check(x >= 0 && x <= 600 && y >= 0 && y <= 200, "plot stays in the box");

  check(!e0ui::smoothed(15) && e0ui::smoothed(16), "smoothing needs 16 points");
  // warmup at 7, cooldown T at 70 (labels 60 wide) collide; cooldown 4T at 545
  // would overflow a 560-wide chart.
  const auto slots =
      e0ui::place_labels({7, 70, 300, 545}, {60, 80, 90, 90}, 560);
  check(slots[0].row == 0 && slots[1].row == 1, "colliding labels change row");
  check(slots[2].row == 0, "a free row is reused");
  check(slots[3].left + 90 <= 560 && slots[3].left < 545,
        "an overflowing label moves left of its line");
  for (const auto &s : slots)
    check(s.left >= 0, "labels stay inside the chart");

  const auto range = e0ui::loss_range({{1, 2.0}, {2, 1.0}});
  check(std::abs(range.lo - 0.95) < 1e-9 && std::abs(range.hi - 2.05) < 1e-9,
        "loss range pads 5%");
  check(e0ui::loss_y(range, 2.05, 200) == 0 &&
            e0ui::loss_y(range, 0.95, 200) == 200,
        "high loss on top");
  const auto flat = e0ui::loss_range({{1, 0.5}});
  check(flat.hi > flat.lo, "a single point still has a range");
  auto ticks = e0ui::nice_ticks(0.95, 2.05);
  check(ticks.values == std::vector<double>{1.0, 1.5, 2.0} && ticks.digits == 1,
        "round ticks inside the range");
  ticks = e0ui::nice_ticks(0.4134, 0.4161);
  check(!ticks.values.empty() && ticks.digits == 3, "narrow range digits");
  for (double v : ticks.values)
    check(v >= 0.4134 && v <= 0.4161, "ticks stay inside the range");
  check(e0ui::nice_ticks(1, 1).values.empty(), "no ticks for an empty range");

  check(e0ui::chart_step(trunk, s) == 512, "chart follows the trunk");
  check(e0ui::chart_step(cool, s) == 856, "chart follows the cooldown");
  auto views = e0ui::branch_views(cool, s);
  check(views[0].state == e0ui::BranchView::cooling &&
            views[0].percent == 64 * 100 / 87 &&
            views[1].state == e0ui::BranchView::pending,
        "first branch cooling");
  auto two = status("trunk", 1583, 2);
  two["branches"][0]["cooldown_seconds"] = 180.0;
  views = e0ui::branch_views(two, s);
  check(views[0].state == e0ui::BranchView::done && views[0].seconds == 180 &&
            views[1].state == e0ui::BranchView::done && views[1].seconds < 0 &&
            views[2].state == e0ui::BranchView::pending,
        "finished branches with and without a duration");

  const e0ui::Json job = {
      {"config",
       {{"d", 256},
        {"n_layers", 4},
        {"n_heads", 4},
        {"d_ff", 1024},
        {"ctx", 128},
        {"core_fmt", "q4"},
        {"mlp", "swiglu"}}},
      {"spec", {{"lr", 0.003}, {"batch", 64}, {"tokens", 28800000}}}};
  check(e0ui::describe_job(job) == "d 256 · 4 layers · 4 heads · ff 1024 · "
                                   "ctx 128 · q4 / swiglu · lr 0.003 · "
                                   "batch 64 · T 28.8 M tokens",
        "job summary");
  check(e0ui::describe_job({{"spec", {{"batch", 8}}}}) == "batch 8",
        "missing fields are left out");
  check(e0ui::describe_job(e0ui::Json::object()).empty(), "empty job");

  // Segment average: the first status of a segment against the latest one.
  auto first = cool, last = later;
  check(e0ui::rate(first, last, s).valid, "segment average");

  check(e0ui::format_loss(2.82614) == "2.826" &&
            e0ui::format_loss(0.5519) == "0.5519" &&
            e0ui::format_loss(0.00381234) == "0.003812",
        "loss keeps four significant digits");
  check(e0ui::format_compact(950) == "950", "small compact");
  check(e0ui::format_compact(147456) == "147 k", "thousands compact");
  check(e0ui::format_compact(1474560) == "1.47 M", "millions compact");
  check(e0ui::format_compact(25952256) == "26.0 M", "tens of millions");
  check(e0ui::format_count(10224282) == "10 224 282", "thousands separator");
  check(e0ui::format_duration(45) == "45 s", "seconds");
  check(e0ui::format_duration(2460) == "41 min", "minutes");
  check(e0ui::format_duration(7500) == "2 h 05 min", "hours");
  check(e0ui::format_megabytes(114245632) == "114 MB", "megabytes");

  const auto ready = e0ui::Json{
      {"schema", "floppylm.worker.v1"},
      {"worker_id", "w"},
      {"pid", 1},
      {"package", "p"},
      {"commit", "c"},
      {"heartbeat_seq", 3},
      {"state", "ready"},
      {"active_job", nullptr},
      {"fault", nullptr},
      {"progress",
       {{"sequence", 1},
        {"phase", "idle"},
        {"trunk_step", 0},
        {"cooldown_step", 0},
        {"operation", ""},
        {"completed_fence", 0}}}};
  auto live = e0ui::worker_from_json(ready, 2);
  check(live.present && live.state == "ready" && !live.stale && !live.faulted,
        "ready worker");
  check(e0ui::worker_line(live) == "WORKER READY · heartbeat 2 s ago",
        "ready line");
  live = e0ui::worker_from_json(ready, 31);
  check(live.stale && e0ui::worker_line(live) ==
                          "WORKER UNREACHABLE · no heartbeat for 31 s",
        "stale heartbeat");
  auto running = ready;
  running["state"] = "running";
  running["progress"]["completed_fence"] = 12;
  running["progress"]["operation"] = "rmsnorm";
  live = e0ui::worker_from_json(running, 1);
  check(e0ui::worker_line(live) ==
            "WORKER RUNNING · heartbeat 1 s ago · fence 12 · rmsnorm",
        "running fence line");
  auto failed = ready;
  failed["state"] = "failed";
  failed["fault"] = {{"kind", "gpu_wait_timeout"},
                     {"error", "wait timed out"},
                     {"requested_fence", 7},
                     {"completed_fence", 6},
                     {"elapsed_ms", 600000}};
  live = e0ui::worker_from_json(failed, 4);
  check(live.faulted &&
            e0ui::worker_line(live) ==
                "WORKER FAILED · gpu_wait_timeout · requested 7 completed 6 · "
                "10 min",
        "fault line");
  check(!e0ui::worker_from_json(e0ui::Json::object(), 0).present,
        "missing schema is absent");
  check(e0ui::worker_line({}) == "WORKER · not published yet",
        "unpublished worker");

  if (failures)
    return EXIT_FAILURE;
  std::puts("dashboard tests passed");
  return EXIT_SUCCESS;
}
