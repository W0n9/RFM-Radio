package com.vlad805.fmradio.activity;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.View;
import android.widget.ImageButton;
import android.widget.TextView;

import androidx.annotation.IdRes;
import androidx.annotation.StringRes;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.PopupMenu;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.vlad805.fmradio.C;
import com.vlad805.fmradio.R;
import com.vlad805.fmradio.Storage;
import com.vlad805.fmradio.Utils;
import com.vlad805.fmradio.UtilsLocalization;
import com.vlad805.fmradio.controller.FavoriteController;
import com.vlad805.fmradio.controller.RadioController;
import com.vlad805.fmradio.controller.RadioState;
import com.vlad805.fmradio.controller.RadioStateUpdater;
import com.vlad805.fmradio.controller.TunerStatus;
import com.vlad805.fmradio.enums.Direction;
import com.vlad805.fmradio.enums.PowerMode;
import com.vlad805.fmradio.enums.TunerDriver;
import com.vlad805.fmradio.helper.ProgressDialog;
import com.vlad805.fmradio.helper.Toast;
import com.vlad805.fmradio.helper.TunerDriverDetector;
import com.vlad805.fmradio.models.FavoriteStation;
import com.vlad805.fmradio.preferences.BandUtils;
import com.vlad805.fmradio.service.FMService;
import com.vlad805.fmradio.view.BlobView;
import com.vlad805.fmradio.view.FavoritesView;
import com.vlad805.fmradio.view.FrequencySliderView;
import com.vlad805.fmradio.view.RecordCapsuleView;
import com.vlad805.fmradio.view.SignalStrengthView;

import net.grandcentrix.tray.AppPreferences;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.vlad805.fmradio.Utils.alert;
import static com.vlad805.fmradio.Utils.getTimeStringBySeconds;

@SuppressLint("NonConstantResourceId")
public class MainActivity extends AppCompatActivity implements View.OnClickListener, RadioStateUpdater.TunerStateListener {
    private Toast mToast;
    private FrequencySliderView mSlider;
    private FavoritesView mFavoritesView;
    private BlobView mBlob;
    private RecordCapsuleView mRecCapsule;

    private RadioController mRadioController;
    private FavoriteController mFavoriteController;
    private ProgressDialog mSearchProgress;

    private AppPreferences mPreferences;

    private ImageButton mCtlToggle;

    private TextView mFrequencyText;
    private TextView mPs;
    private TextView mRt;
    private TextView mPty;
    private TextView mPi;
    private TextView mViewStereoMode;
    private TextView mBandMinLabel;
    private TextView mBandMaxLabel;
    private SignalStrengthView mSignalStrength;

    /** Frequency formatting precision, derived from the spacing preference. */
    private int mFrequencyPrecision = 1;
    private int mRegion;

    private boolean mStatusShown;
    private String mLastPsText = "";

    private RadioState mLastState;

    private final Handler mRecHandler = new Handler(Looper.getMainLooper());
    private final Runnable mRecTicker = new Runnable() {
        @Override
        public void run() {
            if (mLastState != null && mLastState.isRecording()) {
                mRecCapsule.setDurationText(getTimeStringBySeconds(mLastState.getRecordingDuration()));
                mRecHandler.postDelayed(this, 1000);
            }
        }
    };

