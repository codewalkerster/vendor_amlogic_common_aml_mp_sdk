/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_MP_CONFIG_H_
#define _AML_MP_CONFIG_H_
#include <string>

namespace aml_mp {


enum {
    kDebugFlagStatistic =  1 << 0,
    kDebugFlagSubtitle  =  1 << 1,
    kDebugFlagNone =   1 << 31,
};

class AmlMpConfig
{
public:
    static AmlMpConfig& instance() {
        static AmlMpConfig c;
        return c;
    }

    ~AmlMpConfig() = default;
    void init();

public:
    int mLogDebug;
    int mLogMask;
    int mTsPlayerNonTunnel;
    int mUseVideoTunnel;
    int mWaitingEcmMode;
    int mWriteBufferSize;
    int mPreferTunerHal;
    int mDumpPackts;
    int mCasPipSupport;
    int mCasFCCSupport;
    int mSecMemSize;
    std::string mCasType;
    int mDisableSubtitle;
private:
    void reset();

    template <typename T>
    void initProperty(const char* propertyName, T& value);

    void initLinux();

private:
    AmlMpConfig();
    AmlMpConfig(const AmlMpConfig&) = delete;
    AmlMpConfig& operator= (const AmlMpConfig&) = delete;
};
}


#endif
