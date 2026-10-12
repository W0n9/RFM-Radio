package com.vlad805.fmradio.view;

import android.animation.ObjectAnimator;
import android.animation.ValueAnimator;
import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;

import com.vlad805.fmradio.R;

/**
 * The REC capsule of the C-air top bar: idle ghost pill (dot + "REC"),
 * active coral pill with a blinking dot and the running mm:ss.
 * The view is dumb; MainActivity owns state and the 1 s ticker.
 */
public class RecordCapsuleView extends LinearLayout {

	private View mDot;
	private TextView mLabel;
	private TextView mTime;
	@Nullable
	private ObjectAnimator mBlink;
	private boolean mRecording;

	public RecordCapsuleView(final Context context) {
		this(context, null);
	}

	public RecordCapsuleView(final Context context, @Nullable final AttributeSet attrs) {
		super(context, attrs);

		setOrientation(HORIZONTAL);
		setGravity(android.view.Gravity.CENTER_VERTICAL);
		final float d = getResources().getDisplayMetrics().density;
		setPadding((int) (14 * d), (int) (7 * d), (int) (14 * d), (int) (7 * d));

		LayoutInflater.from(context).inflate(R.layout.view_record_capsule, this, true);

		mDot = findViewById(R.id.rec_dot);
		mLabel = findViewById(R.id.rec_label);
		mTime = findViewById(R.id.rec_time);

		applyIdleStyle();
	}

	public void setRecording(final boolean recording) {
		if (mRecording == recording) {
			return;
		}
		mRecording = recording;

		if (recording) {
			setBackgroundResource(R.drawable.rec_capsule_bg_active);
			mDot.setBackgroundResource(R.drawable.rec_dot_active);
			mLabel.setTextColor(ContextCompat.getColor(getContext(), R.color.c_accent));
			mTime.setTextColor(ContextCompat.getColor(getContext(), R.color.c_accent));
			mTime.setVisibility(View.VISIBLE);
			startBlink();
		} else {
			applyIdleStyle();
		}
	}

	public void setDurationText(final CharSequence text) {
		mTime.setText(text);
	}

	public boolean isRecording() {
		return mRecording;
	}

	private void applyIdleStyle() {
		stopBlink();
		setBackgroundResource(R.drawable.rec_capsule_bg);
		mDot.setBackgroundResource(R.drawable.rec_dot);
		mDot.setAlpha(1f);
		mLabel.setTextColor(ContextCompat.getColor(getContext(), R.color.c_ink_dim));
		mTime.setVisibility(View.GONE);
	}

	private void startBlink() {
		stopBlink();
		mBlink = ObjectAnimator.ofFloat(mDot, View.ALPHA, 1f, 0.25f);
		mBlink.setDuration(1200);
		mBlink.setRepeatCount(ValueAnimator.INFINITE);
		mBlink.setRepeatMode(ValueAnimator.REVERSE);
		mBlink.start();
	}

	private void stopBlink() {
		if (mBlink != null) {
			mBlink.cancel();
			mBlink = null;
		}
	}

	@Override
	protected void onDetachedFromWindow() {
		stopBlink();
		super.onDetachedFromWindow();
	}
}
