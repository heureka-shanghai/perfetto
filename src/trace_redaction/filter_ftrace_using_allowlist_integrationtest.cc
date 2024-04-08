/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <cstdint>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "src/base/test/status_matchers.h"
#include "src/base/test/tmp_dir_tree.h"
#include "src/base/test/utils.h"
#include "src/trace_redaction/filter_ftrace_using_allowlist.h"
#include "src/trace_redaction/populate_allow_lists.h"
#include "src/trace_redaction/scrub_ftrace_events.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

class FilterFtraceUsingAllowlistTest : public testing::Test {
 protected:
  void SetUp() override {
    redactor_.emplace_build<PopulateAllowlists>();

    auto* scrub_ftrace_events =
        redactor_.emplace_transform<ScrubFtraceEvents>();
    scrub_ftrace_events->emplace_back<FilterFtraceUsingAllowlist>();

    src_trace_ =
        base::GetTestDataPath("test/data/trace-redaction-general.pftrace");

    dest_trace_ = tmp_dir_.AbsolutePath("dst.pftrace");
  }

  base::Status Redact() {
    auto status = redactor_.Redact(src_trace_, dest_trace_, &context_);

    // If redaction failed, the redactor should not have written the file to
    // disk.
    if (status.ok()) {
      tmp_dir_.TrackFile("dst.pftrace");
    }

    return status;
  }

  base::StatusOr<std::string> LoadOriginal() const {
    return ReadRawTrace(src_trace_);
  }

  base::StatusOr<std::string> LoadRedacted() const {
    return ReadRawTrace(dest_trace_);
  }

  // Parse the given buffer and gather field ids from across all events. This
  // will also include fields like timestamp.
  base::FlatSet<uint32_t> ParseEvents(std::string trace_buffer) {
    base::FlatSet<uint32_t> event_ids;

    protos::pbzero::Trace::Decoder trace_decoder(trace_buffer);

    for (auto packet = trace_decoder.packet(); packet; ++packet) {
      protos::pbzero::TracePacket::Decoder packet_decoder(*packet);

      if (!packet_decoder.has_ftrace_events()) {
        continue;
      }

      protos::pbzero::FtraceEventBundle::Decoder bundle_decoder(
          packet_decoder.ftrace_events());

      for (auto event = bundle_decoder.event(); event; ++event) {
        protozero::ProtoDecoder event_decoder(*event);

        for (auto field = event_decoder.ReadField(); field.valid();
             field = event_decoder.ReadField()) {
          event_ids.insert(field.id());
        }
      }
    }

    return event_ids;
  }

 private:
  base::StatusOr<std::string> ReadRawTrace(const std::string& path) const {
    std::string redacted_buffer;

    if (base::ReadFile(path, &redacted_buffer)) {
      return redacted_buffer;
    }

    return base::ErrStatus("Failed to read %s", path.c_str());
  }

  Context context_;
  TraceRedactor redactor_;

  base::TmpDirTree tmp_dir_;

  std::string src_trace_;
  std::string dest_trace_;
};

// This is not a test of FilterFtraceUsingAllowlist, but instead verifies of the
// sample trace used in the test.
TEST_F(FilterFtraceUsingAllowlistTest, TraceHasAllEvents) {
  auto trace = LoadOriginal();
  ASSERT_OK(trace) << trace->c_str();

  auto events = ParseEvents(std::move(trace.value()));
  ASSERT_EQ(events.size(), 14u);

  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kCpuFrequencyFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kCpuIdleFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kOomScoreAdjUpdateFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kPidFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kPrintFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedProcessExitFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedProcessFreeFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakeupFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakeupNewFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakingFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kTaskNewtaskFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kTaskRenameFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kTimestampFieldNumber));
}

TEST_F(FilterFtraceUsingAllowlistTest, RetainsAllowedEvents) {
  auto redacted = Redact();
  ASSERT_OK(redacted) << redacted.c_message();

  auto trace = LoadRedacted();
  ASSERT_OK(trace) << trace.status().c_message();

  auto events = ParseEvents(std::move(trace.value()));

  // These are not events, they are fields that exist alongside the event.
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kPidFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kTimestampFieldNumber));

  // These are events.
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kCpuFrequencyFieldNumber));
  ASSERT_TRUE(events.count(protos::pbzero::FtraceEvent::kCpuIdleFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedProcessFreeFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakingFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kTaskNewtaskFieldNumber));
  ASSERT_TRUE(
      events.count(protos::pbzero::FtraceEvent::kTaskRenameFieldNumber));
}

TEST_F(FilterFtraceUsingAllowlistTest, RemovesNotAllowedEvents) {
  auto redacted = Redact();
  ASSERT_OK(redacted) << redacted.c_message();

  auto trace = LoadRedacted();
  ASSERT_OK(trace) << trace.status().c_message();

  auto events = ParseEvents(std::move(trace.value()));

  // These are events.
  ASSERT_FALSE(
      events.count(protos::pbzero::FtraceEvent::kOomScoreAdjUpdateFieldNumber));
  ASSERT_FALSE(events.count(protos::pbzero::FtraceEvent::kPrintFieldNumber));
  ASSERT_FALSE(
      events.count(protos::pbzero::FtraceEvent::kSchedProcessExitFieldNumber));
  ASSERT_FALSE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakeupFieldNumber));
  ASSERT_FALSE(
      events.count(protos::pbzero::FtraceEvent::kSchedWakeupNewFieldNumber));
}

}  // namespace perfetto::trace_redaction
