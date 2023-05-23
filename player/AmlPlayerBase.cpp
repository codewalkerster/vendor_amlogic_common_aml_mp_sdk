/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlPlayerBase"
#include "utils/AmlMpLog.h"
#include <utils/AmlMpUtils.h>
#include "AmlPlayerBase.h"
#include "AmlTsPlayer.h"
#include "AmlDummyTsPlayer.h"
#ifdef HAVE_TUNER_HAL
#include "AmlTvPlayer.h"
#endif
#include "AmlCTCPlayer.h"

#ifdef ANDROID
#include <system/window.h>
#endif

#ifdef HAVE_SUBTITLE
#include <SubtitleNativeAPI.h>
#endif
#include "Aml_MP_PlayerImpl.h"



namespace aml_mp {

#ifdef HAVE_SUBTITLE
#define SUBTITLE_SOURCE_CREATE_ID (0)
wptr<AmlPlayerBase> AmlPlayerBase::sSubtitleCbHandle;
#endif

sptr<AmlPlayerBase> AmlPlayerBase::create(Aml_MP_PlayerCreateParams* createParams, int instanceId)
{
    sptr<AmlPlayerBase> player;
#ifdef HAVE_TUNER_HAL
    if (createParams->options & AML_MP_OPTION_PREFER_TUNER_HAL ||
            AmlMpConfig::instance().mPreferTunerHal == 1) {
        player = new AmlTvPlayer(createParams, instanceId);
    } else
#endif

#ifdef ANDROID
    if (createParams->options & AML_MP_OPTION_PREFER_CTC) {
        player = new AmlCTCPlayer(createParams, instanceId);
    } else
#endif
        if (createParams->options & AML_MP_OPTION_DVR_PLAYBACK) {
            player = new AmlDummyTsPlayer(createParams, instanceId);
        } else if (createParams->channelId == AML_MP_CHANNEL_ID_MAIN ||
        !AmlMpPlayerRoster::instance().isAmTsPlayerExist() ||
        isSupportMultiHwDemux() ||
        AmlMpConfig::instance().mTsPlayerNonTunnel) {
        player = new AmlTsPlayer(createParams, instanceId);
    } else {
#ifdef ANDROID
        player = new AmlCTCPlayer(createParams, instanceId);
#endif
    }

    return player;
}

AmlPlayerBase::AmlPlayerBase(Aml_MP_PlayerCreateParams* createParams, int instanceId)
:mInstanceId(instanceId)
, mEventCb(nullptr)
, mUserData(nullptr)
, mSubtitleParams{AML_MP_INVALID_PID, AML_MP_CODEC_UNKNOWN, AML_MP_CODEC_UNKNOWN, 0, 0, 0, {0}}//default CC
,mCreateParams(createParams)
{
    AML_MP_UNUSED(mCreateParams);

    snprintf(mName, sizeof(mName), "%s_%d", LOG_TAG, mInstanceId);

#ifdef HAVE_SUBTITLE
    //Set a default size for subtitle window parament.
    mSubWindowX = 0;
    mSubWindowY = 0;
    mSubWindowWidth = 1920;
    mSubWindowHeight = 1080;

    memset(&mSubtitleData, 0, sizeof(mSubtitleData));
    memset(&mSubtitleDimension, 0, sizeof(mSubtitleDimension));
#endif
}

AmlPlayerBase::~AmlPlayerBase()
{
#ifdef HAVE_SUBTITLE
    if (mSubtitleHandle) {
        amlsub_Destroy(mSubtitleHandle);
        mSubtitleHandle = nullptr;
    }

#ifdef __linux__
#ifndef ANDROID
    if (mSubSourceHandle != nullptr) {
        SubSource_Stop(mSubSourceHandle);
        SubSource_Destroy(mSubSourceHandle);
        mSubSourceHandle = nullptr;
    }
#endif
#endif

#endif//HAVE_SUBTITLE
}

int AmlPlayerBase::registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    mEventCb = cb;
    mUserData = userData;

    return 0;
}

int AmlPlayerBase::start()
{
#ifdef HAVE_SUBTITLE
    startSubtitleDecoding();
#endif

    return 0;
}

