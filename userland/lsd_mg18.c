/* SPDX-License-Identifier: MIT
 * lsd-only MobileGestalt answers for the Pong 22H374 bring-up.
 *
 * Launched only by the UI adapter inside lsd.  The LaunchServices database
 * seed performs a synchronous MobileGestalt query whose reply never arrives
 * on this port; the seed then holds the LaunchServices lock that SpringBoard
 * waits for.  This dylib interposes the MG entry points in lsd's own address
 * space and answers the identity/disk questions locally.  It is deliberately
 * separate from libpong-ui18.dylib so display and render processes keep the
 * native MobileGestalt implementation.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include "PongGestaltKeys.h"

typedef const void *PongMGRef;
extern PongMGRef MGCopyAnswer(CFStringRef);
extern bool MGGetBoolAnswer(CFStringRef);
extern PongMGRef MGGetStringAnswer(CFStringRef);
extern int32_t MGGetSInt32Answer(CFStringRef,int32_t);
extern int64_t MGGetSInt64Answer(CFStringRef,int64_t);
extern CFDictionaryRef MGCopyMultipleAnswers(CFArrayRef,CFDictionaryRef);
extern const CFStringRef kMGQDiskUsageTotalDataCapacity;
extern const CFStringRef kMGQDiskUsageTotalDataAvailable;
extern const CFStringRef kMGQDiskUsageTotalDiskCapacity;

static const char *mg_key_text(CFStringRef key,char *buf,size_t size) {
    if(!key || CFGetTypeID(key)!=CFStringGetTypeID())return "?";
    return CFStringGetCString(key,buf,size,kCFStringEncodingUTF8)?buf:"?";
}
static void mg_log(const char *fn,const char *key) {
    static unsigned logged;
    if(__atomic_fetch_add(&logged,1,__ATOMIC_RELAXED)<160)
        fprintf(stderr,"Pong MG18: %s %s\n",fn,key);
}
static PongMGRef mg_string(const char *value) {
    return CFStringCreateWithCString(kCFAllocatorDefault,value,kCFStringEncodingUTF8);
}
static PongMGRef mg_number(int64_t value) {
    return CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt64Type,&value);
}
static const char *mg_string_table(const char *key) {
    if(!strcmp(key,"ProductType"))return "iPad7,11";
    if(!strcmp(key,"DeviceClass"))return "iPad";
    if(!strcmp(key,"HardwareModel"))return "J171AP";
    if(!strcmp(key,"ModelNumber"))return "J171AP";
    if(!strcmp(key,"RegionInfo"))return "LL/A";
    if(!strcmp(key,"MarketingName"))return "iPad";
    if(!strcmp(key,"BuildVersion"))return "22H374";
    if(!strcmp(key,"ProductVersion"))return "18.7.10";
    if(!strcmp(key,"UniqueDeviceID"))return "0000000000000000000000000000000000000000";
    if(!strcmp(key,"SerialNumber"))return "PONGLIUQIN0001";
    if(!strcmp(key,"ModelName"))return "iPad";
    if(!strcmp(key,"RegulatoryModelNumber"))return "J171AP";
    return NULL;
}
static int64_t mg_storage(const char *key) {
    struct statfs fs;
    if(!statfs("/private/var",&fs) && fs.f_bsize) {
        uint64_t blocks=!strcmp(key,"TotalDataAvailable")?fs.f_bavail:fs.f_blocks;
        return (int64_t)(blocks*fs.f_bsize);
    }
    return 32LL<<30;
}
static PongMGRef mg_answer(CFStringRef key) {
    char buf[128];const char *raw=mg_key_text(key,buf,sizeof(buf));
    mg_log("copy",raw);
    CFStringRef canonical=canonical_key(key);
    const char *asked=mg_key_text(canonical,buf,sizeof(buf));
    const char *text=mg_string_table(asked);
    if(text)return mg_string(text);
    if(!strcmp(asked,"UniqueChipID")||!strcmp(asked,"ChipID"))return mg_number(0x4c495551);
    if(!strcmp(asked,"HasBattery")||!strcmp(asked,"GasGaugeCapability")||
       !strcmp(asked,"IsVirtualDevice")||!strcmp(asked,"IsSimulator"))
        return (PongMGRef)kCFBooleanFalse;
    if(!strcmp(asked,"ExternalPowerSourceConnected")||!strcmp(asked,"ExternalConnected"))
        return (PongMGRef)kCFBooleanTrue;
    if(!strcmp(asked,"TotalDataCapacity")||!strcmp(asked,"TotalDataAvailable")||
       !strcmp(asked,"TotalDiskCapacity"))
        return mg_number(mg_storage(asked));
    if(!strcmp(asked,"DiskUsage")) {
        CFMutableDictionaryRef usage=CFDictionaryCreateMutable(kCFAllocatorDefault,0,
            &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(!usage)return NULL;
        CFTypeRef capacity=mg_number(mg_storage("TotalDataCapacity"));
        CFTypeRef available=mg_number(mg_storage("TotalDataAvailable"));
        CFTypeRef disk=mg_number(mg_storage("TotalDiskCapacity"));
        if(capacity){CFDictionarySetValue(usage,kMGQDiskUsageTotalDataCapacity,capacity);CFRelease(capacity);}
        if(available){CFDictionarySetValue(usage,kMGQDiskUsageTotalDataAvailable,available);CFRelease(available);}
        if(disk){CFDictionarySetValue(usage,kMGQDiskUsageTotalDiskCapacity,disk);CFRelease(disk);}
        return usage;
    }
    return NULL;
}
static bool mg_bool(CFStringRef key) {
    char buf[128];mg_log("bool",mg_key_text(key,buf,sizeof(buf)));
    PongMGRef value=mg_answer(key);
    if(!value)return false;
    bool result=false;
    if(CFGetTypeID(value)==CFBooleanGetTypeID())result=CFBooleanGetValue(value);
    else if(CFGetTypeID(value)==CFNumberGetTypeID()){int64_t n=0;CFNumberGetValue(value,kCFNumberSInt64Type,&n);result=n!=0;}
    CFRelease(value);return result;
}
static PongMGRef mg_string_answer(CFStringRef key) {
    char buf[128];mg_log("string",mg_key_text(key,buf,sizeof(buf)));
    return mg_answer(key);
}
static int64_t mg_int64_value(CFStringRef key,int64_t fallback) {
    PongMGRef value=mg_answer(key);
    if(!value)return fallback;
    int64_t result=fallback;
    if(CFGetTypeID(value)==CFNumberGetTypeID())CFNumberGetValue(value,kCFNumberSInt64Type,&result);
    else if(CFGetTypeID(value)==CFBooleanGetTypeID())result=CFBooleanGetValue(value);
    CFRelease(value);return result;
}
static int64_t mg_int64(CFStringRef key,int64_t fallback) {
    char buf[128];mg_log("int64",mg_key_text(key,buf,sizeof(buf)));
    return mg_int64_value(key,fallback);
}
static int32_t mg_int32(CFStringRef key,int32_t fallback) {
    char buf[128];mg_log("int32",mg_key_text(key,buf,sizeof(buf)));
    int64_t value=mg_int64_value(key,fallback);
    return value<INT32_MIN||value>INT32_MAX?fallback:(int32_t)value;
}
static CFDictionaryRef mg_multiple(CFArrayRef keys,CFDictionaryRef options) {
    (void)options;
    if(!keys || CFGetTypeID(keys)!=CFArrayGetTypeID())return NULL;
    CFMutableDictionaryRef result=CFDictionaryCreateMutable(kCFAllocatorDefault,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!result)return NULL;
    for(CFIndex i=0;i<CFArrayGetCount(keys);i++) {
        CFTypeRef key=CFArrayGetValueAtIndex(keys,i);
        if(!key || CFGetTypeID(key)!=CFStringGetTypeID())continue;
        CFTypeRef value=mg_answer((CFStringRef)key);
        if(value){CFDictionarySetValue(result,key,value);CFRelease(value);}
    }
    return result;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} pong_mg_interpose[]={
    {(const void *)mg_answer,(const void *)MGCopyAnswer},
    {(const void *)mg_bool,(const void *)MGGetBoolAnswer},
    {(const void *)mg_string_answer,(const void *)MGGetStringAnswer},
    {(const void *)mg_int64,(const void *)MGGetSInt64Answer},
    {(const void *)mg_int32,(const void *)MGGetSInt32Answer},
    {(const void *)mg_multiple,(const void *)MGCopyMultipleAnswers},
};
__attribute__((constructor)) static void pong_mg_loaded(void) {
    fprintf(stderr,"Pong MG18: lsd adapter loaded\n");
}
