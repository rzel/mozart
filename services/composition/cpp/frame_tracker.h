// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_
#define APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_

#include "apps/mozart/services/composition/interfaces/scheduling.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace mozart {

// Tracks frame scheduling information.
class FrameTracker {
 public:
  FrameTracker();
  ~FrameTracker();

  // Returns the number of frames that have been tracked.
  uint64_t frame_count() const { return frame_count_; }

  // Returns the current frame info.
  // This value is not meaningful when |frame_count()| is zero.
  const FrameInfo& frame_info() const { return frame_info_; }

  // Returns the difference between the previous frame time and the
  // current frame time, or 0 if this is the first frame.
  // This value is guaranteed to be non-negative.
  ftl::TimeDelta frame_time_delta() const { return frame_time_delta_; }

  // Clears the frame tracker's state such that the next update will be
  // treated as if it were the first.
  void Clear();

  // Updates the properties of this object with new frame scheduling
  // information from |raw_frame_info| and applies compensation for lag.
  //
  // |now| should come from a recent call to |mojo::GetTimeTicksNow()|.
  //
  // Whenever an application receives new frame scheduling information from the
  // system, it should call this function before using it.
  void Update(const FrameInfo& raw_frame_info, MojoTimeTicks now);

 private:
  uint64_t frame_count_ = 0u;
  FrameInfo frame_info_;
  ftl::TimeDelta frame_time_delta_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FrameTracker);
};

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_