    private static final int REQUEST_CODE_FAVORITES_OPENED = 1048;
    private static final int REQUEST_CODE_SETTINGS_CHANGED = 1050;
    private static final int REQUEST_CODE_RECORD_PERMISSIONS = 1051;
    private static final int REQUEST_CODE_PLAYBACK_PERMISSION = 1052;
    private static final int REQUEST_CODE_NOTIFICATION_PERMISSION = 1053;
    private boolean mPendingRecordStart = false;
    private boolean mRecordPermissionsRequested = false;
    private boolean mPendingPlaybackStart = false;
    private boolean mPlaybackPermissionRequested = false;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (!Utils.isRootEnvironmentAvailable()) {
            startActivity(StartupBlockedActivity.forMissingRoot(this)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            finish();
            return;
        }

        if (!TunerDriverDetector.isDeviceSupported()) {
            startActivity(StartupBlockedActivity.forUnsupportedDevice(this)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            finish();
            return;
        }

        setContentView(R.layout.activity_main);

        mToast = Toast.create(this);

        mPreferences = new AppPreferences(this);
        mRadioController = new RadioController(this);
        mFavoriteController = new FavoriteController(this);
        mRadioController.requestForCurrentState(this);
        mRadioController.registerForUpdates(this);
        mRadioController.setPowerMode(PowerMode.NORMAL);

        initUserInterface();
        initLogic();
    }

    /**
     * Activity handle, UI setup
     */
    private void initUserInterface() {
        mBlob = findViewById(R.id.blob);
        mRecCapsule = findViewById(R.id.rec_capsule);
        mFrequencyText = findViewById(R.id.frequency_text);
        mPs = findViewById(R.id.ps_text);
        mRt = findViewById(R.id.rt_text);
        mPty = findViewById(R.id.pty_chip);
        mPi = findViewById(R.id.pi_text);
        mBandMinLabel = findViewById(R.id.band_min_label);
        mBandMaxLabel = findViewById(R.id.band_max_label);

        // Marquee scrolls only while the TextView is selected.
        mRt.setSelected(true);

        mFavoritesView = findViewById(R.id.favorite_list);
        mFavoritesView.setOnFavoritesChangedListener(() -> {
            updateFavoriteFrequencyMarkers();
        });
        mFavoritesView.setOnErrorListener(message -> {
            mToast.text(message).show();
        });

        mSlider = findViewById(R.id.frequency_slider);

        final int kHz = Storage.getInstance(this).getInt(C.PrefKey.LAST_FREQUENCY, C.PrefDefaultValue.LAST_FREQUENCY);
        setFrequencyText(kHz);
        mSlider.setFrequency(kHz);
        mFavoritesView.setActiveFrequency(kHz);
        mFavoritesView.reload(false);
        updateFavoriteFrequencyMarkers();

        initClickableButtons();
    }

    /**
     * Initialize connection and service
     */
    private void initLogic() {
        final boolean needStartup = Storage.getPrefBoolean(this, C.PrefKey.APP_AUTO_STARTUP, C.PrefDefaultValue.APP_AUTO_STARTUP);

        if (needStartup) {
            if (ensurePlaybackPermissionForSoftwareAudio()) {
                startRadio();
            }
        } else {
            setEnabledUi(false);
            showStatus(R.string.status_tuner_stopped);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();

        ensureNotificationPermission();
        reloadPreferences();
        mFavoritesView.reload(false);
    }

    @Override
    protected void onDestroy() {
        mRecHandler.removeCallbacks(mRecTicker);

        if (mRadioController != null) {
            mRadioController.unregisterForUpdates();

            if (Storage.getPrefBoolean(this, C.PrefKey.TUNER_POWER_MODE, false)) {
                mRadioController.setPowerMode(PowerMode.LOW);
            }
        }

        super.onDestroy();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        switch (requestCode) {
            case REQUEST_CODE_FAVORITES_OPENED: {
                if (resultCode == Activity.RESULT_OK && data.getBooleanExtra("changed", false)) {
                    mFavoritesView.reload(true);
                    updateFavoriteFrequencyMarkers();
                }
                break;
            }

            case REQUEST_CODE_SETTINGS_CHANGED: {
                if (resultCode == RESULT_OK && data.getBooleanExtra("changed", false)) {
                    alert(
                            this,
                            R.string.main_warning_pref_changed_title,
                            R.string.main_warning_pref_changed_content,
                            android.R.string.ok
                    );
                }
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void onRequestPermissionsResult(final int requestCode, final String[] permissions, final int[] grantResults) {
        if (requestCode == REQUEST_CODE_RECORD_PERMISSIONS) {
            final boolean granted = hasRecordingPermissions();
            if (granted && mPendingRecordStart) {
                mRadioController.record(true);
            } else if (mPendingRecordStart) {
                mToast.text(getString(R.string.record_permissions_required)).show();
            }
            mPendingRecordStart = false;
        }

        if (requestCode == REQUEST_CODE_PLAYBACK_PERMISSION) {
            final boolean granted = hasPlaybackPermission();
            if (granted && mPendingPlaybackStart) {
                startRadio();
            } else if (mPendingPlaybackStart) {
                mToast.text(getString(R.string.playback_permission_required)).show();
            }
            mPendingPlaybackStart = false;
        }

        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    /**
     * Setup clickable buttons
     */
    private void initClickableButtons() {
        mCtlToggle = findViewById(R.id.ctl_toggle);
        mViewStereoMode = findViewById(R.id.stereo_mono);
        mSignalStrength = findViewById(R.id.signal_strength);

        final int[] ids = {
                R.id.ctl_toggle,
                R.id.ctl_go_down,
                R.id.ctl_go_up,
                R.id.ctl_seek_down,
                R.id.ctl_seek_up,
                R.id.settings_button,
                R.id.overflow_button,
                R.id.rec_capsule
        };

        for (int id : ids) {
            findViewById(id).setOnClickListener(this);
        }
    }

    @Override
    public void onClick(final View view) {
        int id = view.getId();

        if (id == R.id.ctl_toggle) {
            // Main play/stop button
            switch (mRadioController.getState().getStatus()) {
                case IDLE: {
                    if (ensurePlaybackPermissionForSoftwareAudio()) {
                        startRadio();
                    }
                    break;
                }

                case ENABLED: {
                    setEnabledToggleButton(false);
                    mRadioController.disable();
                    break;
                }
            }
        } else if (id == R.id.ctl_go_down) {
            mRadioController.jump(Direction.DOWN);
        } else if (id == R.id.ctl_go_up) {
            mRadioController.jump(Direction.UP);
        } else if (id == R.id.ctl_seek_down) {
            mRadioController.hwSeek(Direction.DOWN);
            showStatus(R.string.status_searching_station);
        } else if (id == R.id.ctl_seek_up) {
            mRadioController.hwSeek(Direction.UP);
            showStatus(R.string.status_searching_station);
        } else if (id == R.id.settings_button) {
            startActivityForResult(new Intent(this, SettingsActivity.class), REQUEST_CODE_SETTINGS_CHANGED);
        } else if (id == R.id.overflow_button) {
            showOverflowMenu();
        } else if (id == R.id.rec_capsule) {
            toggleRecording();
        }
    }

    private void startRadio() {
        setEnabledToggleButton(false);
        mRadioController.setup();
    }

    /**
     * The C-air top bar has no app bar, so the former toolbar menu becomes a
     * popup anchored at the overflow button: scan / speaker / about.
     */
    private void showOverflowMenu() {
        final PopupMenu popup = new PopupMenu(this, findViewById(R.id.overflow_button));
        popup.getMenu().add(0, 1, 0, R.string.menu_scan_stations);
        popup.getMenu().add(0, 2, 1, R.string.menu_speaker).setCheckable(true);
        popup.getMenu().add(0, 3, 2, R.string.menu_about);

        final boolean tunerEnabled = mLastState != null && TunerStatus.ENABLED.equals(mLastState.getStatus());
        popup.getMenu().findItem(1).setEnabled(tunerEnabled);
        popup.getMenu().findItem(2).setEnabled(tunerEnabled);
        if (mLastState != null) {
            popup.getMenu().findItem(2).setChecked(mLastState.isForceSpeaker());
        }

        popup.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case 1:
                    showSearchDialog();
                    return true;
                case 2:
                    startService(new Intent(this, FMService.class).setAction(C.Command.SPEAKER_STATE));
                    return true;
                case 3:
                    startActivity(new Intent(this, AboutActivity.class));
                    return true;
                default:
                    return false;
            }
        });
        popup.show();
    }

    private void toggleRecording() {
        if (mRadioController.getState().isRecording()) {
            mRadioController.record(false);
            return;
        }

        if (hasRecordingPermissions()) {
            mRadioController.record(true);
        } else if (shouldOpenRecordingPermissionSettings()) {
            showRecordingPermissionSettingsDialog();
        } else {
            mPendingRecordStart = true;
            mRecordPermissionsRequested = true;

            // Android 10+ writes recordings via MediaStore into Music/RFM-Recordings,
            // so only microphone access is needed. Older releases still use direct File I/O.
            final String[] permissions = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                    ? new String[]{
                            Manifest.permission.RECORD_AUDIO
                    }
                    : new String[]{
                            Manifest.permission.RECORD_AUDIO,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                    };

            ActivityCompat.requestPermissions(this, permissions, REQUEST_CODE_RECORD_PERMISSIONS);
        }
    }

    private void showSearchDialog() {
        mRadioController.getCurrentState(state -> {
            if (state.getStatus() != TunerStatus.ENABLED) {
                mToast.text(R.string.scan_stations_error_tuner_not_enabled).show();
                return;
            }

            new androidx.appcompat.app.AlertDialog.Builder(this)
                    .setTitle(R.string.scan_stations_confirm_title)
                    .setMessage(R.string.scan_stations_confirm_message)
                    .setCancelable(false)
                    .setPositiveButton(R.string.scan_stations_confirm_continue, (dialog, buttonId) -> startSearch())
                    .setNegativeButton(R.string.scan_stations_confirm_discard, null)
                    .setIcon(android.R.drawable.ic_dialog_alert)
                    .show();
        });
    }

    private void startSearch() {
        mRadioController.hwSearch();

        mSearchProgress = ProgressDialog.create(this)
                .text(R.string.scan_stations_progress)
                .negativeButton(android.R.string.cancel, (dialog, buttonId) -> {
                    mRadioController.cancelHwSearch();
                    unregisterSearchReceiver();
                    mSearchProgress = null;
                })
                .show();

        Utils.registerAppReceiver(this, mSearchDone, new IntentFilter(C.Event.HW_SEARCH_DONE));
    }

    private void unregisterSearchReceiver() {
        try {
            unregisterReceiver(mSearchDone);
        } catch (IllegalArgumentException ignored) {
        }
    }

    private final BroadcastReceiver mSearchDone = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            if (intent == null || intent.getAction() == null) {
                return;
            }

            unregisterSearchReceiver();

            final int[] frequencies = intent.getIntArrayExtra(C.Key.STATION_LIST);
            mFavoriteController.reload();
            final List<FavoriteStation> stations = mFavoriteController.getStationsInCurrentList();
            stations.clear();

            for (final int kHz : frequencies) {
                stations.add(new FavoriteStation(kHz, ""));
            }

            mFavoriteController.save();
            Utils.sendAppBroadcast(MainActivity.this, new Intent(C.Event.FAVORITE_LIST_CHANGED));
            mFavoritesView.reload(true);
            updateFavoriteFrequencyMarkers();

            if (mSearchProgress != null) {
                mSearchProgress.hide();
                mSearchProgress = null;
            }
        }
    };

    private void setEnabledToggleButton(final boolean enabled) {
        mCtlToggle.setEnabled(enabled);
        mCtlToggle.setAlpha(enabled ? 1f : .5f);
    }

    private boolean hasRecordingPermissions() {
        final boolean hasRecordAudio =
                ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return hasRecordAudio;
        }

        return hasRecordAudio
                && ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean hasPlaybackPermission() {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean needsPlaybackPermissionForSoftwareAudio() {
        // Only legacy (Android 7-) bridges PCM in app space and therefore
        // needs RECORD_AUDIO for playback. MTK plays through the hostless HAL
        // loopback; Qualcomm routes in hardware.
        return (
            Build.VERSION.SDK_INT < Build.VERSION_CODES.O &&
            TunerDriverDetector.getTunerDriver() == TunerDriver.LEGACY
        );
    }

    private boolean ensurePlaybackPermissionForSoftwareAudio() {
        if (!needsPlaybackPermissionForSoftwareAudio() || hasPlaybackPermission()) {
            return true;
        }

        if (shouldOpenPlaybackPermissionSettings()) {
            showPlaybackPermissionSettingsDialog();
        } else {
            mPendingPlaybackStart = true;
            mPlaybackPermissionRequested = true;
            ActivityCompat.requestPermissions(
                    this,
                    new String[] { Manifest.permission.RECORD_AUDIO },
                    REQUEST_CODE_PLAYBACK_PERMISSION
            );
        }

        return false;
    }

    /**
     * Android 13 mutes all notification channels of new installs until POST_NOTIFICATIONS is
     * granted. The FM service keeps running either way, so a refusal just hides the ongoing
     * media notification — ask once per resume where it is missing, without nagging.
     */
    private void ensureNotificationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return;
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
            return;
        }

        ActivityCompat.requestPermissions(
                this,
                new String[] { Manifest.permission.POST_NOTIFICATIONS },
                REQUEST_CODE_NOTIFICATION_PERMISSION
        );
    }

