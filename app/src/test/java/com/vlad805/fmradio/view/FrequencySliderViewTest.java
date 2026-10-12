package com.vlad805.fmradio.view;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class FrequencySliderViewTest {

	// Europe/US band, 100 kHz spacing
	@Test
	public void snapsToNearest100kHzOnEuBand() {
		assertEquals(106300, FrequencySliderView.snapToSpacing(106300, 87500, 108000, 100));
		assertEquals(106200, FrequencySliderView.snapToSpacing(106234, 87500, 108000, 100));
		assertEquals(106300, FrequencySliderView.snapToSpacing(106251, 87500, 108000, 100));
		assertEquals(87500, FrequencySliderView.snapToSpacing(87549, 87500, 108000, 100));
		assertEquals(108000, FrequencySliderView.snapToSpacing(107950, 87500, 108000, 100));
	}

	// Japan standard band, 50 kHz spacing
	@Test
	public void snapsOnJapanStandardBand() {
		assertEquals(90000, FrequencySliderView.snapToSpacing(90020, 76000, 95000, 50));
		assertEquals(90050, FrequencySliderView.snapToSpacing(90030, 76000, 95000, 50));
		assertEquals(76000, FrequencySliderView.snapToSpacing(76010, 76000, 95000, 50));
	}

	// 200 kHz spacing
	@Test
	public void snapsTo200kHzGrid() {
		assertEquals(87700, FrequencySliderView.snapToSpacing(87749, 87500, 108000, 200));
		assertEquals(87900, FrequencySliderView.snapToSpacing(87900, 87500, 108000, 200));
		assertEquals(87900, FrequencySliderView.snapToSpacing(87801, 87500, 108000, 200));
	}

	@Test
	public void clampsToBandEdges() {
		assertEquals(87500, FrequencySliderView.snapToSpacing(80000, 87500, 108000, 100));
		assertEquals(108000, FrequencySliderView.snapToSpacing(120000, 87500, 108000, 100));
		assertEquals(95000, FrequencySliderView.snapToSpacing(96000, 76000, 95000, 50));
	}
}
