package com.vlad805.fmradio.service.audio;

import android.content.Context;

import com.vlad805.fmradio.helper.RootAudioRouter;

/**
 * MediaTek FM audio on this HAL generation is a hostless loopback: a patch
 * with no threads routes the tuner to the current output device entirely
 * inside the audio HAL. App-space PCM capture returns zeros for every
 * AudioRecord configuration tried (and the stock FM service shows the same),
 * so there is no bridge and no recording support — the record action shows
 * the existing "unsupported" toast via the IAudioRecordable absence path.
 *
 * Routing to speaker requires root: the framework rejects
 * AudioSystem.setForceUse and the AudioManager patch APIs from an app
 * process with MODIFY_AUDIO_ROUTING. The only working channel is running
 * those calls inside an su-launched app_process (see RootAudioRouter).
 */
public class MtkAudioService extends AudioService {
	public MtkAudioService(final Context context) {
		super(context);
	}

	@Override
	public void startAudio() {
		requestForFocus(true);
	}

	@Override
	public void stopAudio() {
		requestForFocus(false);
	}

	@Override
	public void setSpeakerEnabled(final boolean enabled) {
		// setSpeakerphoneOn is not required for the hostless FM loopback and
		// only affects the communication device; skip it. The actual routing
		// change is done via root because MODIFY_AUDIO_ROUTING is denied.
		if (enabled) {
			RootAudioRouter.routeFm("speaker");
		} else {
			RootAudioRouter.routeFm("headphone");
		}
	}
}
