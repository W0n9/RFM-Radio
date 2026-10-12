package com.vlad805.fmradio.view.adapter;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.vlad805.fmradio.R;
import com.vlad805.fmradio.Utils;
import com.vlad805.fmradio.models.FavoriteStation;
import com.vlad805.fmradio.view.ItemTouchHelperAdapter;
import com.vlad805.fmradio.view.ItemTouchHelperViewHolder;

import java.util.Collections;
import java.util.List;

/**
 * Horizontal favorite chips of the C-air redesign: one chip per station,
 * followed by the "+" add chip and the lists-manager chip. In edit mode the
 * action chips are replaced by a single DONE chip while stations are dragged
 * around (whole-chip drag, no handle).
 */
public class FavoriteChipsAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> implements ItemTouchHelperAdapter {
	private static final int TYPE_STATION = 0;
	private static final int TYPE_ACTION_ADD = 1;
	private static final int TYPE_ACTION_LISTS = 2;
	private static final int TYPE_ACTION_DONE = 3;

	private final LayoutInflater mInflater;
	private List<FavoriteStation> mList = Collections.emptyList();
	private int mActiveFrequency = -1;
	private boolean mEditMode;

	public FavoriteChipsAdapter(final Context context) {
		mInflater = LayoutInflater.from(context);
	}

	public void setList(final List<FavoriteStation> stations) {
		mList = stations;
		notifyDataSetChanged();
	}

	public void setActiveFrequency(final int activeFrequency) {
		if (mActiveFrequency == activeFrequency) {
			return;
		}

		final int previousIndex = findStationIndexByFrequency(mActiveFrequency);
		mActiveFrequency = activeFrequency;
		final int currentIndex = findStationIndexByFrequency(mActiveFrequency);

		if (previousIndex >= 0) {
			notifyItemChanged(toAdapterPosition(previousIndex));
		}

		if (currentIndex >= 0) {
			notifyItemChanged(toAdapterPosition(currentIndex));
		}
	}

	public void setEditMode(final boolean editMode) {
		if (mEditMode == editMode) {
			return;
		}

		mEditMode = editMode;
		notifyDataSetChanged();
	}

	public boolean isEditMode() {
		return mEditMode;
	}

	@Override
	public int getItemViewType(final int position) {
		if (mEditMode) {
			// Edit mode: stations only, plus the trailing DONE chip.
			return position < mList.size() ? TYPE_STATION : TYPE_ACTION_DONE;
		}

		// Normal mode: "+" always first (never scrolls off), then stations, then the lists chip.
		if (position == 0) {
			return TYPE_ACTION_ADD;
		}
		return position <= mList.size() ? TYPE_STATION : TYPE_ACTION_LISTS;
	}

	@Override
	public int getItemCount() {
		return mList.size() + (mEditMode ? 1 : 2);
	}

	/** Adapter position of a station = list index, plus one for the leading "+" outside edit mode. */
	private int toAdapterPosition(final int listIndex) {
		return mEditMode ? listIndex : listIndex + 1;
	}

	private int toListIndex(final int adapterPosition) {
		return mEditMode ? adapterPosition : adapterPosition - 1;
	}

	@Override
	@NonNull
	public RecyclerView.ViewHolder onCreateViewHolder(@NonNull final ViewGroup parent, final int viewType) {
		if (viewType == TYPE_STATION) {
			return new StationHolder(mInflater.inflate(R.layout.favorite_chip_item, parent, false));
		}
		return new ActionHolder(mInflater.inflate(R.layout.favorite_chip_action, parent, false));
	}

	@Override
	public void onBindViewHolder(@NonNull final RecyclerView.ViewHolder holder, final int position) {
		if (holder.getItemViewType() == TYPE_STATION) {
			final FavoriteStation station = mList.get(toListIndex(position));
			((StationHolder) holder).populate(station, station.getFrequency() == mActiveFrequency);
			return;
		}

		final ActionHolder action = (ActionHolder) holder;
		switch (holder.getItemViewType()) {
			case TYPE_ACTION_ADD:
				action.bindText("+", R.string.favorite_add_cd);
				break;
			case TYPE_ACTION_LISTS:
				action.bindIcon(R.drawable.ic_favorite_lists, R.string.favorite_lists_entry);
				break;
			case TYPE_ACTION_DONE:
			default:
				action.bindText("✓", R.string.favorites_done);
				break;
		}
	}

	@Override
	public void onItemMove(final int fromPosition, final int toPosition) {
		if (fromPosition < 0 || toPosition < 0 || fromPosition >= mList.size() || toPosition >= mList.size()) {
			return;
		}

		if (fromPosition < toPosition) {
			for (int i = fromPosition; i < toPosition; i++) {
				Collections.swap(mList, i, i + 1);
			}
		} else {
			for (int i = fromPosition; i > toPosition; i--) {
				Collections.swap(mList, i, i - 1);
			}
		}

		notifyItemMoved(fromPosition, toPosition);
	}

	@Override
	public void onItemDismiss(final int position) {
	}

	private int findStationIndexByFrequency(final int frequency) {
		for (int i = 0; i < mList.size(); ++i) {
			if (mList.get(i).getFrequency() == frequency) {
				return i;
			}
		}

		return -1;
	}

	static class StationHolder extends RecyclerView.ViewHolder implements ItemTouchHelperViewHolder {
		private final TextView frequency;
		private final TextView name;

		StationHolder(final View root) {
			super(root);
			frequency = root.findViewById(R.id.chip_frequency);
			name = root.findViewById(R.id.chip_name);
		}

		void populate(final FavoriteStation station, final boolean active) {
			itemView.setSelected(active);
			frequency.setText(Utils.getMHz(station.getFrequency()));
			name.setText(station.getTitle());
			name.setVisibility(station.getTitle() == null || station.getTitle().isEmpty() ? View.GONE : View.VISIBLE);
		}

		@Override
		public void onItemSelected() {
			itemView.setAlpha(0.7f);
		}

		@Override
		public void onItemClear() {
			itemView.setAlpha(1f);
		}
	}

	static class ActionHolder extends RecyclerView.ViewHolder {
		private final TextView text;
		private final ImageView icon;

		ActionHolder(final View root) {
			super(root);
			text = root.findViewById(R.id.chip_action_text);
			icon = root.findViewById(R.id.chip_action_icon);
		}

		void bindText(final String label, final int contentDescriptionRes) {
			text.setVisibility(View.VISIBLE);
			icon.setVisibility(View.GONE);
			text.setText(label);
			itemView.setContentDescription(itemView.getContext().getString(contentDescriptionRes));
		}

		void bindIcon(final int iconRes, final int contentDescriptionRes) {
			text.setVisibility(View.GONE);
			icon.setVisibility(View.VISIBLE);
			icon.setImageResource(iconRes);
			itemView.setContentDescription(itemView.getContext().getString(contentDescriptionRes));
		}
	}
}
