// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Copyright 2022 Whist Technologies, Inc. All rights reserved.

#ifndef WHIST_BROWSER_HYBRID_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_WHIST_COMPOSITOR_H_
#define WHIST_BROWSER_HYBRID_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_WHIST_COMPOSITOR_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "cc/layers/surface_layer.h"
#include "cc/layers/video_frame_provider.h"
#include "media/base/media_util.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/platform/web_media_player.h"
#include "third_party/blink/public/platform/web_video_frame_submitter.h"
#include "third_party/blink/renderer/modules/mediastream/video_renderer_algorithm_wrapper.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_media.h"
#include "third_party/blink/renderer/platform/wtf/thread_safe_ref_counted.h"

namespace base {
class SingleThreadTaskRunner;
class WaitableEvent;
}

namespace gfx {
class Size;
}

namespace media {
class VideoRendererAlgorithm;
}

namespace viz {
class SurfaceId;
}

namespace blink {
class WhistPlayer;
struct WhistCompositorTraits;

// This class is designed to handle the work load on compositor thread for
// WebMediaPlayerMS. It will be instantiated on the main thread, but destroyed
// on the thread holding the last reference.
//
// WhistCompositor utilizes VideoRendererAlgorithm to store the
// incoming frames and select the best frame for rendering to maximize the
// smoothness, if REFERENCE_TIMEs are populated for incoming VideoFrames.
// Otherwise, WhistCompositor will simply store the most recent
// frame, and submit it whenever asked by the compositor.
class MODULES_EXPORT WhistCompositor
    : public cc::VideoFrameProvider,
      public WTF::ThreadSafeRefCounted<WhistCompositor,
                                       WhistCompositorTraits> {
 public:
  using OnNewFramePresentedCB = base::OnceClosure;

  WhistCompositor(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
      std::unique_ptr<WebVideoFrameSubmitter> submitter,
      WebMediaPlayer::SurfaceLayerMode surface_layer_mode,
      const base::WeakPtr<WhistPlayer>& player);

  WhistCompositor(const WhistCompositor&) = delete;
  WhistCompositor& operator=(const WhistCompositor&) =
      delete;

  // Can be called from any thread.
  cc::UpdateSubmissionStateCB GetUpdateSubmissionStateCallback() {
    return update_submission_state_callback_;
  }

  void EnqueueFrame(scoped_refptr<media::VideoFrame> frame, bool is_copy);

  // Statistical data
  gfx::Size GetCurrentSize();
  base::TimeDelta GetCurrentTime();
  size_t total_frame_count();
  size_t dropped_frame_count();

  // Signals the VideoFrameSubmitter to prepare to receive BeginFrames and
  // submit video frames given by WhistCompositor.
  virtual void EnableSubmission(
      const viz::SurfaceId& id,
      media::VideoTransformation transformation,
      bool force_submit);

  // Notifies the |submitter_| that the frames must be submitted.
  void SetForceSubmit(bool force_submit);

  // Notifies the |submitter_| that the page is no longer visible.
  void SetIsPageVisible(bool is_visible);

  // VideoFrameProvider implementation.
  void SetVideoFrameProviderClient(
      cc::VideoFrameProvider::Client* client) override;
  bool UpdateCurrentFrame(base::TimeTicks deadline_min,
                          base::TimeTicks deadline_max) override;
  bool HasCurrentFrame() override;
  scoped_refptr<media::VideoFrame> GetCurrentFrame() override;
  void PutCurrentFrame() override;
  base::TimeDelta GetPreferredRenderInterval() override;

  void StartRendering();
  void StopRendering();
  void ReplaceCurrentFrameWithACopy();

  void RenderWithoutAlgorithm(scoped_refptr<media::VideoFrame> frame,
                              bool is_copy);

  // Tell |video_frame_provider_client_| to stop using this instance in
  // preparation for dtor.
  void StopUsingProvider();

  // Sets a hook to be notified when a new frame is presented, to fulfill a
  // prending video.requestAnimationFrame() request.
  // Can be called from any thread.
  void SetOnFramePresentedCallback(OnNewFramePresentedCB presented_cb);

  // Gets the metadata for the last frame that was presented to the compositor.
  // Used to populate the VideoFrameMetadata of video.requestVideoFrameCallback
  // callbacks. See https://wicg.github.io/video-rvfc/.
  // Can be called on any thread.
  std::unique_ptr<WebMediaPlayer::VideoFramePresentationMetadata>
  GetLastPresentedFrameMetadata();

  // Sets the ForceBeginFrames flag on |submitter_|. Can be called from any
  // thread.
  //
  // The flag is used to keep receiving BeginFrame()/UpdateCurrentFrame() calls
  // even if the video element is not visible, so websites can still use the
  // requestVideoFrameCallback() API when the video is offscreen.
  void SetForceBeginFrames(bool enable);

 private:
  friend class WTF::ThreadSafeRefCounted<WhistCompositor,
                                         WhistCompositorTraits>;
  // friend class WebMediaPlayerMSTest;
  friend struct WhistCompositorTraits;

  ~WhistCompositor() override;

  // Ran on the |video_frame_compositor_task_runner_| to initialize
  // |submitter_|
  void InitializeSubmitter();

  // Signals the VideoFrameSubmitter to stop submitting frames.
  void SetIsSurfaceVisible(bool, base::WaitableEvent*);

  // The use of std::vector here is OK because this method is bound into a
  // base::OnceCallback instance, and passed to media::VideoRendererAlgorithm
  // ctor.
  bool MapTimestampsToRenderTimeTicks(
      const std::vector<base::TimeDelta>& timestamps,
      std::vector<base::TimeTicks>* wall_clock_times);

  // For algorithm disabled case only: call SetCurrentFrame() with the current
  // frame immediately. |video_frame_provider_client_| gets notified about the
  // new frame with a DidReceiveFrame() call.
  // void RenderWithoutAlgorithm(scoped_refptr<media::VideoFrame> frame,
  //                             bool is_copy);
  void RenderWithoutAlgorithmOnCompositor(
      scoped_refptr<media::VideoFrame> frame,
      bool is_copy);

  // Update |current_frame_| and |dropped_frame_count_|
  void SetCurrentFrame(
      scoped_refptr<media::VideoFrame> frame,
      bool is_copy,
      absl::optional<base::TimeTicks> expected_presentation_time);

  // Following the update to |current_frame_|, this will check for changes that
  // require updating video layer.
  void CheckForFrameChanges(
      bool is_first_frame,
      bool has_frame_size_changed,
      absl::optional<media::VideoTransformation> new_frame_transform,
      absl::optional<bool> new_frame_opacity);

  void StartRenderingInternal();
  void StopRenderingInternal();
  void StopUsingProviderInternal();
  void ReplaceCurrentFrameWithACopyInternal();

  void SetAlgorithmEnabledForTesting(bool algorithm_enabled);

  // Used for DCHECKs to ensure method calls executed in the correct thread,
  // which is renderer main thread in this class.
  THREAD_CHECKER(thread_checker_);

  const scoped_refptr<base::SingleThreadTaskRunner> video_frame_compositor_task_runner_;
  const scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  const scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

  base::WeakPtr<WhistPlayer> player_;

  // TODO(qiangchen, emircan): It might be nice to use a real MediaLog here from
  // the WebMediaPlayerMS instance, but it owns the MediaLog and this class has
  // non-deterministic destruction paths (either compositor or IO).
  media::NullMediaLog media_log_;

  size_t serial_;

  // A pointer back to the compositor to inform it about state changes. This
  // is not |nullptr| while the compositor is actively using this
  // VideoFrameProvider. This will be set to |nullptr| when the compositor stops
  // serving this VideoFrameProvider.
  cc::VideoFrameProvider::Client* video_frame_provider_client_;

  // |current_frame_| is updated only on compositor thread. The object it
  // holds can be freed on the compositor thread if it is the last to hold a
  // reference but media::VideoFrame is a thread-safe ref-pointer. It is
  // however read on the compositor and main thread so locking is required
  // around all modifications and all reads on the any thread.
  scoped_refptr<media::VideoFrame> current_frame_;

  // Historical data about last rendering. These are for detecting whether
  // rendering is paused (one reason is that the tab is not in the front), in
  // which case we need to do background rendering.
  base::TimeTicks last_deadline_max_;
  base::TimeDelta last_render_length_;

  size_t total_frame_count_;
  size_t dropped_frame_count_;

  bool current_frame_is_copy_ = false;

  // Used to complete video.requestAnimationFrame() calls. Reported up via
  // GetLastPresentedFrameMetadata().
  // TODO(https://crbug.com/1050755): Improve the accuracy of these fields for
  // cases where we only use RenderWithoutAlgorithm().
  base::TimeTicks last_presentation_time_ GUARDED_BY(current_frame_lock_);
  base::TimeTicks last_expected_display_time_ GUARDED_BY(current_frame_lock_);
  size_t presented_frames_ GUARDED_BY(current_frame_lock_) = 0u;

  // The value of GetPreferredRenderInterval() the last time |current_frame_|
  // was updated. Used by GetLastPresentedFrameMetadata(), to prevent calling
  // GetPreferredRenderInterval() from the main thread.
  base::TimeDelta last_preferred_render_interval_
      GUARDED_BY(current_frame_lock_);

  bool stopped_;
  bool render_started_;

  // Called when a new frame is enqueued, either in RenderWithoutAlgorithm() or
  // in RenderUsingAlgorithm(). Used to fulfill video.requestAnimationFrame()
  // requests.
  base::Lock new_frame_presented_cb_lock_;
  OnNewFramePresentedCB new_frame_presented_cb_;

  std::unique_ptr<WebVideoFrameSubmitter> submitter_;

  cc::UpdateSubmissionStateCB update_submission_state_callback_;

  // |current_frame_lock_| protects |current_frame_|, |rendering_frame_buffer_|,
  // |dropped_frame_count_|, and |render_started_|.
  base::Lock current_frame_lock_;

  base::WeakPtrFactory<WhistCompositor> weak_ptr_factory_{this};
};

struct WhistCompositorTraits {
  // Ensure destruction occurs on main thread so that "Web" and other resources
  // are destroyed on the correct thread.
  static void Destruct(const WhistCompositor* player);
};

}  // namespace blink

#endif  // WHIST_BROWSER_HYBRID_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_WHIST_COMPOSITOR_H_
