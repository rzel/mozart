// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/composition/cpp/frame_tracker.h"

#include "lib/ftl/logging.h"

namespace mozart {

FrameTracker::FrameTracker() {}

FrameTracker::~FrameTracker() {}

void FrameTracker::Clear() {
  frame_count_ = 0u;
  frame_info_ = FrameInfo();
  frame_time_delta_ = ftl::TimeDelta::Zero();
}

void FrameTracker::Update(const FrameInfo& raw_frame_info, MojoTimeTicks now) {
  const int64_t old_frame_time = frame_info_.frame_time;
  const int64_t old_presentation_time = frame_info_.presentation_time;
  frame_info_ = raw_frame_info;

  // Ensure frame info is sane since it comes from another service.
  if (frame_info_.frame_time > now) {
    FTL_LOG(WARNING) << "Frame time is in the future: frame_time="
                     << frame_info_.frame_time << ", now=" << now;
    frame_info_.frame_time = now;
  }
  if (frame_info_.frame_deadline < frame_info_.frame_time) {
    FTL_LOG(WARNING)
        << "Frame deadline is earlier than frame time: frame_deadline="
        << frame_info_.frame_deadline
        << ", frame_time=" << frame_info_.frame_time << ", now=" << now;
    frame_info_.frame_deadline = frame_info_.frame_time;
  }
  if (frame_info_.presentation_time < frame_info_.frame_deadline) {
    FTL_LOG(WARNING) << "Presentation time is earlier than frame deadline: "
                        "presentation_time="
                     << frame_info_.presentation_time
                     << ", frame_deadline=" << frame_info_.frame_deadline
                     << ", now=" << now;
    frame_info_.presentation_time = frame_info_.frame_deadline;
  }

  // Compensate for significant lag by adjusting the frame time if needed
  // to step past skipped frames.
  uint64_t lag = now - frame_info_.frame_time;
  if (frame_info_.frame_interval > 0u && lag >= frame_info_.frame_interval) {
    uint64_t offset = lag % frame_info_.frame_interval;
    uint64_t adjustment = now - offset - frame_info_.frame_time;
    frame_info_.frame_time = now - offset;
    frame_info_.frame_deadline += adjustment;
    frame_info_.presentation_time += adjustment;

    // Jank warning.
    // TODO(jeffbrown): Suppress this once we're happy with things.
    FTL_LOG(WARNING) << "Missed " << frame_info_.frame_interval
                     << " us frame deadline by " << lag << " us, skipping "
                     << (lag / frame_info_.frame_interval) << " frames";
  }

  // Ensure monotonicity.
  if (frame_count_++ == 0u)
    return;
  if (frame_info_.frame_time < old_frame_time) {
    FTL_LOG(WARNING) << "Frame time is going backwards: new="
                     << frame_info_.frame_time << ", old=" << old_frame_time
                     << ", now=" << now;
    frame_info_.frame_time = old_frame_time;
  }
  if (frame_info_.presentation_time < old_presentation_time) {
    FTL_LOG(WARNING) << "Presentation time is going backwards: new="
                     << frame_info_.presentation_time
                     << ", old=" << old_presentation_time << ", now=" << now;
    frame_info_.presentation_time = old_presentation_time;
  }
  frame_time_delta_ =
      ftl::TimeDelta::FromMicroseconds(frame_info_.frame_time - old_frame_time);
}

}  // namespace mozart
