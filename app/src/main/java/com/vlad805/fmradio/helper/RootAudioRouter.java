package com.vlad805.fmradio.helper;

import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.security.MessageDigest;

/**
 * Runs audio routing commands with root privileges via an su shell.
 *
 * On Android 12+ the framework guards AudioSystem.setForceUse and the
 * AudioManager patch APIs behind the signature-level MODIFY_AUDIO_ROUTING
 * permission; attempts from an app process are rejected with a native JNI
 * error. The only channel left to a root-enabled app is to run those calls
 * inside an su-launched app_process — this class wraps that.
 *
 * The helper class (FmPatch) is bundled as assets/fmpatch.dex and deployed to
 * DEX_PATH on first use (re-deployed when the checksum diverges).
 *
 * All methods are synchronous with a short timeout; they are intended to be
 * called from a background thread (e.g. a service worker), never the UI
 * thread.
 */
public final class RootAudioRouter {
	private static final String TAG = "RootAudioRouter";
	private static final long TIMEOUT_MS = 8_000L;

	private static final String DEX_ASSET = "fmpatch.dex";
	private static final String DEX_PATH = "/data/local/tmp/fmpatch.dex";

	private RootAudioRouter() {
	}

	/**
	 * Executes the given command as root.
	 *
	 * @param command shell command to run under `su`
	 * @return true when the command exited with 0
	 */
	public static boolean run(final String command) {
		return runWithStdin(command, null) == 0;
	}

	/**
	 * Runs a root command, optionally piping payload to its stdin after the command line.
	 *
	 * @return process exit code, or -1 on failure
	 */
	private static int runWithStdin(final String command, final String stdinPayload) {
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
				os.writeBytes(command + "\n");
				if (stdinPayload != null) {
					os.writeBytes(stdinPayload + "\n");
				}
				os.writeBytes("exit\n");
				os.flush();
			}

			final long deadline = System.currentTimeMillis() + TIMEOUT_MS;
			while (System.currentTimeMillis() < deadline) {
				try {
					final int rc = process.exitValue();
					reader.join(300L);
					return rc;
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
		return -1;
	}

	/**
	 * Deploys the bundled FmPatch dex to the device when missing or outdated.
	 * Idempotent; safe to call on every start.
	 *
	 * @return true when the dex on device matches the bundled asset
	 */
	public static boolean ensureDexDeployed(final Context context) {
		final byte[] asset;
		try (InputStream in = context.getAssets().open(DEX_ASSET)) {
			asset = readAll(in);
		} catch (final IOException e) {
			Log.e(TAG, "cannot read bundled " + DEX_ASSET, e);
			return false;
		}
		final String wantMd5 = md5Hex(asset);
		final String check = "[ \"$(md5sum " + DEX_PATH + " 2>/dev/null | cut -d' ' -f1)\" = " + wantMd5 + " ]";

		if (runWithStdin(check, null) == 0) {
			return true; // 设备上已是当前版本
		}

		final String b64 = android.util.Base64.encodeToString(asset, android.util.Base64.NO_WRAP);
		if (runWithStdin("base64 -d > " + DEX_PATH + ".tmp", b64) != 0
				|| runWithStdin("chmod 644 " + DEX_PATH + ".tmp && mv " + DEX_PATH + ".tmp " + DEX_PATH, null) != 0) {
			Log.e(TAG, "dex deploy failed");
			return false;
		}
		if (runWithStdin(check, null) != 0) {
			Log.e(TAG, "dex deploy verification failed");
			return false;
		}
		Log.i(TAG, "dex deployed to " + DEX_PATH);
		return true;
	}

	/**
	 * Switches the FM tuner loopback to the given output.
	 *
	 * @param device "speaker" (dual sink: speaker + wired, matching MIUI's FM
	 *               service — a speaker-only patch stays silent on this HAL) or
	 *               "wired" (auto-selects headphone / headset)
	 * @return true on success
	 */
	public static boolean routeFm(final String device) {
		return run("CLASSPATH=" + DEX_PATH + " app_process / FmPatch route " + device);
	}

	/**
	 * Releases any existing FM tuner patches.
	 *
	 * @return true on success
	 */
	public static boolean releaseFmPatches() {
		return run("CLASSPATH=" + DEX_PATH + " app_process / FmPatch release");
	}

	private static byte[] readAll(final InputStream in) throws IOException {
		final java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
		final byte[] buf = new byte[8192];
		int n;
		while ((n = in.read(buf)) != -1) {
			out.write(buf, 0, n);
		}
		return out.toByteArray();
	}

	private static String md5Hex(final byte[] data) {
		try {
			final byte[] digest = MessageDigest.getInstance("MD5").digest(data);
			final StringBuilder sb = new StringBuilder(digest.length * 2);
			for (final byte b : digest) {
				sb.append(Character.forDigit((b >> 4) & 0xf, 16));
				sb.append(Character.forDigit(b & 0xf, 16));
			}
			return sb.toString();
		} catch (final java.security.NoSuchAlgorithmException e) {
			throw new IllegalStateException(e);
		}
	}
}
