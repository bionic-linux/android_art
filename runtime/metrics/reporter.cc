/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "reporter.h"

#include "runtime.h"
#include "runtime_options.h"
#include "statsd.h"
#include "thread-current-inl.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

std::unique_ptr<MetricsReporter> MetricsReporter::Create(ReportingConfig config, Runtime* runtime) {
  // We can't use std::make_unique here because the MetricsReporter constructor is private.
  return std::unique_ptr<MetricsReporter>{new MetricsReporter{std::move(config), runtime}};
}

MetricsReporter::MetricsReporter(ReportingConfig config, Runtime* runtime)
    : config_{std::move(config)}, runtime_{runtime} {}

MetricsReporter::~MetricsReporter() { MaybeStopBackgroundThread(); }

void MetricsReporter::MaybeStartBackgroundThread(SessionData session_data) {
  CHECK(!thread_.has_value());

  thread_.emplace(&MetricsReporter::BackgroundThreadRun, this);

  messages_.SendMessage(BeginSessionMessage{session_data});
}

void MetricsReporter::MaybeStopBackgroundThread() {
  if (thread_.has_value()) {
    messages_.SendMessage(ShutdownRequestedMessage{});
    thread_->join();
  }
}

void MetricsReporter::NotifyStartupCompleted() {
  if (thread_.has_value()) {
    messages_.SendMessage(StartupCompletedMessage{});
  }
}

void MetricsReporter::BackgroundThreadRun() {
  LOG_STREAM(DEBUG) << "Metrics reporting thread started";

  // AttachCurrentThread is needed so we can safely use the ART concurrency primitives within the
  // messages_ MessageQueue.
  const bool attached = runtime_->AttachCurrentThread(kBackgroundThreadName,
                                                      /*as_daemon=*/true,
                                                      runtime_->GetSystemThreadGroup(),
                                                      /*create_peer=*/true);
  bool running = true;

  // Configure the backends
  if (config_.dump_to_logcat) {
    backends_.emplace_back(new LogBackend(LogSeverity::INFO));
  }
  if (config_.dump_to_file.has_value()) {
    backends_.emplace_back(new FileBackend(config_.dump_to_file.value()));
  }
  if (config_.dump_to_statsd) {
    backends_.emplace_back(CreateStatsdBackend());
  }

  MaybeResetTimeout();

  while (running) {
    messages_.SwitchReceive(
        [&](BeginSessionMessage message) {
          LOG_STREAM(DEBUG) << "Received session metadata";

          for (auto& backend : backends_) {
            backend->BeginSession(message.session_data);
          }
        },
        [&]([[maybe_unused]] ShutdownRequestedMessage message) {
          LOG_STREAM(DEBUG) << "Shutdown request received";
          running = false;

          // Do one final metrics report, if enabled.
          if (config_.report_metrics_on_shutdown) {
            ReportMetrics();
          }
        },
        [&]([[maybe_unused]] TimeoutExpiredMessage message) {
          LOG_STREAM(DEBUG) << "Timer expired, reporting metrics";

          ReportMetrics();

          MaybeResetTimeout();
        },
        [&]([[maybe_unused]] StartupCompletedMessage message) {
          LOG_STREAM(DEBUG) << "App startup completed, reporting metrics";
          ReportMetrics();
        });
  }

  if (attached) {
    runtime_->DetachCurrentThread();
  }
  LOG_STREAM(DEBUG) << "Metrics reporting thread terminating";
}

void MetricsReporter::MaybeResetTimeout() {
  if (config_.periodic_report_seconds.has_value()) {
    messages_.SetTimeout(SecondsToMs(config_.periodic_report_seconds.value()));
  }
}

void MetricsReporter::ReportMetrics() const {
  ArtMetrics* metrics{runtime_->GetMetrics()};

  for (auto& backend : backends_) {
    metrics->ReportAllMetrics(backend.get());
  }
}

ReportingConfig ReportingConfig::FromRuntimeArguments(const RuntimeArgumentMap& args) {
  using M = RuntimeArgumentMap;
  return {
      .dump_to_logcat = args.Exists(M::WriteMetricsToLog),
      .dump_to_file = args.GetOptional(M::WriteMetricsToFile),
      .dump_to_statsd = args.Exists(M::WriteMetricsToStatsd),
      .report_metrics_on_shutdown = !args.Exists(M::DisableFinalMetricsReport),
      .periodic_report_seconds = args.GetOptional(M::MetricsReportingPeriod),
  };
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
