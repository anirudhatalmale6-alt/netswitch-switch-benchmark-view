// JNI bridge — exposes the native ggw_perfcore engine to Kotlin/Java on Android.
// The SAME core .cpp is used unchanged; only main() is compiled out. The identical
// file is added to the iOS Xcode target as Obj-C++ (.mm), so both platforms run one core.
#define PERFCORE_NO_MAIN
#include "../ggw_perfcore.cpp"

#include <jni.h>

extern "C" {

// DRAMM priority-table bandwidth (GB/s) — plan p.5
JNIEXPORT jdouble JNICALL
Java_com_ai2orbit_perfcore_PerfCore_drammGbps(JNIEnv*, jobject, jint repeat){
  return dramm_run((int)repeat).gbps;
}

// Bus-friction throughput (Mops/s) at a given friction level 0..1
JNIEXPORT jdouble JNICALL
Java_com_ai2orbit_perfcore_PerfCore_frictionMops(JNIEnv*, jobject, jdouble level){
  return friction_run((double)level).mops;
}

// Headroom gained by removing friction (streaming / random)
JNIEXPORT jdouble JNICALL
Java_com_ai2orbit_perfcore_PerfCore_frictionHeadroom(JNIEnv*, jobject){
  double hi=friction_run(0.0).mops, lo=friction_run(1.0).mops;
  return (lo>0)? hi/lo : 0.0;
}

// Electricity: P = V*I (Watts)
JNIEXPORT jdouble JNICALL
Java_com_ai2orbit_perfcore_PerfCore_powerWatts(JNIEnv*, jobject, jdouble volts, jdouble amps){
  return power_calc((double)volts,(double)amps,1.0).watts;
}

// Peak (max) throughput reached this pass — ops/s
JNIEXPORT jdouble JNICALL
Java_com_ai2orbit_perfcore_PerfCore_peakOpsPerSec(JNIEnv*, jobject){
  return friction_run(0.0).mops * 1e6;
}

// Self-check: returns 0 when all internal checks pass (same as CLI selftest)
JNIEXPORT jint JNICALL
Java_com_ai2orbit_perfcore_PerfCore_selfTest(JNIEnv*, jobject){
  FricResult f0=friction_run(0.0), f1=friction_run(1.0);
  DrammResult d=dramm_run(1);
  PowerResult p=power_calc(5.0,3.0,1.5e9);
  bool ok = (f0.mops>f1.mops) && d.integrity_ok && (std::fabs(p.watts-15.0)<1e-9);
  return ok?0:1;
}

} // extern "C"
