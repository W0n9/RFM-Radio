package com.vlad805.fmradio.view;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;

import com.vlad805.fmradio.R;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Horizontal tuning slider of the C-air redesign. Replaces FrequencyBarView
 * (the vertical ruler) and deliberately mirrors its contract so that
 * MainActivity wiring is a drop-in swap.
 *
 * Track with a gradient fill up to the thumb, favorite stations as dots on
 * the track, drag anywhere to tune, tap to jump, dots snap exactly.
 */
public class FrequencySliderView extends View {

	/** Same shape as the old FrequencyBarView listener. */
	public interface OnFrequencyChangedListener {
		void onChanged(int kHz);
	}

	private static final long CHANGE_THRESHOLD_MS = 400;

	private final Paint mTrackPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mDotPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mDotActivePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mDotHaloPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mThumbPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mThumbShadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

	private int mMin = 87500;
	private int mMax = 108000;
	private int mSpacing = 100;
	private int mValue = 87500;

	private Set<Integer> mFavorites = Collections.emptySet();

	@Nullable
	private OnFrequencyChangedListener mListener;

	private boolean mDragging;
	private long mLastFiredAt;
	private int mLastFiredValue = Integer.MIN_VALUE;

	private float mTrackY;
	private float mTrackStartX;
	private float mTrackEndX;
	private float mTrackStroke;
	private float mDotRadius;
	private float mDotHitRadius;
	private float mThumbRadius;
	private float mDensity;

	public FrequencySliderView(final Context context) {
		this(context, null);
	}

	public FrequencySliderView(final Context context, @Nullable final AttributeSet attrs) {
		super(context, attrs);

		mTrackPaint.setColor(ContextCompat.getColor(context, R.color.c_outline_soft));
		mTrackPaint.setStyle(Paint.Style.STROKE);
		mTrackPaint.setStrokeCap(Paint.Cap.ROUND);

		mFillPaint.setStyle(Paint.Style.STROKE);
		mFillPaint.setStrokeCap(Paint.Cap.ROUND);

		mDotPaint.setColor(ContextCompat.getColor(context, R.color.c_ink_faint));
		mDotActivePaint.setColor(ContextCompat.getColor(context, R.color.c_accent));
		mDotHaloPaint.setColor(ContextCompat.getColor(context, R.color.c_accent_bg));
		mThumbPaint.setColor(ContextCompat.getColor(context, R.color.c_ink));
		mThumbShadowPaint.setColor(0x66000000);

		final float d = getResources().getDisplayMetrics().density;
		mDensity = d;
		mTrackStroke = 3f * d;
		mTrackPaint.setStrokeWidth(mTrackStroke);
		mFillPaint.setStrokeWidth(mTrackStroke);
		mDotRadius = 3.5f * d;
		mDotHitRadius = 12f * d;
		mThumbRadius = 8.5f * d;
	}

	public void setOnFrequencyChangeListener(@Nullable final OnFrequencyChangedListener listener) {
		mListener = listener;
	}

	/** Same semantics as the old ruler: values in kHz. */
	public void setMinMaxValue(final int minValue, final int maxValue, final int spacing) {
		mMin = minValue;
		mMax = maxValue;
		mSpacing = Math.max(1, spacing);
		mValue = snapToSpacing(mValue, mMin, mMax, mSpacing);
		invalidate();
	}

	public void setFrequency(final int kHz) {
		if (mDragging) {
			return;
		}
		mValue = snapToSpacing(kHz, mMin, mMax, mSpacing);
		invalidate();
	}

	public void setFavoriteFrequencies(@Nullable final Set<Integer> frequencies) {
		mFavorites = frequencies == null ? Collections.emptySet() : new HashSet<>(frequencies);
		invalidate();
	}

	static int snapToSpacing(final int kHz, final int min, final int max, final int spacing) {
		final int snapped = min + Math.round((kHz - min) / (float) spacing) * spacing;
		return Math.max(min, Math.min(max, snapped));
	}

	@Override
	protected void onSizeChanged(final int w, final int h, final int oldw, final int oldh) {
		super.onSizeChanged(w, h, oldw, oldh);

		mTrackY = h / 2f;
		final float inset = mThumbRadius + 2 * mDensity;
		mTrackStartX = inset;
		mTrackEndX = w - inset;

		mFillPaint.setShader(new LinearGradient(
				mTrackStartX, 0, mTrackEndX, 0,
				ContextCompat.getColor(getContext(), R.color.c_accent),
				ContextCompat.getColor(getContext(), R.color.c_accent_soft),
				Shader.TileMode.CLAMP
		));
	}

