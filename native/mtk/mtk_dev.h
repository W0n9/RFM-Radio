#pragma once

// Thin fd-scoped wrappers over the MTK FM driver ioctls. Each wrapper logs
// failures with errno and restores errno for the caller. Not host-tested;
// pure logic lives in mtk_params.

#include <stdint.h>

#include "mtk_ioctl.h"

// Opens /dev/fm. Returns fd >= 0, or -1 with errno set.
int mtk_dev_open();
void mtk_dev_close(int fd);

// POWERUP tunes to parm.freq as part of power-on. Checks parm.err.
bool mtk_power_up(int fd, fm_tune_parm *parm);
bool mtk_power_down(int fd);
// TUNE updates parm.freq in place (driver may snap to the channel grid).
bool mtk_tune(int fd, fm_tune_parm *parm);
// GETRSSI returns a dBm-like signed value directly (no Qualcomm-style decode).
bool mtk_get_rssi(int fd, int32_t *rssi);
// SOFT_MUTE_TUNE: one blocking ioctl does tune + sample + kernel-side
// station verdict. freq is in 10 kHz units; on success *rssi is the raw CQI
// value (not the GETRSSI dBm scale) and *valid the kernel's joint verdict.
// Requires the radio to be powered up; a false return means no reading.
bool mtk_soft_mute_tune(int fd, uint16_t freq, int32_t *rssi, int32_t *valid);
bool mtk_set_mute(int fd, bool mute);
// stereo_auto=true -> auto stereo; false -> force mono.
bool mtk_set_stereo_auto(int fd, bool stereo_auto);
bool mtk_set_antenna(int fd, int32_t antenna);
bool mtk_rds_onoff(int fd, bool on);

bool mtk_get_chip_id(int fd, uint16_t *chip_id);
bool mtk_rds_support(int fd, int32_t *support);
bool mtk_is_powered_up(int fd, uint32_t *powered);
