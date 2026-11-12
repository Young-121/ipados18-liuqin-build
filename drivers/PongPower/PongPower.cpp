/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOService.h>
#include <libkern/OSKextLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "PongPlatformResources.h"
extern kmod_info_t g_kernel_kmod_info;
#include "../PongBootCapture/PongBootCapture.h"
#include "../PongUsb/PongUsbDevice.h"
extern "C" {
#ifndef PONG_POWER_BACKLIGHT_ONLY
void PongAudioServiceStaticInit(void);
bool pong_audio_service_classes_ready(void) {
 static uint32_t state;
 uint32_t expected=0;
 if(__atomic_compare_exchange_n(&state,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  void *h=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
  if(!h){__atomic_store_n(&state,3,__ATOMIC_RELEASE);return false;}
  PongAudioServiceStaticInit();
  __atomic_store_n(&state,OSMetaClass::postModLoad(h)==0?2u:3u,__ATOMIC_RELEASE);
 }
 // Never repeat partially failed C++ meta-class constructors or wait on
 // another optional caller. Actual endpoint publication stays independent.
 return __atomic_load_n(&state,__ATOMIC_ACQUIRE)==2;
}
#endif /* PONG_POWER_BACKLIGHT_ONLY */
void sheng_adsp_probe(void);
void sheng_audio_probe(void);
void sheng_battery_start(void);
void PongBatteryStaticInit(void);
volatile int32_t native_battery_stats[16];
static int (*previous_halt_restart)(unsigned int);
static uint32_t pong_panic_latched;
static int
pong_halt_restart(unsigned int type)
{
    /* The XNU shutdown slider reaches PEHaltRestart(kPEHaltCPU); the stock
     * Pong platform has no handler, so the CPUs spin forever. PSCI
     * SYSTEM_OFF / SYSTEM_RESET is the firmware's own power path (the same
     * conduit PongBootCapture uses for its reset target). */
    /* XNU reports kPEPanicBegin / kPEPanicRestartCPU before the panic path
     * calls halt_all_cpus(). Keep the previous behaviour there (CPUs spin,
     * panic text stays on screen) instead of power-cycling it away. */
    if (type == kPEPanicBegin || type == kPEPanicRestartCPU ||
        type == kPEPanicRestartCPUNoCallouts) {
        __atomic_store_n(&pong_panic_latched, 1, __ATOMIC_RELEASE);
        return previous_halt_restart ? previous_halt_restart(type) : -1;
    }
    if (type == kPEHaltCPU || type == kPERestartCPU) {
        if (__atomic_load_n(&pong_panic_latched, __ATOMIC_ACQUIRE)) {
            return previous_halt_restart ? previous_halt_restart(type) : -1;
        }
        register uint64_t x0 __asm__("x0") = (type == kPEHaltCPU) ? 0x84000008u : 0x84000009u;
        register uint64_t x1 __asm__("x1") = 0;
        register uint64_t x2 __asm__("x2") = 0;
        register uint64_t x3 __asm__("x3") = 0;
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                         :: "memory", "x4", "x5", "x6", "x7", "x8", "x9",
                            "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
        return -1;
    }
    return previous_halt_restart ? previous_halt_restart(type) : -1;
}
void pong_power_start_optional(void) {
 pong_rpmh_init();
 static uint32_t halt_installed;
 if (!__atomic_exchange_n(&halt_installed, 1, __ATOMIC_ACQ_REL)) {
  previous_halt_restart = PE_halt_restart;
  PE_halt_restart = pong_halt_restart;
 }
#ifndef PONG_POWER_BACKLIGHT_ONLY
 // Register the transport classes independently of ADSP readiness. Actual
 // USB/audio services still publish only after their own hardware succeeds.
 if(pong_audio_service_classes_ready())PongUsb::start();
 unsigned enabled=0;PE_parse_boot_argn("pong_adsp",&enabled,sizeof(enabled));
 if(enabled!=2)return;
 static uint32_t started;
 if(__atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL))return;
 sheng_adsp_probe();
 // DSP channel registration is ready synchronously; remote opens follow
 // only after firmware and PAS prerequisites complete on its worker.
 void *h=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
 if(h){PongBatteryStaticInit();if(!OSMetaClass::postModLoad(h))sheng_battery_start();}
 sheng_audio_probe();
#endif /* PONG_POWER_BACKLIGHT_ONLY */
}
}
