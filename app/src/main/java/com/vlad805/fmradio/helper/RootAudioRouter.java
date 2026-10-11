package com.vlad805.fmradio.helper;

import android.util.Log;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;

/**
 * Runs audio routing commands with root privileges via an su shell.
 *
 * On Android 12+ the framework guards AudioSystem.setForceUse and the
 * AudioManager patch APIs behind the signature-level MODIFY_AUDIO_ROUTING
 * permission; attempts from an app process are rejected with a native JNI
 * error. The only channel left to a root-enabled app is to run those calls
 * inside an su-launched app_process — this class wraps that.
 *
 * All methods are synchronous with a short timeout; they are intended to be
 * called from a background thread (e.g. a service worker), never the UI
 * thread.
 */
public final class RootAudioRouter {
	private static final String TAG = "RootAudioRouter";
	private static final long TIMEOUT_MS = 5_000L;

	private RootAudioRouter() {
	}

	/**
	 * Executes the given command as root.
	 *
	 * @param command shell command to run under `su`
	 * @return true when the command exited with 0
	 */
	public static boolean run(final String command) {
		Process process = null;
		try {
			process = new ProcessBuilder("su").redirectErrorStream(true).start();
			final Process running = process;

			final Thread reader = new Thread(() -> {
				try (BufferedReader in = new BufferedReader(new InputStreamReader(running.getInputStream()))) {
					String line;
					while ((line = in.readLine()) != null) {
						Log.d(TAG, line);
					}
				} catch (final IOException e) {
					Log.d(TAG, "root output stream closed", e);
				}
			}, "RootAudioRouter-out");
			reader.setDaemon(true);
			reader.start();

			try (DataOutputStream os = new DataOutputStream(process.getOutputStream())) {
				os.writeBytes(command + "\nexit\n");
				os.flush();
			}

			final long deadline = System.currentTimeMillis() + TIMEOUT_MS;
			while (System.currentTimeMillis() < deadline) {
				try {
					final int rc = process.exitValue();
					reader.join(300L);
					return rc == 0;
				} catch (final IllegalThreadStateException ignored) {
					Thread.sleep(50L);
				}
			}

			Log.e(TAG, "root command timed out: " + command);
			process.destroy();
		} catch (final InterruptedException e) {
			Thread.currentThread().interrupt();
			if (process != null) {
				process.destroy();
			}
		} catch (final IOException | SecurityException e) {
			Log.e(TAG, "failed to run root command: " + command, e);
		}
		return false;
	}

	/**
	 * Switches the FM tuner loopback to the given output device.
	 *
	 * @param device "speaker" | "wired"（自动选择 headphone 或带麦 headset）
	 * @return true on success
	 */
	public static boolean routeFm(final String device) {
		return run("CLASSPATH=" + dexPath() + " app_process / FmPatch route " + device);
	}

	/**
	 * Releases any existing FM tuner patches.
	 *
	 * @return true on success
	 */
	public static boolean releaseFmPatches() {
		return run("CLASSPATH=" + dexPath() + " app_process / FmPatch release");
	}

	private static String dexPath() {
		final String custom = System.getenv("RFM_FMPATCH_DEX");
		return custom != null ? custom : "/data/local/tmp/fmpatch.dex";
	}
}