int AmlPlayerBase::stop()
{
#ifdef HAVE_SUBTITLE
    stopSubtitleDecoding();
#endif

    return 0;
}

int AmlPlayerBase::flush() {
#ifdef HAVE_SUBTITLE
    AmlSubtitleStatus ret = amlsub_Reset(mSubtitleHandle);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_Reset failed! %d", ret);
        return -1;
    }

#ifdef __linux__
#ifndef ANDROID
    if (mSubSourceHandle != nullptr) {
        ret = SubSource_Reset(mSubSourceHandle);
        if (ret != SUB_STAT_OK) {
            MLOGE("SubSource_Reset failed! %d", ret);
            return -1;
        }
    }
#endif
#endif

#endif//HAVE_SUBTITLE
    return 0;
}

int AmlPlayerBase::setSubtitleParams(const Aml_MP_SubtitleParams* params)
{
    MLOGI("setSubtitleParams");
    mSubtitleParams = *params;

    return 0;
}

int AmlPlayerBase::switchSubtitleTrack(const Aml_MP_SubtitleParams* params)
{
#ifdef HAVE_SUBTITLE
    stopSubtitleDecoding();
    setSubtitleParams(params);
    startSubtitleDecoding();
#else
    AML_MP_UNUSED(params);
#endif
    return 0;
}

int AmlPlayerBase::showSubtitle()
{
    MLOGI("showSubtitle");

#ifdef HAVE_SUBTITLE
    RETURN_IF(0, mSubtitleHandle == nullptr);

    AmlSubtitleStatus ret = amlsub_UiShow(mSubtitleHandle);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_UiShow failed! %d", ret);
        return -1;
    }
#endif
    return 0;
}

int AmlPlayerBase::hideSubtitle()
{
    MLOGI("hideSubtitle");

#ifdef HAVE_SUBTITLE
    RETURN_IF(0, mSubtitleHandle == nullptr);

    AmlSubtitleStatus ret = amlsub_UiHide(mSubtitleHandle);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_UiHide failed!");
        return -1;
    }
#endif

    return 0;
}

#ifdef HAVE_SUBTITLE
bool AmlPlayerBase::constructAmlSubtitleParam(AmlSubtitleParam* amlSubParam, Aml_MP_SubtitleParams* params)
{
    bool ret = true;

    switch (params->subtitleCodec) {
    case AML_MP_SUBTITLE_CODEC_CC:
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_CLOSED_CAPTION;
        break;

    case AML_MP_SUBTITLE_CODEC_SCTE27:
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_SCTE27;
        break;

    case AML_MP_SUBTITLE_CODEC_DVB:
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_DVB;
        break;

    case AML_MP_SUBTITLE_CODEC_TELETEXT:
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_DVB_TELETEXT;
        break;

    case AML_MP_SUBTITLE_CODEC_TTML:// TTML Subtitle Support
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_TTML;
        break;
    case AML_MP_SUBTITLE_CODEC_ASS:
    case AML_MP_SUBTITLE_CODEC_SUBRIP:
        amlSubParam->subtitleType = AmlSubtitletype::TYPE_SUBTITLE_SSA;//Subtitles have reused these formats
        break;
    default:
        ret = false;
        break;
    }

    amlSubParam->pid = params->pid;
    amlSubParam->videoFormat = params->videoFormat;
    amlSubParam->channelId = params->channelId;
    amlSubParam->ancillaryPageId = params->ancillaryPageId;
    amlSubParam->compositionPageId = params->compositionPageId;
    return ret;
}

#ifdef __linux__
#ifndef ANDROID
CodecID AmlPlayerBase::mpSubtitleCodec2CodecID(Aml_MP_CodecID codecId)
{
    switch (codecId) {
        case AML_MP_SUBTITLE_CODEC_CC: return CodecID::CODEC_ID_EIA_608;
        case AML_MP_SUBTITLE_CODEC_SCTE27: return CodecID::CODEC_ID_DVB_SUBTITLE;//no suitable value
        case AML_MP_SUBTITLE_CODEC_DVB: return CodecID::CODEC_ID_DVB_SUBTITLE;
        case AML_MP_SUBTITLE_CODEC_TELETEXT: return CodecID::CODEC_ID_DVB_TELETEXT;
        case AML_MP_SUBTITLE_CODEC_ASS: return CodecID::CODEC_ID_SSA;
        case AML_MP_SUBTITLE_CODEC_SUBRIP: return CodecID::CODEC_ID_SSA;
        // todo, wait for subtitle confirm subtitle type name
        case AML_MP_SUBTITLE_CODEC_TTML: return CodecID::CODEC_ID_SMPTE_TTML_SUBTITLE;
        default:
            MLOGI("unknown Aml_MP_CodecID, use default CodecID");
            return CodecID::CODEC_ID_DVB_SUBTITLE;
    }
}
#endif
#endif

