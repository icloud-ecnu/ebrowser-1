// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_H_
#define CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_H_

#include <stddef.h>

#include "base/callback_list.h"
#include "base/id_map.h"
#include "base/macros.h"
#include "base/optional.h"
#include "content/browser/media/session/audio_focus_manager.h"
#include "content/browser/media/session/media_session_uma_helper.h"
#include "content/common/content_export.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/media_metadata.h"

class MediaSessionBrowserTest;

namespace media {
enum class MediaContentType;
}  // namespace media

namespace content {

class AudioFocusDelegate;
class AudioFocusManagerTest;
class MediaSessionPlayerObserver;
class MediaSessionStateObserver;
class MediaSessionVisibilityBrowserTest;

// MediaSession manages the media session and audio focus for a given
// WebContents. It is requesting the audio focus, pausing when requested by the
// system and dropping it on demand.
// The audio focus can be of two types: Transient or Content. A Transient audio
// focus will allow other players to duck instead of pausing and will be
// declared as temporary to the system. A Content audio focus will not be
// declared as temporary and will not allow other players to duck. If a given
// WebContents can only have one audio focus at a time, it will be Content in
// case of Transient and Content audio focus are both requested.
// TODO(thakis,mlamouri): MediaSession isn't CONTENT_EXPORT'd because it creates
// complicated build issues with WebContentsUserData being a non-exported
// template, see htttps://crbug.com/589840. As a result, the class uses
// CONTENT_EXPORT for methods that are being used from tests. CONTENT_EXPORT
// should be moved back to the class when the Windows build will work with it.
class MediaSession : public WebContentsObserver,
                     protected WebContentsUserData<MediaSession> {
 public:
  enum class SuspendType {
    // Suspended by the system because a transient sound needs to be played.
    SYSTEM,
    // Suspended by the UI.
    UI,
    // Suspended by the page via script or user interaction.
    CONTENT,
  };

  // Only visible to tests.
  enum class State {
    ACTIVE,
    SUSPENDED,
    INACTIVE
  };

  // Returns the MediaSession associated to this WebContents. Creates one if
  // none is currently available.
  CONTENT_EXPORT static MediaSession* Get(WebContents* web_contents);

  ~MediaSession() override;

  void SetMetadata(const base::Optional<MediaMetadata>& metadata);
  const base::Optional<MediaMetadata>& metadata() const { return metadata_; }

  // Adds the given player to the current media session. Returns whether the
  // player was successfully added. If it returns false, AddPlayer() should be
  // called again later.
  CONTENT_EXPORT bool AddPlayer(MediaSessionPlayerObserver* observer,
                                int player_id,
                                media::MediaContentType media_content_type);

  // Removes the given player from the current media session. Abandons audio
  // focus if that was the last player in the session.
  CONTENT_EXPORT void RemovePlayer(MediaSessionPlayerObserver* observer,
                                   int player_id);

  // Removes all the players associated with |observer|. Abandons audio focus if
  // these were the last players in the session.
  CONTENT_EXPORT void RemovePlayers(MediaSessionPlayerObserver* observer);

  // Record that the session was ducked.
  void RecordSessionDuck();

  // Called when a player is paused in the content.
  // If the paused player is the last player, we suspend the MediaSession.
  // Otherwise, the paused player will be removed from the MediaSession.
  CONTENT_EXPORT void OnPlayerPaused(MediaSessionPlayerObserver* observer,
                                     int player_id);

  // Resume the media session.
  // |type| represents the origin of the request.
  CONTENT_EXPORT void Resume(SuspendType suspend_type);

  // Suspend the media session.
  // |type| represents the origin of the request.
  CONTENT_EXPORT void Suspend(SuspendType suspend_type);

  // Stop the media session.
  // |type| represents the origin of the request.
  CONTENT_EXPORT void Stop(SuspendType suspend_type);

  // Let the media session start ducking such that the volume multiplier is
  // reduced.
  CONTENT_EXPORT void StartDucking();

  // Let the media session stop ducking such that the volume multiplier is
  // recovered.
  CONTENT_EXPORT void StopDucking();

  // Returns if the session can be controlled by Resume() and Suspend calls
  // above.
  CONTENT_EXPORT bool IsControllable() const;

  // Returns if the session is currently active.
  CONTENT_EXPORT bool IsActive() const;

  // Returns if the session is currently suspended.
  // TODO(mlamouri): IsSuspended() below checks if the state is not ACTIVE
  // instead of checking if the state is SUSPENDED. In order to not have to
  // change all the callers and make the current refactoring ridiculously huge,
  // this method is introduced temporarily and will be removed later.
  CONTENT_EXPORT bool IsReallySuspended() const;

  // Returns if the session is currently suspended or inactive.
  CONTENT_EXPORT bool IsSuspended() const;

  // Returns the audio focus type. The type is updated everytime after the
  // session requests audio focus.
  CONTENT_EXPORT AudioFocusManager::AudioFocusType audio_focus_type() const {
    return audio_focus_type_;
  }

  // Returns whether the session has Pepper instances.
  bool HasPepper() const;

  // WebContentsObserver implementation
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<MediaSession>;
  friend class ::MediaSessionBrowserTest;
  friend class content::MediaSessionVisibilityBrowserTest;
  friend class content::AudioFocusManagerTest;
  friend class content::MediaSessionStateObserver;

  CONTENT_EXPORT void SetDelegateForTests(
      std::unique_ptr<AudioFocusDelegate> delegate);
  CONTENT_EXPORT bool IsActiveForTest() const;
  CONTENT_EXPORT void RemoveAllPlayersForTest();
  CONTENT_EXPORT MediaSessionUmaHelper* uma_helper_for_test();

  // Representation of a player for the MediaSession.
  struct PlayerIdentifier {
    PlayerIdentifier(MediaSessionPlayerObserver* observer, int player_id);
    PlayerIdentifier(const PlayerIdentifier&) = default;

    void operator=(const PlayerIdentifier&) = delete;
    bool operator==(const PlayerIdentifier& player_identifier) const;

    // Hash operator for base::hash_map<>.
    struct Hash {
      size_t operator()(const PlayerIdentifier& player_identifier) const;
    };

    MediaSessionPlayerObserver* observer;
    int player_id;
  };
  using PlayersMap = base::hash_set<PlayerIdentifier, PlayerIdentifier::Hash>;
  using StateChangedCallback = base::Callback<void(State)>;

  CONTENT_EXPORT explicit MediaSession(WebContents* web_contents);

  void Initialize();

  CONTENT_EXPORT void OnSuspendInternal(SuspendType suspend_type,
                                        State new_state);
  CONTENT_EXPORT void OnResumeInternal(SuspendType suspend_type);

  // Requests audio focus to the AudioFocusDelegate.
  // Returns whether the request was granted.
  CONTENT_EXPORT bool RequestSystemAudioFocus(
      AudioFocusManager::AudioFocusType audio_focus_type);

  // To be called after a call to AbandonAudioFocus() in order request the
  // delegate to abandon the audio focus.
  CONTENT_EXPORT void AbandonSystemAudioFocusIfNeeded();

  // Notifies WebContents about the state change of the media session.
  void UpdateWebContents();

  // Internal method that should be used instead of setting audio_focus_state_.
  // It sets audio_focus_state_ and notifies observers about the state change.
  void SetAudioFocusState(State audio_focus_state);

  // Update the volume multiplier when ducking state changes.
  void UpdateVolumeMultiplier();

  // Get the volume multiplier, which depends on whether the media session is
  // ducking.
  double GetVolumeMultiplier() const;

  // Registers a MediaSession state change callback.
  CONTENT_EXPORT std::unique_ptr<base::CallbackList<void(State)>::Subscription>
  RegisterMediaSessionStateChangedCallbackForTest(
      const StateChangedCallback& cb);

  CONTENT_EXPORT bool AddPepperPlayer(MediaSessionPlayerObserver* observer,
                                      int player_id);

  std::unique_ptr<AudioFocusDelegate> delegate_;
  PlayersMap players_;
  PlayersMap pepper_players_;

  State audio_focus_state_;
  SuspendType suspend_type_;
  AudioFocusManager::AudioFocusType audio_focus_type_;

  MediaSessionUmaHelper uma_helper_;

  // The ducking state of this media session. The initial value is |false|, and
  // is set to |true| after StartDucking(), and will be set to |false| after
  // StopDucking().
  bool is_ducking_;

  base::Optional<MediaMetadata> metadata_;
  base::CallbackList<void(State)> media_session_state_listeners_;

  DISALLOW_COPY_AND_ASSIGN(MediaSession);
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_H_
