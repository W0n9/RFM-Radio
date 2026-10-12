package com.vlad805.fmradio.helper;

import android.app.Activity;
import android.graphics.Color;
import android.os.Build;
import android.view.View;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

/**
 * Draws each activity edge to edge and consumes the system bar insets as root padding,
 * so the window background fills the bars and the content stays below them on every
 * API level (mandatory from targetSdk 35, adopted earlier to keep one code path).
 */
public final class EdgeToEdgeHelper {
    private EdgeToEdgeHelper() {}

    public static void apply(final Activity activity) {
        final View root = activity.findViewById(android.R.id.content);

        WindowCompat.setDecorFitsSystemWindows(activity.getWindow(), false);
        activity.getWindow().setStatusBarColor(Color.TRANSPARENT);
        activity.getWindow().setNavigationBarColor(Color.TRANSPARENT);

        final WindowInsetsControllerCompat controller =
                new WindowInsetsControllerCompat(activity.getWindow(), root);
        controller.setAppearanceLightStatusBars(false);
        controller.setAppearanceLightNavigationBars(false);

        ViewCompat.setOnApplyWindowInsetsListener(root, (v, insets) -> {
            final Insets bars = insets.getInsets(
                    WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return WindowInsetsCompat.CONSUMED;
        });

        // A listener set after layout already ran receives no dispatch until the next one;
        // force a re-dispatch so the padding applies on first frame.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT_WATCH) {
            root.requestApplyInsets();
        }
    }
}
