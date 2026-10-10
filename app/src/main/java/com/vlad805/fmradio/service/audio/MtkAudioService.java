package com.vlad805.fmradio.service.audio;

import android.content.Context;

import com.vlad805.fmradio.helper.Audio;

/**
 * MediaTek FM audio on this HAL generation is a hostless loopback: a patch
 * with no threads routes the tuner to the current output device entirely
 * inside the audio HAL. App-space PCM capture returns zeros for every
 * AudioRecord configuration tried (and the stock FM service shows the same),
 * so there is no bridge and no recording support — the record action shows
 * the existing "unsupported" toast via the IAudioRecordable absence path.
 *
 * The HAL picks up FM routing from the same calls MIUI's FM service makes:
 * AudioManager.setSpeakerphoneOn + AudioSystem.setForceUse (the latter via
 * the app's existing Audio helper).
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
		mAudioManager.setSpeakerphoneOn(enabled);
		Audio.toggleThroughSpeaker(enabled);
	}
}
