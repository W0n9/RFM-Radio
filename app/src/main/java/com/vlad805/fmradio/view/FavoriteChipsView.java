package com.vlad805.fmradio.view;

import android.content.Context;
import android.util.AttributeSet;
import android.view.Menu;
import android.view.View;

import androidx.appcompat.widget.PopupMenu;
import androidx.recyclerview.widget.ItemTouchHelper;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.vlad805.fmradio.R;
import com.vlad805.fmradio.controller.FavoriteController;
import com.vlad805.fmradio.controller.RadioController;
import com.vlad805.fmradio.helper.EditTextDialog;
import com.vlad805.fmradio.helper.RecyclerItemClickListener;
import com.vlad805.fmradio.helper.Toast;
import com.vlad805.fmradio.models.FavoriteStation;
import com.vlad805.fmradio.view.adapter.FavoriteChipsAdapter;

import java.util.List;

/**
 * Favorites as a horizontally scrolling chip row (C-air redesign).
 * Replaces FavoritesView + FavoritesRecyclerView (the two-column grid).
 *
 * Unlike the old stack, both controllers are injected by the host activity
 * instead of being constructed here, so the main screen shares one
 * RadioController / FavoriteController pair.
 */
public class FavoriteChipsView extends RecyclerView implements RecyclerItemClickListener.OnItemClickListener {
	private static final int MENU_RENAME = 100;
	private static final int MENU_REMOVE = 101;
	private static final int MENU_REORDER = 102;

	private List<FavoriteStation> mStations;
	private FavoriteChipsAdapter mAdapter;
	private FavoriteController mController;
	private RadioController mRadioController;
	private Toast mToast;
	private SimpleItemTouchHelperCallback mTouchHelperCallback;
	private boolean mEditMode;

	public interface OnFavoritesChangedListener {
		void onFavoritesChanged();
	}

	public interface OnListsClickListener {
		void onListsClick();
	}

	private OnFavoritesChangedListener mOnFavoritesChangedListener;
	private OnListsClickListener mOnListsClickListener;

	public FavoriteChipsView(final Context context) {
		this(context, null);
	}

	public FavoriteChipsView(final Context context, final AttributeSet attrs) {
		super(context, attrs);

		mToast = Toast.create(context);

		setLayoutManager(new LinearLayoutManager(context, LinearLayoutManager.HORIZONTAL, false));
		setClipToPadding(false);

		mAdapter = new FavoriteChipsAdapter(context);
		setAdapter(mAdapter);

		mTouchHelperCallback = new SimpleItemTouchHelperCallback(mAdapter, false);
		new ItemTouchHelper(mTouchHelperCallback).attachToRecyclerView(this);

		addOnItemTouchListener(new RecyclerItemClickListener(context, this, this));
	}

	/** Both controllers come from the host; reload once both are present. */
	public void setControllers(final FavoriteController favorites, final RadioController radio) {
		mController = favorites;
		mRadioController = radio;
		reload(false);
	}

	public void reload(final boolean force) {
		if (mController == null) {
			return;
		}

		if (force) {
			mController.reload();
		}

		mStations = mController.getStationsInCurrentList();
		mAdapter.setList(mStations);
		notifyFavoritesChanged();
	}

	public void setActiveFrequency(final int frequency) {
		mAdapter.setActiveFrequency(frequency);
	}

	public List<FavoriteStation> getStationsInCurrentList() {
		return mStations;
	}

	public void setOnFavoritesChangedListener(final OnFavoritesChangedListener listener) {
		mOnFavoritesChangedListener = listener;
	}

	public void setOnListsClickListener(final OnListsClickListener listener) {
		mOnListsClickListener = listener;
	}

	@Override
	public void onItemClick(final View view, final int position) {
		if (mStations == null || !isEnabled()) {
			return;
		}

		if (mEditMode) {
			if (position == mStations.size()) {
				setEditMode(false); // the DONE chip
			}
			return;
		}

		if (position < mStations.size()) {
			mRadioController.setFrequency(mStations.get(position).getFrequency());
			return;
		}

		if (position == mStations.size()) {
			addCurrentStation();
			return;
		}

		if (mOnListsClickListener != null) {
			mOnListsClickListener.onListsClick();
		}
	}

	@Override
	public void onLongItemClick(final View view, final int position) {
		if (mEditMode || mStations == null || !isEnabled() || position >= mStations.size()) {
			return;
		}

		final FavoriteStation station = mStations.get(position);

		final PopupMenu popupMenu = new PopupMenu(getContext(), view);
		final Menu menu = popupMenu.getMenu();
		menu.add(1, MENU_RENAME, 1, R.string.popup_station_rename);
		menu.add(1, MENU_REMOVE, 2, R.string.popup_station_remove);
		menu.add(1, MENU_REORDER, 3, R.string.popup_station_reorder);

		popupMenu.setOnMenuItemClickListener(item -> {
			switch (item.getItemId()) {
				case MENU_REMOVE: {
					mStations.remove(station);
					mAdapter.notifyItemRemoved(position);
					onFavoriteListUpdated();
					break;
				}

				case MENU_RENAME: {
					new EditTextDialog(getContext(), station.getTitle(), title -> {
						station.setTitle(title);
						mAdapter.notifyItemChanged(position);
						onFavoriteListUpdated();
					}).setTitle(R.string.popup_station_rename).open();
					break;
				}

				case MENU_REORDER: {
					setEditMode(true);
					break;
				}
			}
			return true;
		});

		popupMenu.show();
	}

	public void setEditMode(final boolean editMode) {
		if (mEditMode == editMode) {
			return;
		}

		final boolean wasEditMode = mEditMode;
		mEditMode = editMode;
		mAdapter.setEditMode(editMode);
		mTouchHelperCallback.setLongPressDragEnabled(editMode);

		if (wasEditMode && !editMode) {
			onFavoriteListUpdated();
		}
	}

	private void addCurrentStation() {
		mRadioController.requestForCurrentState((state, mode) -> {
			setActiveFrequency(state.getFrequency());
			if (hasFrequency(state.getFrequency())) {
				mToast.text(R.string.favorite_station_already_added).show();
				return;
			}

			final String ps = state.getPs();
			new EditTextDialog(getContext(), ps == null ? "" : ps.trim(), title -> {
				mStations.add(new FavoriteStation(state.getFrequency(), title));
				mAdapter.notifyItemInserted(mStations.size() - 1);
				scrollToPosition(mStations.size() - 1);
				onFavoriteListUpdated();
			}).setTitle(R.string.popup_station_create).setHint(R.string.popup_station_create_hint).open();
		});
	}

	private boolean hasFrequency(final int frequency) {
		if (frequency <= 0 || mStations == null) {
			return false;
		}

		for (final FavoriteStation station : mStations) {
			if (station.getFrequency() == frequency) {
				return true;
			}
		}

		return false;
	}

	private void onFavoriteListUpdated() {
		if (mController != null) {
			mController.save();
		}
		notifyFavoritesChanged();
	}

	private void notifyFavoritesChanged() {
		if (mOnFavoritesChangedListener != null) {
			mOnFavoritesChangedListener.onFavoritesChanged();
		}
	}
}
