// 一次性 root 探针：以 uid 0 经 app_process 调用隐藏的 AudioManager patch API。
// 用途：验证 H4（平台是否允许 FM_TUNER -> SPEAKER 直连 patch）。
//
// 用法（设备上）：
//   CLASSPATH=/data/local/tmp/fmpatch.dex app_process / FmPatch list
//   CLASSPATH=/data/local/tmp/fmpatch.dex app_process / FmPatch route speaker|headphone|headset
//   CLASSPATH=/data/local/tmp/fmpatch.dex app_process / FmPatch release
//   route 可追加 hold 秒数，让进程保持存活（观察 patch 是否随创建者死亡而释放）

import java.lang.reflect.Array;
import java.lang.reflect.Method;
import java.util.ArrayList;

public class FmPatch {
    static final int DEVICE_IN_FM_TUNER = 0x80002000;
    static final int DEVICE_OUT_SPEAKER = 0x2;
    static final int DEVICE_OUT_WIRED_HEADSET = 0x4;
    static final int DEVICE_OUT_WIRED_HEADPHONE = 0x8;

    static Class<?> am, audioPort, audioDevicePort, audioPatch, audioPortConfig;

    public static void main(String[] args) throws Exception {
        am = Class.forName("android.media.AudioManager");
        audioPort = Class.forName("android.media.AudioPort");
        audioDevicePort = Class.forName("android.media.AudioDevicePort");
        audioPatch = Class.forName("android.media.AudioPatch");
        audioPortConfig = Class.forName("android.media.AudioPortConfig");

        String cmd = args.length > 0 ? args[0] : "list";
        if (cmd.equals("list")) {
            listPorts();
            listFmPatches();
        } else if (cmd.equals("release")) {
            releaseFmPatches();
        } else if (cmd.equals("route")) {
            int sink = args[1].equals("speaker") ? DEVICE_OUT_SPEAKER : 0;
            releaseFmPatches();
            if (sink != 0) {
                createFmPatch(sink);
            } else {
                // "wired"：按当前可用端口选 headphone(0x8)，物理带麦耳机则是 headset(0x4)
                createFmPatch(findWiredSink());
            }
            listFmPatches();
            if (args.length > 2) {
                int hold = Integer.parseInt(args[2]);
                System.out.println("holding " + hold + "s");
                Thread.sleep(hold * 1000L);
            }
        }
        // AudioPortEventHandler 的 HandlerThread 是非守护线程，不显式退出会挂住
        System.out.flush();
        System.exit(0);
    }

    static int deviceType(Object port) throws Exception {
        // AudioDevicePort.type() 返回 native audio_devices_t
        return (int) audioDevicePort.getMethod("type").invoke(port);
    }

    static ArrayList<Object> ports() throws Exception {
        ArrayList<Object> list = new ArrayList<>();
        int rc = (int) am.getMethod("listAudioPorts", ArrayList.class).invoke(null, list);
        if (rc != 0) System.out.println("listAudioPorts rc=" + rc);
        return list;
    }

    static ArrayList<Object> patches() throws Exception {
        ArrayList<Object> list = new ArrayList<>();
        int rc = (int) am.getMethod("listAudioPatches", ArrayList.class).invoke(null, list);
        if (rc != 0) System.out.println("listAudioPatches rc=" + rc);
        return list;
    }

    static void listPorts() throws Exception {
        for (Object p : ports()) {
            if (audioDevicePort.isInstance(p)) {
                System.out.printf("port %s type=0x%x%n", p, deviceType(p));
            }
        }
    }

    static boolean isFmPatch(Object patch) throws Exception {
        Object sources = audioPatch.getMethod("sources").invoke(patch);
        for (int i = 0; i < Array.getLength(sources); i++) {
            Object port = audioPortConfig.getMethod("port").invoke(Array.get(sources, i));
            if (audioDevicePort.isInstance(port) && deviceType(port) == DEVICE_IN_FM_TUNER) {
                return true;
            }
        }
        return false;
    }

    static void listFmPatches() throws Exception {
        for (Object patch : patches()) {
            if (isFmPatch(patch)) System.out.println("fm patch: " + patch);
        }
    }

    static void releaseFmPatches() throws Exception {
        Method release = am.getMethod("releaseAudioPatch", audioPatch);
        for (Object patch : patches()) {
            if (isFmPatch(patch)) {
                int rc = (int) release.invoke(null, patch);
                System.out.println("releaseAudioPatch rc=" + rc + " " + patch);
            }
        }
    }

    static void createFmPatch(int sinkType) throws Exception {
        if (sinkType < 0) return;
        Object source = null, sink = null;
        for (Object p : ports()) {
            if (!audioDevicePort.isInstance(p)) continue;
            int t = deviceType(p);
            if (t == DEVICE_IN_FM_TUNER) source = p;
            if (t == sinkType) sink = p;
        }
        System.out.printf("source=%s sink=%s%n", source, sink);
        if (source == null || sink == null) return;

        Method activeConfig = audioPort.getMethod("activeConfig");
        Object configs = Array.newInstance(audioPortConfig, 1);
        Object sinks = Array.newInstance(audioPortConfig, 1);
        Array.set(configs, 0, activeConfig.invoke(source));
        Array.set(sinks, 0, activeConfig.invoke(sink));
        Object out = Array.newInstance(audioPatch, 1);
        Method create = am.getMethod("createAudioPatch", out.getClass(), configs.getClass(), sinks.getClass());
        int rc = (int) create.invoke(null, out, configs, sinks);
        System.out.println("createAudioPatch rc=" + rc + " patch=" + Array.get(out, 0));
    }

    /** 返回当前存在的有线输出端口类型：headphone 优先，其次带麦 headset。 */
    static int findWiredSink() throws Exception {
        boolean hasHeadphone = false, hasHeadset = false;
        for (Object p : ports()) {
            if (!audioDevicePort.isInstance(p)) continue;
            int t = deviceType(p);
            if (t == DEVICE_OUT_WIRED_HEADPHONE) hasHeadphone = true;
            if (t == DEVICE_OUT_WIRED_HEADSET) hasHeadset = true;
        }
        if (hasHeadphone) return DEVICE_OUT_WIRED_HEADPHONE;
        if (hasHeadset) return DEVICE_OUT_WIRED_HEADSET;
        System.out.println("no wired output available");
        return -1;
    }
}
