#include "mtk_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "mtk_log.h"

namespace {

// ioctl wrapper: logs errno on failure and restores it for the caller.
bool mtk_ioctl(int fd, unsigned long request, void *arg, const char *what) {
    if (ioctl(fd, request, arg) != 0) {
        const int saved = errno;
        mtk_log("dev", "%s fail errno=%d", what, saved);
        errno = saved;
        return false;
    }
    return true;
}

}  // namespace

int mtk_dev_open() {
    // O_NONBLOCK is defense in depth: the verified driver never blocks reads,
    // but if a variant ever does, disable() must still be able to stop the
    // RDS reader thread.
    const int fd = open("/dev/fm", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        const int saved = errno;
        mtk_log("dev", "open /dev/fm fail errno=%d", saved);
        errno = saved;
        return -1;
    }
    return fd;
}

void mtk_dev_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

bool mtk_power_up(int fd, fm_tune_parm *parm) {
    if (!mtk_ioctl(fd, MTK_IOCTL_POWERUP, parm, "powerup")) {
        return false;
    }
    if (parm->err != 0) {
        mtk_log("dev", "powerup err=%d", parm->err);
        return false;
    }
    return true;
}

bool mtk_power_down(int fd) {
    int32_t arg = 0;
    return mtk_ioctl(fd, MTK_IOCTL_POWERDOWN, &arg, "powerdown");
}

bool mtk_tune(int fd, fm_tune_parm *parm) {
    if (!mtk_ioctl(fd, MTK_IOCTL_TUNE, parm, "tune")) {
        return false;
    }
    if (parm->err != 0) {
        mtk_log("dev", "tune err=%d", parm->err);
        return false;
    }
    return true;
}

bool mtk_get_rssi(int fd, int32_t *rssi) {
    return mtk_ioctl(fd, MTK_IOCTL_GETRSSI, rssi, "get_rssi");
}

bool mtk_soft_mute_tune(int fd, uint16_t freq, int32_t *rssi, int32_t *valid) {
    fm_softmute_tune_t parm = {};
    parm.freq = freq;
    if (!mtk_ioctl(fd, MTK_IOCTL_SOFT_MUTE_TUNE, &parm, "soft_mute_tune")) {
        return false;
    }
    *rssi = parm.rssi;
    *valid = parm.valid;
    return true;
}

bool mtk_set_mute(int fd, bool mute) {
    uint32_t arg = mute ? 1 : 0;
    return mtk_ioctl(fd, MTK_IOCTL_MUTE, &arg, "mute");
}

bool mtk_set_stereo_auto(int fd, bool stereo_auto) {
    int32_t arg = stereo_auto ? kMtkStereoAuto : kMtkForceMono;
    return mtk_ioctl(fd, MTK_IOCTL_SETMONOSTERO, &arg, "stereo");
}

bool mtk_set_antenna(int fd, int32_t antenna) {
    return mtk_ioctl(fd, MTK_IOCTL_ANA_SWITCH, &antenna, "set_antenna");
}

bool mtk_rds_onoff(int fd, bool on) {
    uint16_t arg = on ? 1 : 0;
    return mtk_ioctl(fd, MTK_IOCTL_RDS_ONOFF, &arg, "rds_onoff");
}

bool mtk_get_chip_id(int fd, uint16_t *chip_id) {
    return mtk_ioctl(fd, MTK_IOCTL_GETCHIPID, chip_id, "get_chip_id");
}

bool mtk_rds_support(int fd, int32_t *support) {
    return mtk_ioctl(fd, MTK_IOCTL_RDS_SUPPORT, support, "rds_support");
}

bool mtk_is_powered_up(int fd, uint32_t *powered) {
    return mtk_ioctl(fd, MTK_IOCTL_IS_FM_POWERED_UP, powered, "is_powered_up");
}