#endif//HAVE_SUBTITLE

int AmlPlayerBase::startSubtitleDecoding()
{
    MLOGI("startSubtitleDecoding");

#ifdef HAVE_SUBTITLE
    AmlSubtitleParam subParam{};
    subParam.ioSource = AmlSubtitleIOType::E_SUBTITLE_DEMUX;
    subParam.dmxId = mCreateParams->demuxId;
    if (!constructAmlSubtitleParam(&subParam, &mSubtitleParams)) {
        MLOGI("unknown subtitle codec, not start subtitle");
        return 0;
    }

    if (mSubtitleHandle == nullptr) {
        mSubtitleHandle = amlsub_Create();
    }

    if (mSubtitleHandle == nullptr) {
        MLOGE("mSubtitleHandle is NULL");
        return -1;
    }

#ifdef __linux__
#ifndef ANDROID
    //send es subtitle through SubSource_SendData
    if (mCreateParams->sourceType == AML_MP_INPUT_SOURCE_ES_MEMORY && mSubSourceHandle == nullptr) {
        int sessionId = -1;

        amlsub_GetSessionId(mSubtitleHandle, &sessionId);

        subParam.ioSource = AmlSubtitleIOType::E_SUBTITLE_SOCK;
        MLOGI("es mode, create mSubSourceHandle:%d, sessionId=%d, ioSource:%d, mCreateParams.options:0x%" PRIx64 "", (AML_MP_INPUT_SOURCE_ES_MEMORY), sessionId, subParam.ioSource, mCreateParams->options);
        mSubSourceHandle = SubSource_Create(sessionId < 0 ? SUBTITLE_SOURCE_CREATE_ID : sessionId);
        if (mSubSourceHandle == nullptr) {
            MLOGE("mSubSourceHandle is NULL");
            return -1;
        }

    }
#endif
#endif

    sSubtitleCbHandle = this;
#ifdef ANDROID
    amlsub_RegistOnDataCB(mSubtitleHandle, AmlMPSubtitleDataCb);
#else
    if (mCreateParams->options & AML_MP_OPTION_REPORT_SUBTITLE_DATA) {
        amlsub_RegistOnDataCB(mSubtitleHandle, AmlMPSubtitleDataCb);
    }

#endif
    amlsub_RegistOnSubtitleAvailCb(mSubtitleHandle, AmlMPSubtitleAvailCb);
    amlsub_RegistGetDimensionCb(mSubtitleHandle, AmlMPSubtitleDimensionCb);
    amlsub_RegistAfdEventCB(mSubtitleHandle, AmlMPSubtitleAfdEventCb);
    amlsub_RegistOnChannelUpdateCb(mSubtitleHandle, AmlMPSubtitleChannelUpdateCb);
    amlsub_RegistOnSubtitleLanguageCb(mSubtitleHandle, AmlMPSubtitleLanguageCb);
    amlsub_RegistOnSubtitleInfoCB(mSubtitleHandle, AmlMPSubtitleInfoCb);

    AmlSubtitleStatus ret = amlsub_Open(mSubtitleHandle, &subParam);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_Open failed!");
    }

    if (mSubtitleParams.subtitleCodec == AML_MP_SUBTITLE_CODEC_CC) {
        //only cc set it
        amlsub_SetPip(mSubtitleHandle, MODE_SUBTITLE_PIP_PLAYER, getPlayerId());
        MLOGI("cc setpip for subtitle playerId");
    }