    private boolean shouldOpenRecordingPermissionSettings() {
        if (!mRecordPermissionsRequested) {
            return false;
        }

        // Android 10+ saves recordings through MediaStore, so only RECORD_AUDIO can be denied.
        // Older versions still write directly to shared storage and may also require storage access.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return !ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.RECORD_AUDIO);
        }

        return !ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.RECORD_AUDIO)
                && !ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.WRITE_EXTERNAL_STORAGE);
    }

    private void showRecordingPermissionSettingsDialog() {
        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.record_permissions_required_title)
                .setMessage(R.string.record_permissions_required)
                .setPositiveButton(R.string.record_permissions_open_settings, (DialogInterface dialog, int which) -> openAppSettings())
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private boolean shouldOpenPlaybackPermissionSettings() {
        if (!mPlaybackPermissionRequested) {
            return false;
        }
        return !ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.RECORD_AUDIO);
    }

    private void showPlaybackPermissionSettingsDialog() {
        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.playback_permission_required_title)
                .setMessage(R.string.playback_permission_required)
                .setPositiveButton(R.string.record_permissions_open_settings, (DialogInterface dialog, int which) -> openAppSettings())
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void openAppSettings() {
        final Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                .setData(Uri.fromParts("package", getPackageName(), null));
        startActivity(intent);
    }

    private void reloadPreferences() {
        final int regionPref = mPreferences.getInt(C.PrefKey.TUNER_REGION, C.PrefDefaultValue.TUNER_REGION);
        final int spacingPref = mPreferences.getInt(C.PrefKey.TUNER_SPACING, C.PrefDefaultValue.TUNER_SPACING);

        mRegion = regionPref;
        mFrequencyPrecision = spacingPref == BandUtils.SPACING_50kHz ? 2 : 1;

        final BandUtils.BandLimit bandLimit = BandUtils.getBandLimit(regionPref);
        final int spacing = BandUtils.getSpacing(spacingPref);

        mSlider.setMinMaxValue(bandLimit.lower, bandLimit.upper, spacing);
        mSlider.setOnFrequencyChangeListener(mOnFrequencyChanged);

        mBandMinLabel.setText(Utils.getMHz(bandLimit.lower, mFrequencyPrecision).trim());
        mBandMaxLabel.setText(Utils.getMHz(bandLimit.upper, mFrequencyPrecision).trim());

        updateFavoriteFrequencyMarkers();
    }

    private void updateFavoriteFrequencyMarkers() {
        if (mFavoritesView == null || mSlider == null) {
            return;
        }

        final Set<Integer> frequencies = new HashSet<>();
        for (final FavoriteStation station : mFavoritesView.getStationsInCurrentList()) {
            frequencies.add(station.getFrequency());
        }
        mSlider.setFavoriteFrequencies(frequencies);
    }

    private final FrequencySliderView.OnFrequencyChangedListener mOnFrequencyChanged = new FrequencySliderView.OnFrequencyChangedListener() {
        @Override
        public void onChanged(final int kHz) {
            if (mRadioController.getState().getFrequency() == kHz) {
                return;
            }

            mRadioController.setFrequency(kHz);
        }
    };

    /**
     * Called when the state of the tuner changes
     * @param state State object
     * @param mode Bitmask what was changed
     */
    @Override
    public void onStateUpdated(final RadioState state, final int mode) {
        mLastState = state;

        if ((mode & RadioStateUpdater.SET_RMSSI) > 0) {
            final boolean available = state.getRmssi() != RadioState.RMSSI_UNKNOWN;
            if (available) {
                mSignalStrength.setRmssi(state.getRmssi());
            } else {
                mSignalStrength.clear();
            }
        }

        if ((mode & RadioStateUpdater.SET_STATUS) > 0) {
            handleChangingState(state.getStatus());
            if ((mode & RadioStateUpdater.SET_INITIAL) == 0 || state.getStatus() != TunerStatus.ENABLED) {
                return;
            }
        }

        if ((mode & RadioStateUpdater.SET_FREQUENCY) > 0) {
            hideStatus();

            setFrequencyText(state.getFrequency());

            mSlider.setFrequency(state.getFrequency());
            mFavoritesView.setActiveFrequency(state.getFrequency());

            updateNowPlaying(state);
        }

        if (
            (mode & RadioStateUpdater.SET_PS) > 0 ||
            (mode & RadioStateUpdater.SET_PI) > 0 ||
            (mode & RadioStateUpdater.SET_RT) > 0 ||
            (mode & RadioStateUpdater.SET_PTY) > 0 ||
            (mode & RadioStateUpdater.SET_COUNTRY) > 0
        ) {
            updateNowPlaying(state);
        }

        if ((mode & RadioStateUpdater.SET_STEREO) > 0) {
            mViewStereoMode.setText(state.isStereo()
                    ? R.string.stereo_mode_label
                    : R.string.mono_mode_label
            );
            mViewStereoMode.setContentDescription(getString(state.isStereo()
                    ? R.string.stereo_mode_active
                    : R.string.mono_mode_active
            ));
        }

        if ((mode & RadioStateUpdater.SET_RECORDING) > 0 || (mode & RadioStateUpdater.SET_INITIAL) > 0) {
            updateRecordingUi(state);
        }

        // SET_SPEAKER needs no live widget: the overflow popup reads mLastState at open.
    }

    private void handleChangingState(final TunerStatus status) {
        mBlob.setPlaying(status == TunerStatus.ENABLED);

        switch (status) {
            case IDLE: {
                setEnabledUi(false);
                clearMetadata();
                showStatus(R.string.status_tuner_stopped);
                setPlayingMainPlayButton(false);
                setEnabledToggleButton(true);
                break;
            }

            case INSTALLING: {
                showStatus(R.string.status_installing);
                setEnabledToggleButton(false);
                break;
            }

            case INSTALLED:
            case LAUNCHED: {
                hideStatus();
                setEnabledToggleButton(false);
                break;
            }

            case LAUNCHING: {
                showStatus(R.string.status_launching);
                setEnabledToggleButton(false);
                break;
            }

            case ROOT_ACCESS_DENIED: {
                clearMetadata();
                showStatus(R.string.status_tuner_stopped);
                setEnabledToggleButton(false);

                Utils.alert(
                        this,
                        R.string.root_required_title,
                        R.string.root_access_denied_message,
                        android.R.string.ok
                );
                break;
            }

            case FATAL_ERROR: {
                clearMetadata();
                showStatus(R.string.status_tuner_stopped);
                setEnabledToggleButton(false);

                Utils.alert(
                        this,
                        R.string.main_error_launching_title,
                        R.string.main_error_launching,
                        android.R.string.ok
                );
                break;
            }

            case ENABLING: {
                hideStatus();
                setEnabledToggleButton(false);
                break;
            }

            case DISABLING: {
                setEnabledToggleButton(false);
                break;
            }

            case ENABLED: {
                setEnabledUi(true);
                hideStatus();
                setEnabledToggleButton(true);
                setPlayingMainPlayButton(true);
                break;
            }
        }
    }

    private void setPlayingMainPlayButton(final boolean isPlay) {
        mCtlToggle.setImageResource(isPlay ? R.drawable.ic_stop : R.drawable.ic_play);
    }

    /**
     * Enables or disables the entire interface
     * @param state True if enabled, false otherwise
     */
    private void setEnabledUi(final boolean state) {
        final @IdRes int[] ids = {
                R.id.ctl_go_down,
                R.id.ctl_go_up,
                R.id.ctl_seek_down,
                R.id.ctl_seek_up,
                R.id.frequency_slider,
                R.id.freq_overlay,
                R.id.now_playing,
                R.id.rec_capsule,
                R.id.favorite_list
        };

        for (final int id : ids) {
            final View v = findViewById(id);
            v.setAlpha(state ? 1f : .5f);
            v.setEnabled(state);
        }
    }

    private void updateRecordingUi(final RadioState state) {
        final boolean isRecording = state.isRecording();
        mRecCapsule.setRecording(isRecording);

        mRecHandler.removeCallbacks(mRecTicker);
        if (isRecording) {
            mRecCapsule.setDurationText(getTimeStringBySeconds(state.getRecordingDuration()));
            mRecHandler.postDelayed(mRecTicker, 1000);
        }
    }

    /* ===================== Now-playing helpers (absorbed from RadioUIView) ===================== */

    private void setFrequencyText(final int kHz) {
        mFrequencyText.setText(Utils.getMHz(kHz, mFrequencyPrecision).trim());
    }

    private void updateNowPlaying(final RadioState state) {
        final String ps = state.getPs() != null ? state.getPs() : "";
        mLastPsText = ps.isEmpty()
                ? Utils.getMHz(state.getFrequency(), mFrequencyPrecision).trim()
                : ps;
        if (!mStatusShown) {
            mPs.setText(mLastPsText);
        }

        mRt.setText(state.getRt() != null ? state.getRt() : "");

        final String pty = UtilsLocalization.getProgramType(state.getPty(), mRegion);
        mPty.setText(pty);
        mPty.setVisibility(pty.isEmpty() ? View.GONE : View.VISIBLE);

        final String pi = state.getPi() != null ? state.getPi() : "";
        final String country = state.getCountry() != null ? state.getCountry() : "";
        mPi.setText(country.isEmpty() || pi.isEmpty() ? pi : country + ", " + pi);
        mPi.setVisibility(mPi.getText().length() == 0 ? View.GONE : View.VISIBLE);
    }

    /** Status text borrows the PS slot, dimmed, until hideStatus() restores the station name. */
    private void showStatus(@StringRes final int stringRes) {
        mStatusShown = true;
        mPs.setText(stringRes);
        mPs.setAlpha(.55f);
    }

    private void hideStatus() {
        mStatusShown = false;
        mPs.setAlpha(1f);
        mPs.setText(mLastPsText);
    }

    private void clearMetadata() {
        mLastPsText = "";
        mPs.setText("");
        mRt.setText("");
        mPty.setText("");
        mPty.setVisibility(View.GONE);
        mPi.setText("");
        mPi.setVisibility(View.GONE);
    }
}
