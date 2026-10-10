package com.vlad805.fmradio.service.audio;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioRecord;
import android.os.Build;
import android.util.Log;

/**
 * MediaTek FM audio: the tuner delivers PCM over Connsys I2S into the audio
 * HAL; there is no hardware loopback, so playback is a software bridge —
 * capture from the FM tuner input device, forward to AudioTrack (playback)
 * and to the recorder session (recording), same shape as LightAudioService.
 */
public class MtkAudioService extends LightAudioService {
	private static final String TAG = "MAS";

	public MtkAudioService(final Context context) {
		super(context, 48000); // Connsys I2S capture rate
	}

	@Override
	protected AudioRecord getAudioRecorder() {
		final int bufferSize = Math.max(
				AudioRecord.getMinBufferSize(
						mSampleRate,
						android.media.AudioFormat.CHANNEL_IN_STEREO,
						android.media.AudioFormat.ENCODING_PCM_16BIT
				),
				mBufferSize
		);
		final AudioRecord bound = createFmTunerRecorder(mSampleRate, bufferSize);
		if (bound != null) {
			return bound;
		}
		Log.w(TAG, "FM tuner device binding failed, falling back to legacy source");
		return super.getAudioRecorder();
	}

	@Override
	public void setSpeakerEnabled(final boolean enabled) {
		try {
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
				if (enabled) {
					for (final AudioDeviceInfo device : mAudioManager.getAvailableCommunicationDevices()) {
						if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
							mAudioManager.setCommunicationDevice(device);
							return;
						}
					}
				} else {
					mAudioManager.clearCommunicationDevice();
				}
			}
			mAudioManager.setSpeakerphoneOn(enabled);
		} catch (Throwable t) {
			Log.w(TAG, "setSpeakerEnabled failed for speaker=" + enabled, t);
		}
	}
}