#ifdef __linux__
#ifndef ANDROID
    //ReportType
    if (mSubSourceHandle != nullptr) {
        if (SUB_STAT_OK != SubSource_ReportType(mSubSourceHandle, subParam.subtitleType)) {
            MLOGE("SubSource_ReportType failed");
        }
        MLOGI("mSubSourceHandle, reportType:%d", subParam.subtitleType);
    }
#endif
#endif

    if (AmlMpConfig::instance().mTsPlayerNonTunnel == 1) {
        int mediasyncId = getMediaSyncId();

        //we get mediasyncid in NonTunnelMode but video initialization time is 200~300ms
        if (mediasyncId >= 0) {
            MLOGI("nontunnel mode setpip for subtitle,mediasyncId:%d\n",mediasyncId);
            amlsub_SetPip(mSubtitleHandle, MODE_SUBTITLE_PIP_MEDIASYNC, mediasyncId);
        } else {
            MLOGI("setpip for subtitle, mediasyncId=-1, get the first frame event and set it again\n");
        }
    }

    showSubtitle();//SubtitleServer: Error, no default fallback display registered!
    MLOGI("Subtitle size is x:%d, y: %d, width: %d, height: %d", mSubWindowX, mSubWindowY, mSubWindowWidth, mSubWindowHeight);
    ret = amlsub_UiSetSurfaceViewRect(mSubtitleHandle, mSubWindowX, mSubWindowY, mSubWindowWidth, mSubWindowHeight);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_UiSetSurfaceViewRect failed!");
    }

#endif
    return 0;
}

int AmlPlayerBase::stopSubtitleDecoding()
{
    MLOGI("stopSubtitleDecoding");

#ifdef HAVE_SUBTITLE
    RETURN_IF(-1, mSubtitleHandle == nullptr);

    if (sSubtitleCbHandle == this) {
        sSubtitleCbHandle = nullptr;
    }

    hideSubtitle();

    AmlSubtitleStatus ret = amlsub_Close(mSubtitleHandle);
    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_Close failed!");

        //subtitle bug, not return -1
        //return -1;
    }

#ifdef __linux__
#ifndef ANDROID
    //the connection to the server will be disconnected
    if (mSubSourceHandle != nullptr) {
        if (SUB_STAT_OK != SubSource_Stop(mSubSourceHandle)) {
            MLOGE("SubSource_Stop failed");
        }
        SubSource_Destroy(mSubSourceHandle);
        mSubSourceHandle = nullptr;
        MLOGE("SubSource_Destroy.");
    }
#endif
#endif

#endif//HAVE_SUBTITLE

    return 0;
}

int AmlPlayerBase::setSubtitleWindow(int x, int y, int width, int height)
{
    MLOGI("setSubtitleWindow");
    MLOGI("param x: %d, y: %d, width: %d, height: %d", x, y, width, height);
    mSubWindowX = x;
    mSubWindowY = y;
    mSubWindowWidth = width;
    mSubWindowHeight = height;

#ifdef HAVE_SUBTITLE
    RETURN_IF(0, mSubtitleHandle == nullptr);

    AmlSubtitleStatus ret = amlsub_UiSetSurfaceViewRect(mSubtitleHandle, mSubWindowX, mSubWindowY, mSubWindowWidth, mSubWindowHeight);

    if (ret != SUB_STAT_OK) {
        MLOGE("amlsub_UiSetSurfaceViewRect failed!");
        return -1;
    }
#endif

    return 0;
}

int AmlPlayerBase::setParameter(Aml_MP_PlayerParameterKey key, void* parameter) {
#ifdef HAVE_SUBTITLE
    AmlSubtitleStatus ret = SUB_STAT_INV;
    AmlTeletextCtrlParam amlTeletextCtrlParam;

    switch (key) {
        case AML_MP_PLAYER_PARAMETER_TELETEXT_CONTROL:
            amlTeletextCtrlParam = convertToTeletextCtrlParam((AML_MP_TeletextCtrlParam*)parameter);
            ret = amlsub_TeletextControl(mSubtitleHandle, &amlTeletextCtrlParam);
            break;
        default:
            ret = SUB_STAT_INV;
    }

    if (ret != SUB_STAT_OK) {
        return -1;
    }
#else
    AML_MP_UNUSED(key);
    AML_MP_UNUSED(parameter);
#endif
    return 0;
}

