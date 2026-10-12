// 一次性验证工具：以 root 身份调用 AudioSystem::setForceUse(FOR_MEDIA, ...)。
//
// 目的：验证 "MTK FM loopback 路由由 force-use 控制" 这一假设。
// app 自己调用会被 Android 12 的 MODIFY_AUDIO_ROUTING 权限拒绝
// (AudioSystem-JNI: Command failed for android_media_AudioSystem_setForceUse: -1)，
// 所以必须借助 root。
//
// 用法:
//   fmroute get            读回当前 FOR_MEDIA 的 force use
//   fmroute speaker        设为 FORCE_SPEAKER (1)
//   fmroute none           设为 FORCE_NONE (0)
//   fmroute raw <n>        设为任意值 (ROM 默认是 10 = FORCE_NO_BT_A2DP)

#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kForceForMedia = 1;  // AUDIO_POLICY_FORCE_FOR_MEDIA
constexpr int kForceNone = 0;      // AUDIO_POLICY_FORCE_NONE
constexpr int kForceSpeaker = 1;   // AUDIO_POLICY_FORCE_SPEAKER

// 设备上 libaudioclient.so 实测存在的符号
constexpr const char* kSetSymbol =
        "_ZN7android11AudioSystem11setForceUseE24audio_policy_force_use_t25audio_policy_forced_cfg_t";
constexpr const char* kGetSymbol =
        "_ZN7android11AudioSystem11getForceUseE24audio_policy_force_use_t";

const char* kCandidates[] = {
        "libaudioclient.so",
        "/system/lib64/libaudioclient.so",
};

using SetForceUseFn = void (*)(int, int);
using GetForceUseFn = int (*)(int);

void* openAudioClient() {
    for (const char* name : kCandidates) {
        void* handle = dlopen(name, RTLD_NOW);
        if (handle != nullptr) {
            fprintf(stderr, "[fmroute] loaded %s\n", name);
            return handle;
        }
        fprintf(stderr, "[fmroute] dlopen(%s) failed: %s\n", name, dlerror());
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: fmroute get|speaker|none\n");
        return 2;
    }

    void* handle = openAudioClient();
    if (handle == nullptr) {
        return 1;
    }

    auto setForceUse = reinterpret_cast<SetForceUseFn>(dlsym(handle, kSetSymbol));
    auto getForceUse = reinterpret_cast<GetForceUseFn>(dlsym(handle, kGetSymbol));

    if (setForceUse == nullptr || getForceUse == nullptr) {
        fprintf(stderr, "[fmroute] dlsym failed: set=%p get=%p (%s)\n",
                reinterpret_cast<void*>(setForceUse),
                reinterpret_cast<void*>(getForceUse), dlerror());
        return 1;
    }

    const int before = getForceUse(kForceForMedia);
    printf("[fmroute] uid=%d before=%d\n", getuid(), before);

    const char* action = argv[1];
    if (strcmp(action, "get") == 0) {
        return 0;
    }

    int target;
    if (strcmp(action, "speaker") == 0) {
        target = kForceSpeaker;
    } else if (strcmp(action, "none") == 0) {
        target = kForceNone;
    } else if (strcmp(action, "raw") == 0 && argc >= 3) {
        target = atoi(argv[2]);
    } else {
        fprintf(stderr, "[fmroute] unknown action: %s\n", action);
        return 2;
    }

    setForceUse(kForceForMedia, target);

    const int after = getForceUse(kForceForMedia);
    printf("[fmroute] set FOR_MEDIA=%d -> after=%d (%s)\n", target, after,
           after == target ? "OK" : "REJECTED/IGNORED");
    return after == target ? 0 : 1;
}
