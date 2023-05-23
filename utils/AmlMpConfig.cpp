/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlMpConfig"
#include <utils/AmlMpLog.h>
#include "AmlMpConfig.h"
#include <cutils/properties.h>
#include <string>
#include <unistd.h>
#include "AmlMpUtils.h"

namespace aml_mp {

static const char* mName = LOG_TAG;

template <typename T>
void AmlMpConfig::initProperty(const char* propertyName, T& value)
{
    char bufs[PROP_VALUE_MAX];

    if (property_get(propertyName, bufs, 0) > 0) {
        value = strtol(bufs, nullptr, 0);
    }
}

template<>
void AmlMpConfig::initProperty(const char* propertyName, std::string& value)
{
    char bufs[PROP_VALUE_MAX];

    if (property_get(propertyName, bufs, 0) > 0) {
        value = bufs;
    }
}

void AmlMpConfig::reset()
{
    mLogDebug = 0;
    mLogMask = 0;

    mTsPlayerNonTunnel = 0;
#if ANDROID_PLATFORM_SDK_VERSION >= 30
    mTsPlayerNonTunnel = isSupportMultiHwDemux();
#endif

    mWaitingEcmMode = 1;
    mWriteBufferSize = 2; // default write buffer size set to 2MB.
    mDumpPackts = 0;

// android Q is use surface by default in AmTsPlayer
#if ANDROID_PLATFORM_SDK_VERSION == 29
    mUseVideoTunnel = 0;
#else
    mUseVideoTunnel = 1;
#endif
    mPreferTunerHal = 0;
    mCasPipSupport = 0;
    mCasFCCSupport = 0;
    mSecMemSize = 0;
    mCasType = "none";

    mDisableSubtitle = 0;
}

void AmlMpConfig::init()
{
#ifdef __linux__
#ifndef ANDROID
    return initLinux();
#endif
#endif

    initProperty("vendor.amlmp.log-debug", mLogDebug);
    initProperty("vendor.amlmp.log-mask", mLogMask);
    initProperty("vendor.amtsplayer.pipeline", mTsPlayerNonTunnel);
    initProperty("vendor.amlmp.waiting-ecm-mode", mWaitingEcmMode);
    initProperty("vendor.amlmp.write-buffer-size", mWriteBufferSize);
    initProperty("vendor.media.amlmp.prefer.tuner_hal", mPreferTunerHal);
    initProperty("vendor.enable.dump.packts", mDumpPackts);
    initProperty("vendor.cas.support.pip.function", mCasPipSupport);
    initProperty("vendor.cas.support.fcc.function", mCasFCCSupport);
    initProperty("vendor.secmem.size", mSecMemSize);
    initProperty("vendor.cas.type", mCasType);
}

void AmlMpConfig::initLinux()
{
    mTsPlayerNonTunnel = 0;
    bool isMultiHwDemux = isSupportMultiHwDemux();
    if (isMultiHwDemux) {
         if ((access("/usr/bin/westeros",F_OK) == 0) || (access("/usr/bin/weston",F_OK) == 0)) {
            mTsPlayerNonTunnel = 1;
            MLOGI("is non tunnel mode!");
          }
    }

    initProperty("vendor_amlmp_log_debug", mLogDebug);
    initProperty("vendor_amlmp_log_mask", mLogMask);
    initProperty("TSPLAYER_PIPELINE", mTsPlayerNonTunnel);
    initProperty("vendor_amlmp_waiting_ecm_mode", mWaitingEcmMode);
    initProperty("vendor_amlmp_write_buffer_size", mWriteBufferSize);
    initProperty("vendor_enable_dump_packts", mDumpPackts);
    initProperty("vendor_cas_support_pip_function", mCasPipSupport);
    initProperty("vendor_cas_support_fcc_function", mCasFCCSupport);
    initProperty("vendor_secmem_size", mSecMemSize);
    initProperty("vendor_cas_type", mCasType);
    initProperty("vendor_amlmp_disable_subtitle", mDisableSubtitle);
}

AmlMpConfig::AmlMpConfig()
{
    reset();
    init();
}

}
