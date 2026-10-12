package com.vlad805.fmradio.view;

import android.animation.AnimatorSet;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.SweepGradient;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;

import com.vlad805.fmradio.R;

/**
 * The colored "cover art" blob of the C-air redesign: a heavily blurred
 * conic-gradient circle that breathes while the tuner is playing and
 * extinguishes to a faint grayscale glow when stopped.
 *
 * Blur works on every API level via BlurMaskFilter on a software layer.
 * The layer rasterizes once; the breathing loop only animates layer
 * properties (scale/rotation/alpha), so onDraw never re-runs per frame.
 * Never call invalidate() from an animation callback.
 */
public class BlobView extends View {

	private static final long BREATH_HALF_MS = 2250;
	private static final float ALPHA_PLAYING = 0.75f;
	private static final float ALPHA_OFF = 0.18f;
	private static final float BREATH_SCALE = 1.09f;
	private static final float BREATH_ROTATION = 9f;
	/** The gradient is authored for a 210° start angle, matching the prototype. */
	private static final float GRADIENT_ROTATION_DEG = 210f;

	private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	@Nullable
	private AnimatorSet mBreath;
	private boolean mPlaying;

	public BlobView(final Context context) {
		this(context, null);
	}

	public BlobView(final Context context, @Nullable final AttributeSet attrs) {
		super(context, attrs);

		// BlurMaskFilter is ignored by the hardware pipeline; software layer is required.
		setLayerType(LAYER_TYPE_SOFTWARE, null);

		final float d = getResources().getDisplayMetrics().density;
		mPaint.setMaskFilter(new BlurMaskFilter(20f * d, BlurMaskFilter.Blur.NORMAL));

		// Born extinguished; the first real state arrives via setPlaying().
		setAlpha(ALPHA_OFF);
		setSaturation(0.2f);
	}

	@Override
	protected void onSizeChanged(final int w, final int h, final int oldw, final int oldh) {
		super.onSizeChanged(w, h, oldw, oldh);

		final SweepGradient gradient = new SweepGradient(w / 2f, h / 2f, new int[] {
				ContextCompat.getColor(getContext(), R.color.c_accent),
				ContextCompat.getColor(getContext(), R.color.c_blob_violet),
				ContextCompat.getColor(getContext(), R.color.c_blob_blue),
				ContextCompat.getColor(getContext(), R.color.c_blob_teal),
				ContextCompat.getColor(getContext(), R.color.c_accent),
		}, null);

		final Matrix rotate = new Matrix();
		rotate.setRotate(GRADIENT_ROTATION_DEG, w / 2f, h / 2f);
		gradient.setLocalMatrix(rotate);

		mPaint.setShader(gradient);
	}

	@Override
	protected void onDraw(@NonNull final Canvas canvas) {
		super.onDraw(canvas);
		// Blob occupies the central ~75% so the blur tail stays inside the view bounds.
		canvas.drawCircle(getWidth() / 2f, getHeight() / 2f, Math.min(getWidth(), getHeight()) * 0.375f, mPaint);
	}

	public void setPlaying(final boolean playing) {
		if (mPlaying == playing && mBreath != null) {
			return;
		}
		mPlaying = playing;

		if (mBreath != null) {
			mBreath.cancel();
			mBreath = null;
		}

		if (playing) {
			setSaturation(1f);
			animate().alpha(ALPHA_PLAYING).setDuration(400).start();

			final ObjectAnimator sx = ObjectAnimator.ofFloat(this, SCALE_X, 1f, BREATH_SCALE);
			final ObjectAnimator sy = ObjectAnimator.ofFloat(this, SCALE_Y, 1f, BREATH_SCALE);
			final ObjectAnimator rot = ObjectAnimator.ofFloat(this, ROTATION, 0f, BREATH_ROTATION);
			for (final ObjectAnimator a : new ObjectAnimator[] {sx, sy, rot}) {
				a.setDuration(BREATH_HALF_MS);
				a.setRepeatCount(ObjectAnimator.INFINITE);
				a.setRepeatMode(ObjectAnimator.REVERSE);
			}
			mBreath = new AnimatorSet();
			mBreath.playTogether(sx, sy, rot);
			mBreath.start();
		} else {
			animate().alpha(ALPHA_OFF).scaleX(1f).scaleY(1f).rotation(0f).setDuration(400).start();
			setSaturation(0.2f);
		}
	}

	/** One-shot paint mutation; safe because the software layer re-rasterizes only on invalidate(). */
	private void setSaturation(final float saturation) {
		final ColorMatrix cm = new ColorMatrix();
		cm.setSaturation(saturation);
		mPaint.setColorFilter(saturation >= 1f ? null : new ColorMatrixColorFilter(cm));
		invalidate();
	}

	@Override
	protected void onDetachedFromWindow() {
		if (mBreath != null) {
			mBreath.cancel();
			mBreath = null;
		}
		super.onDetachedFromWindow();
	}
}
