/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "demux/AmlTsParser.h"
#include "source/Source.h"
#include "TestModule.h"
#ifdef ANDROID
#include <system/window.h>
#include <utils/RefBase.h>
#endif
#include <Aml_MP/Aml_MP.h>

namespace aml_mp {

// for dvr (encrypt) playback
class DVRPlayback : public TestModule, public ISourceReceiver
{
public:
    DVRPlayback(const std::string& url, bool cryptoMode, Aml_MP_DemuxId demuxId, bool isTimeShift);
    ~DVRPlayback();
    #ifdef ANDROID
    void setANativeWindow(const android::sp<ANativeWindow>& window);
    #endif
    void registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData);
    int start(bool isSetStream = true);
    int stop();
    void signalQuit();
    int setVideoWindow(int x, int y, int width, int height);
    int setParameter(Aml_MP_PlayerParameterKey key, void* parameter);
    int setStreams();
    int getMpPlayerHandle(AML_MP_PLAYER* handle);

protected:
    virtual const Command* getCommandTable() const override;
    virtual void* getCommandHandle() const override;

private:
    std::string stripUrlIfNeeded(const std::string& url) const;
    int initDVRDecryptPlayback(Aml_MP_DVRPlayerDecryptParams& decryptParams);
    int uninitDVRDecryptPlayback();

private:
    const std::string mUrl;
    bool mCryptoMode;
    Aml_MP_DemuxId mDemuxId;
    AML_MP_DVRPLAYER mPlayer = AML_MP_INVALID_HANDLE;
    AML_MP_PLAYER mHandle = AML_MP_INVALID_HANDLE;
    Aml_MP_PlayerEventCallback mEventCallback = nullptr;
    void* mUserData = nullptr;

    AML_MP_CASSESSION mCasSession = nullptr;
    AML_MP_SECMEM mSecMem = nullptr;
    bool mDvrReplayInited = false;

private:
    DVRPlayback(const DVRPlayback&) = delete;
    DVRPlayback& operator=(const DVRPlayback&) = delete;
};
}
