package com.vlad805.fmradio.service.audio;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import static com.vlad805.fmradio.Utils.sleep;

/**
 * Audio service records sound from
 * a specific source (unavailable for playback through speakers) and outputs it
 * to a regular audio output (headphones or speaker).
 * vlad805 (c) 2019
 */
public abstract class AudioService {
	protected final AudioManager mAudioManager;

	protected int mSampleRate;
	protected int mBufferSize = 16384;
	protected int mAudioSource = 1998;

	public AudioService(Context context) {
		this(context, 44100);
	}

	public AudioService(Context context, int sampleRate) {
		mAudioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
		mSampleRate = sampleRate;
	}

	/**
	 * Finds the system's FM tuner input device, if the audio HAL exposes one.
	 *
	 * @return FM tuner input device or null
	 */
	protected AudioDeviceInfo findFmTunerInputDevice() {
		if (mAudioManager == null) {
			return null;
		}
		for (final AudioDeviceInfo device : mAudioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
			if (device.getType() == AudioDeviceInfo.TYPE_FM_TUNER) {
				return device;
			}
		}
		return null;
	}

	/**
	 * Creates an AudioRecord bound to the FM tuner input device.
	 *
	 * @return Initialized recorder, or null when the device or construction fails
	 */
	protected AudioRecord createFmTunerRecorder(final int sampleRate, final int bufferSizeBytes) {
		final AudioDeviceInfo fmTuner = findFmTunerInputDevice();
		if (fmTuner == null) {
			return null;
		}
		try {
			final AudioFormat format = new AudioFormat.Builder()
					.setSampleRate(sampleRate)
					.setEncoding(AudioFormat.ENCODING_PCM_16BIT)
					.setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
					.build();
			final AudioRecord recorder = new AudioRecord.Builder()
					.setAudioSource(MediaRecorder.AudioSource.DEFAULT)
					.setAudioFormat(format)
					.setBufferSizeInBytes(bufferSizeBytes)
					.build();
			if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
				recorder.release();
				return null;
			}
			recorder.setPreferredDevice(fmTuner);
			return recorder;
		} catch (Throwable t) {
			return null;
		}
	}

	/**
	 * Start recording audio from a specific source and write to normal audio output
	 */
	public abstract void startAudio();

	/**
	 * Stop redirecting audio
	 */
	public abstract void stopAudio();

	/**
	 * Applies the recording pre-roll preference to the active audio implementation.
	 *
	 * @param enabled Whether recent PCM history should be retained
	 */
	public void setPreRollEnabled(final boolean enabled) {
		// Audio services without recording support ignore this preference.
	}

	/**
	 * Update FM output route when user toggles speaker mode.
	 */
	public void setSpeakerEnabled(final boolean enabled) {
		// Default implementation is for AudioRecord-based services and does nothing.
	}

	/**
	 * AudioSource:
	 * DEFAULT              work, HQ, loud
	 * MIC                  work, HW
	 * VOICE_UPLINK         not work
	 * VOICE_DOWNLINK       not work
	 * VOICE_CALL           not work
	 * CAMCORDER            work, LQ, very sensitive
	 * VOICE_RECOGNITION    work, LQ, only left channel
	 * VOICE_COMMUNICATION  work, LQ, very sensitive
	 * REMOTE_SUBMIX        not work
	 * 1998 = FM            work (Mi A1) / not work (Xperia L)
	 */
	protected AudioRecord getAudioRecorder() {
		// AUDIO_CHANNEL_IN_FRONT_BACK?
		try {
			final AudioRecord recorder = new AudioRecord(
					mAudioSource,
					mSampleRate,
					AudioFormat.CHANNEL_IN_STEREO,
					AudioFormat.ENCODING_PCM_16BIT,
					mBufferSize
			);

			sleep(200);

			if (recorder.getState() == AudioRecord.STATE_INITIALIZED) { // If works, then done
				return recorder;
			}
		} catch (Exception e) {
			e.printStackTrace();
		}

		return null;
	}

	protected void requestForFocus(boolean needFocus) {
		if (needFocus) { // If focus desired...
			mAudioManager.requestAudioFocus(null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK);
		} else { // If focus return...
			mAudioManager.abandonAudioFocus(null);
		}
	}
}