int AmlPlayerBase::writeEsData(Aml_MP_StreamType type, const uint8_t* buffer, size_t size, int64_t pts)
{
#ifdef __linux__
#ifndef ANDROID

#ifdef HAVE_SUBTITLE
    SubSourceStatus ret;
    if (mSubSourceHandle != nullptr) {
        ret = SubSource_SendData(mSubSourceHandle, reinterpret_cast<const char *>(buffer), size, pts, mpSubtitleCodec2CodecID(mSubtitleParams.subtitleCodec));
        //ret = SubSource_SendData(mSubSourceHandle, reinterpret_cast<const char *>(buffer), size);
        if (SUB_STAT_OK != ret)
        {
            MLOGE("SubSource_SendData failed, type:%d(%s), size:%zu, pts:%" PRId64 ", CodeId:%d", type, mpStreamType2Str(type), size, pts, mpSubtitleCodec2CodecID(mSubtitleParams.subtitleCodec));
        }

        MLOGI_IF(AmlMpConfig::instance().mLogMask & kDebugFlagSubtitle, "SubSource_SendData type:%d(%s), size:%zu, pts:%" PRId64", CodeId:0x%x, ret:%d", type, mpStreamType2Str(type), size, pts, mpSubtitleCodec2CodecID(mSubtitleParams.subtitleCodec), ret);
    }

    return size;
#endif//HAVE_SUBTITLE

#endif
#endif

    AML_MP_UNUSED(type);
    AML_MP_UNUSED(buffer);
    AML_MP_UNUSED(size);
    AML_MP_UNUSED(pts);
    //MLOGI("TODO!!! %s, type:%d, buffer:%p, size:%d, pts:%lld", __FUNCTION__, type, buffer, size, pts);

    //always return size
    return size;
}

#ifdef HAVE_SUBTITLE
void AmlPlayerBase::AmlMPSubtitleDataCb(const char * data, int size, AmlSubDataType type,
                                            int x, int y, int width, int height, int videoWidth,
                                            int videoHeight, int showing) {
    ALOG(LOG_INFO, nullptr, "Call AmlMPSubtitleDataCb");
    if (sSubtitleCbHandle == nullptr) {
        return;
    }

    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }

    cbHandle->mSubtitleData.data = data;
    cbHandle->mSubtitleData.size = size;

    cbHandle->mSubtitleData.type = convertToMpSubtitleDataType(type);
    cbHandle->mSubtitleData.x = x;
    cbHandle->mSubtitleData.y = y;
    cbHandle->mSubtitleData.width = width;
    cbHandle->mSubtitleData.height = height;
    cbHandle->mSubtitleData.videoWidth = videoWidth;
    cbHandle->mSubtitleData.videoHeight = videoHeight;
    cbHandle->mSubtitleData.showing = showing;
    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_DATA, (int64_t)(&(cbHandle->mSubtitleData)));
}

#define ERROR_DECODER_NORMAL 1
#define ERROR_DECODER_TIMEOUT 2
#define ERROR_DECODER_LOSEDATA 3
#define ERROR_DECODER_INVALIDDATA 4
#define ERROR_DECODER_TIMEERROR 5

void AmlPlayerBase::AmlMPSubtitleAvailCb(int avail) {
    ALOG(LOG_INFO, nullptr, "Call AmlMPSubtitleAvailCb: %d", avail);
    if (sSubtitleCbHandle == nullptr) {
        return;
    }

    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }

    switch (avail) {
    case ERROR_DECODER_NORMAL:
        cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_AVAIL, (int64_t)(&avail));
        break;
    case ERROR_DECODER_TIMEOUT:
        cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_TIMEOUT, (int64_t)(&avail));
        break;
    case ERROR_DECODER_LOSEDATA:
        cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_LOSEDATA, (int64_t)(&avail));
        break;
    case ERROR_DECODER_INVALIDDATA:
        cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_INVALID_DATA, (int64_t)(&avail));
        break;
    case ERROR_DECODER_TIMEERROR:
        cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_INVALID_TIMESTAMP, (int64_t)(&avail));
        break;
    default:
        ALOG(LOG_INFO, nullptr, "Unhandled avail callback param: %d", avail);
        break;
    }
}

