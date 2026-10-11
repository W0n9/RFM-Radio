package com.vlad805.fmradio.service.audio;

import android.content.Context;

import com.vlad805.fmradio.helper.RootAudioRouter;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * MediaTek FM audio on this HAL generation is a hostless loopback: a patch
 * with no threads routes the tuner to the current output device entirely
 * inside the audio HAL. App-space PCM capture returns zeros for every
 * AudioRecord configuration tried (and the stock FM service shows the same),
 * so there is no bridge and no recording support — the record action shows
 * the existing "unsupported" toast via the IAudioRecordable absence path.
 *
 * The loopback only exists while an FM_TUNER audio patch is present, so this
 * service creates one on start and releases it on stop (mirroring MIUI's FM
 * service). Creating patches and AudioSystem.setForceUse both require
 * MODIFY_AUDIO_ROUTING, which Android 12 denies to app processes — the calls
 * are therefore executed inside an su-launched app_process (RootAudioRouter),
 * off the main thread on a serialized worker.
 */
public class MtkAudioService extends AudioService {
	private final ExecutorService mRouter = Executors.newSingleThreadExecutor();
	private volatile boolean mSpeakerEnabled = false;

	public MtkAudioService(final Context context) {
		super(context);
	}

	@Override
	public void startAudio() {
		requestForFocus(true);
		// Without an FM patch the hostless loopback is silent on every output.
		mRouter.execute(() -> RootAudioRouter.routeFm(mSpeakerEnabled ? "speaker" : "wired"));
	}

	@Override
	public void stopAudio() {
		requestForFocus(false);
		mRouter.execute(RootAudioRouter::releaseFmPatches);
	}

	@Override
	public void setSpeakerEnabled(final boolean enabled) {
		mSpeakerEnabled = enabled;
		mRouter.execute(() -> RootAudioRouter.routeFm(enabled ? "speaker" : "wired"));
	}
}