	private float xFor(final int kHz) {
		final float range = mMax - mMin;
		if (range <= 0 || mTrackEndX <= mTrackStartX) {
			return mTrackStartX;
		}
		return mTrackStartX + (kHz - mMin) / range * (mTrackEndX - mTrackStartX);
	}

	@Override
	protected void onDraw(@NonNull final Canvas canvas) {
		super.onDraw(canvas);

		final float thumbX = xFor(mValue);

		canvas.drawLine(mTrackStartX, mTrackY, mTrackEndX, mTrackY, mTrackPaint);
		canvas.drawLine(mTrackStartX, mTrackY, thumbX, mTrackY, mFillPaint);

		for (final int favorite : mFavorites) {
			if (favorite < mMin || favorite > mMax) {
				continue;
			}
			final float x = xFor(favorite);
			if (favorite == mValue) {
				canvas.drawCircle(x, mTrackY, mDotRadius * 2f, mDotHaloPaint);
				canvas.drawCircle(x, mTrackY, mDotRadius, mDotActivePaint);
			} else {
				canvas.drawCircle(x, mTrackY, mDotRadius, mDotPaint);
			}
		}

		canvas.drawCircle(thumbX, mTrackY + 1.5f * mDensity, mThumbRadius, mThumbShadowPaint);
		canvas.drawCircle(thumbX, mTrackY, mThumbRadius, mThumbPaint);
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(@NonNull final MotionEvent event) {
		if (!isEnabled()) {
			return false;
		}

		switch (event.getActionMasked()) {
			case MotionEvent.ACTION_DOWN: {
				getParent().requestDisallowInterceptTouchEvent(true);
				mDragging = true;
				setValueFromTouch(event.getX());
				fireThrottled(true);
				return true;
			}

			case MotionEvent.ACTION_MOVE: {
				if (mDragging) {
					setValueFromTouch(event.getX());
					fireThrottled(false);
				}
				return true;
			}

			case MotionEvent.ACTION_UP: {
				if (mDragging) {
					mDragging = false;
					setValueFromTouch(event.getX());
					fireFinal();
				}
				getParent().requestDisallowInterceptTouchEvent(false);
				return true;
			}

			case MotionEvent.ACTION_CANCEL: {
				mDragging = false;
				getParent().requestDisallowInterceptTouchEvent(false);
				return true;
			}
		}

		return super.onTouchEvent(event);
	}

	private void setValueFromTouch(final float x) {
		// Favorite dots win inside their hit radius: land exactly on the station.
		for (final int favorite : mFavorites) {
			if (Math.abs(x - xFor(favorite)) <= mDotHitRadius) {
				mValue = favorite;
				invalidate();
				return;
			}
		}

		final float usable = mTrackEndX - mTrackStartX;
		final float ratio = usable <= 0 ? 0f : (x - mTrackStartX) / usable;
		mValue = snapToSpacing(mMin + Math.round(ratio * (mMax - mMin)), mMin, mMax, mSpacing);
		invalidate();
	}

	private void fireThrottled(final boolean force) {
		final long now = System.currentTimeMillis();
		if (!force && now - mLastFiredAt < CHANGE_THRESHOLD_MS) {
			return;
		}
		fire();
	}

	private void fireFinal() {
		fire();
	}

	private void fire() {
		if (mListener == null || mValue == mLastFiredValue) {
			return;
		}
		mLastFiredValue = mValue;
		mLastFiredAt = System.currentTimeMillis();
		mListener.onChanged(mValue);
	}

	@Override
	public void onInitializeAccessibilityNodeInfo(@NonNull final AccessibilityNodeInfo info) {
		super.onInitializeAccessibilityNodeInfo(info);
		info.setClassName("android.widget.SeekBar");
		// RangeInfo.obtain is deprecated at compileSdk 36, but its replacement
		// constructor needs API 30 while our minSdk is 23.
		@SuppressWarnings("deprecation")
		final AccessibilityNodeInfo.RangeInfo range = AccessibilityNodeInfo.RangeInfo.obtain(
				AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_INT, mMin, mMax, mValue);
		info.setRangeInfo(range);
		info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
		info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
	}

	@Override
	public boolean performAccessibilityAction(final int action, final android.os.Bundle arguments) {
		if (action == AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD.getId()
				|| action == AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD.getId()) {
			final int direction = action == AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD.getId() ? 1 : -1;
			mValue = snapToSpacing(mValue + direction * mSpacing, mMin, mMax, mSpacing);
			invalidate();
			fireFinal();
			return true;
		}
		return super.performAccessibilityAction(action, arguments);
	}
}