void AmlPlayerBase::AmlMPSubtitleDimensionCb(int width, int height) {
    if (sSubtitleCbHandle == nullptr) {
        return;
    }
    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }
    cbHandle->mSubtitleDimension.width = width;
    cbHandle->mSubtitleDimension.height = height;
    ALOG(LOG_INFO, nullptr, "[AmlMPSubtitleDimensionCb] Get call back info width: %d, height: %d\n", cbHandle->mSubtitleDimension.width, cbHandle->mSubtitleDimension.height);
    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_DIMENSION, (int64_t)(&(cbHandle->mSubtitleDimension)));
}

void AmlPlayerBase::AmlMPSubtitleAfdEventCb(int event) {
    ALOG(LOG_INFO, nullptr, "AmlMPSubtitleAfdEventCb: %d", event);
    if (sSubtitleCbHandle == nullptr) {
        return;
    }
    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }

    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_AFD_EVENT, (int64_t)(&event));
}

void AmlPlayerBase::AmlMPSubtitleChannelUpdateCb(int event, int id) {
    if (sSubtitleCbHandle == nullptr) {
        return;
    }
    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }
    ALOG(LOG_INFO, nullptr, "AmlMPSubtitleChannelUpdateCb [event: %d, id: %d]", event, id);

    Aml_MP_SubtitleChannelUpdate subtitleChannelUpdate;
    subtitleChannelUpdate.event = event;
    subtitleChannelUpdate.id = id;
    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_CHANNEL_UPDATE, (int64_t)(&subtitleChannelUpdate));
}

void AmlPlayerBase::AmlMPSubtitleLanguageCb(std::string lang) {
    if (sSubtitleCbHandle == nullptr) {
        return;
    }
    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }
    lang.copy(cbHandle->mSubtitleIso639Code, 3, 0);
    cbHandle->mSubtitleIso639Code[3] = '\0';
    ALOG(LOG_INFO, nullptr, "[AmlMPSubtitleLanguageCb] Get callback info iso639Code: %s\n", cbHandle->mSubtitleIso639Code);
    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_LANGUAGE, (int64_t)(&(cbHandle->mSubtitleIso639Code)));
}

void AmlPlayerBase::AmlMPSubtitleInfoCb(int what, int extra) {
    ALOG(LOG_INFO, nullptr, "AmlMPSubtitleInfoCb [what: %d, extra: %d]", what, extra);
    if (sSubtitleCbHandle == nullptr) {
        return;
    }
    sptr<AmlPlayerBase> cbHandle = sSubtitleCbHandle.promote();
    if (cbHandle == nullptr) {
        return;
    }

    Aml_MP_SubtitleInfo subtitleInfo;
    subtitleInfo.what = what;
    subtitleInfo.extra = extra;
    cbHandle->notifyListener(AML_MP_PLAYER_EVENT_SUBTITLE_INFO, (int64_t)(&subtitleInfo));
}

#endif

///////////////////////////////////////////////////////////////////////////////
void AmlPlayerBase::notifyListener(Aml_MP_PlayerEventType eventType, int64_t param)
{
    if (mEventCb) {
        switch (eventType) {
            case AML_MP_PLAYER_EVENT_VIDEO_DECODE_FIRST_FRAME:
            {
#ifdef HAVE_SUBTITLE
                if (mSubtitleHandle != nullptr && AmlMpConfig::instance().mTsPlayerNonTunnel == 1) {
                    int mediasyncId = getMediaSyncId();
                    //get the first frame event and set it again
                    MLOGI("nontunnel mode setpip for subtitle when first video decoded,mediasyncId:%d\n",mediasyncId);
                    amlsub_SetPip(mSubtitleHandle, MODE_SUBTITLE_PIP_MEDIASYNC, mediasyncId);
                }
#endif
                break;
            }
            default:
                break;
        }

        mEventCb(mUserData, eventType, param);
    } else {
        MLOGE("mEventCb is NULL! %s", mpPlayerEventType2Str(eventType));
    }
}

///////////////////////////////////////////////////////////////////////////////

}
