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

bool MetricsReporter::IsPeriodicReportingEnabled() const {
  return config_.periodic_report_seconds.has_value();
}

void MetricsReporter::SetReportingPeriod(unsigned int period_seconds) {
  DCHECK(!thread_.has_value()) << "The reporting period should not be changed after the background "
                                  "reporting thread is started.";

  config_.periodic_report_seconds = period_seconds;

  // Since we've explicitly set the reporting period, disable backoff.
  config_.enable_periodic_reporting_backoff = false;
}

bool MetricsReporter::MaybeStartBackgroundThread(SessionData session_data) {
  CHECK(!thread_.has_value());
  thread_.emplace(&MetricsReporter::BackgroundThreadRun, this);
  messages_.SendMessage(BeginSessionMessage{session_data});
  return true;
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

void MetricsReporter::RequestMetricsReport(bool synchronous) {
  if (thread_.has_value()) {
    messages_.SendMessage(RequestMetricsReportMessage{synchronous});
    if (synchronous) {
      thread_to_host_messages_.ReceiveMessage();
    }
  }
}

void MetricsReporter::SetCompilationInfo(CompilationReason compilation_reason,
                                         CompilerFilter::Filter compiler_filter) {
  if (thread_.has_value()) {
    messages_.SendMessage(CompilationInfoMessage{compilation_reason, compiler_filter});
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
    auto backend = CreateStatsdBackend();
    if (backend != nullptr) {
      backends_.emplace_back(std::move(backend));
    }
  }

  MaybeResetTimeout();

  while (running) {
    messages_.SwitchReceive(
        [&](BeginSessionMessage message) {
          LOG_STREAM(DEBUG) << "Received session metadata";
          session_data_ = message.session_data;
        },
        [&]([[maybe_unused]] ShutdownRequestedMessage message) {
          LOG_STREAM(DEBUG) << "Shutdown request received";
          running = false;

          // Do one final metrics report, if enabled.
          if (config_.report_metrics_on_shutdown) {
            ReportMetrics();
          }
        },
        [&](RequestMetricsReportMessage message) {
          LOG_STREAM(DEBUG) << "Explicit report request received";
          ReportMetrics();
          if (message.synchronous) {
            thread_to_host_messages_.SendMessage(ReportCompletedMessage{});
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
        },
        [&](CompilationInfoMessage message) {
          LOG_STREAM(DEBUG) << "Compilation info received";
          session_data_.compilation_reason = message.compilation_reason;
          session_data_.compiler_filter = message.compiler_filter;
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
    if (config_.enable_periodic_reporting_backoff) {
      config_.enable_periodic_reporting_backoff *= 2;
    }
  }
}

void MetricsReporter::ReportMetrics() {
  ArtMetrics* metrics{runtime_->GetMetrics()};

  if (!session_started_) {
    for (auto& backend : backends_) {
      backend->BeginSession(session_data_);
    }
    session_started_ = true;
  }

  for (auto& backend : backends_) {
    metrics->ReportAllMetrics(backend.get());
  }
}

ReportingConfig ReportingConfig::FromRuntimeArguments(const RuntimeArgumentMap& args) {
  using M = RuntimeArgumentMap;

  // For periodic reporting, we currently have two modes. If we have a period set explicitly, we use
  // that without any backoff. Otherwise, we choose a starting period randomly between 30 and 60
  // seconds (to prevent all apps from reporting on the same schedule), and then double the period
  // on each report.
  std::optional<unsigned int> reporting_period = args.GetOptional(M::MetricsReportingPeriod);
  bool enable_periodic_reporting_backoff = !reporting_period.has_value();

  if (enable_periodic_reporting_backoff && !reporting_period.has_value()) {
    reporting_period = GetRandomNumber(30, 60);
  }

  return {
      .dump_to_logcat = args.Exists(M::WriteMetricsToLog),
      .dump_to_statsd = args.GetOrDefault(M::WriteMetricsToStatsd),
      .dump_to_file = args.GetOptional(M::WriteMetricsToFile),
      .report_metrics_on_shutdown = !args.Exists(M::DisableFinalMetricsReport),
      .periodic_report_seconds = reporting_period,
      .enable_periodic_reporting_backoff = enable_periodic_reporting_backoff,
  };
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
