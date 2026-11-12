// SPDX-License-Identifier: BSD-2-Clause-Patent
// Start the source-built UFS adapter after the original storage family loads.
//
// The original IOStorageFamily and APFS stay in the kernel collection.  The
// public IOBlockStorageDevice implementation is compiled from IOStorageFamily
// source, with its metaclass registered below the original class, so the
// original IOBlockStorageDriver can match the source-backed PongUfsBlock.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/OSKextLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

extern kmod_info_t g_kernel_kmod_info;

// The source-storage build keeps the class constructors out of
// __DATA,__mod_init_func and calls them explicitly after the original family
// has loaded.  llvm-objcopy globalizes these symbols in the source objects.
extern "C" void source_storage_base_init(void) __asm__("__GLOBAL__sub_I_IOBlockStorageDevice.cpp");
extern "C" void source_storage_ufs_init(void) __asm__("__GLOBAL__sub_I_NativeUfs.cpp");
extern "C" void source_storage_transport_init(void) __asm__("__GLOBAL__sub_I_PongUfsTransport.cpp");
extern "C" IOService *native_ufs_source_create(void);

// NativeUfs records how far root start got and the last soft-failure reason.
// The minimal Pong kernel has no recorder yet, so provide the storage the
// diagnostic panic path reads; a future root driver can update these.
extern "C" volatile uint32_t native_ufs_root_step = 0;
extern "C" const char *volatile native_ufs_root_failure = nullptr;

static const OSMetaClass *storageParent;
static IOService *storageDriver;
static bool attempted;

// The source IOBlockStorageDevice metaclass asks for the original base class
// when its constructor runs.  It is registered only after this function has
// found the original family.
extern "C" const OSMetaClass *native_source_storage_parent(void)
{
  if (!storageParent) panic("SourceStorage: metaclass initialized before original family");
  return storageParent;
}

extern "C" void native_source_storage_start(void)
{
  unsigned enabled = 0;
  if (!PE_parse_boot_argn("native-source-storage", &enabled, sizeof(enabled)) ||
      enabled != 1 || attempted) {
    return;
  }
  attempted = true;

  OSReturn status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOStorageFamily");
  if (status != kOSReturnSuccess) {
    panic("SourceStorage: IOStorageFamily load failed %x", status);
  }
  const OSSymbol *name = OSSymbol::withCString("IOBlockStorageDevice");
  storageParent = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
  if (name) name->release();
  if (!storageParent || storageParent->getClassSize() != sizeof(IOBlockStorageDevice)) {
    panic("SourceStorage: original base layout %u differs from source %lu",
          storageParent ? storageParent->getClassSize() : 0,
          (unsigned long)sizeof(IOBlockStorageDevice));
  }

  status = OSKextLoadKextWithIdentifier("com.apple.filesystems.apfs");
  if (status != kOSReturnSuccess) {
    panic("SourceStorage: APFS load failed %x", status);
  }

  void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
  if (!handle) panic("SourceStorage: cannot begin class registration");
  source_storage_base_init();
  source_storage_ufs_init();
  source_storage_transport_init();
  status = OSMetaClass::postModLoad(handle);
  if (status != kOSReturnSuccess) {
    panic("SourceStorage: class registration failed %x", status);
  }

  OSDictionary *matching = IOService::serviceMatching("IOPlatformExpertDevice");
  IOService *provider = matching
      ? IOService::waitForMatchingService(matching, 5000000000ULL) : nullptr;
  if (!provider) panic("SourceStorage: platform root unavailable");
  storageDriver = native_ufs_source_create();
  if (!storageDriver) panic("SourceStorage: cannot allocate UFS service");
  const char *step = !storageDriver->init() ? "init"
      : !storageDriver->attach(provider) ? "attach"
      : !storageDriver->start(provider) ? "start" : nullptr;
  if (step) {
    panic("SourceStorage: UFS %s failed (root step %u: %s)", step,
          native_ufs_root_step,
          native_ufs_root_failure ? native_ufs_root_failure : "no reason recorded");
  }
  printf("SourceStorage: UFS started through source IOStorageFamily/base\n");
}
