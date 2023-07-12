/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlMpDVR"

#include <Aml_MP/Dvr.h>
#include "AmlDVRPlayer.h"
#include "AmlDVRRecorder.h"
#include "utils/AmlMpUtils.h"
#include "utils/AmlMpHandle.h"
#include <dvr_segment.h>
#include <dvr_wrapper.h>
#include <dvr_utils.h>

using namespace aml_mp;

static const char* mName = LOG_TAG;

int Aml_MP_DVRRecorder_Create(Aml_MP_DVRRecorderCreateParams* createParams, AML_MP_DVRRECORDER* handle)
{
    RETURN_IF(-1, createParams == nullptr);

    AmlDVRRecorder* recorder(new AmlDVRRecorder(&createParams->basicParams, &createParams->timeshiftParams, &createParams->encryptParams));
    if (recorder == nullptr) {
        MLOGE("new AmlDVRRecorder failed!");
        return -1;
    }

    recorder->incStrong(recorder);

    *handle = aml_handle_cast(recorder);

    return 0;
}

int Aml_MP_DVRRecorder_Destroy(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> dvrRecorder = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, dvrRecorder == nullptr);

    dvrRecorder->decStrong(recorder);

    return 0;
}

int Aml_MP_DVRRecorder_RegisterEventCallback(AML_MP_DVRRECORDER recorder, Aml_MP_DVRRecorderEventCallback cb, void* userData) {
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    int ret;
    RETURN_IF(-1, amlMpHandle == nullptr);

    ret = amlMpHandle->registerEventCallback(cb, userData);

    return ret;
}

int Aml_MP_DVRRecorder_SetStreams(AML_MP_DVRRECORDER recorder, Aml_MP_DVRStreamArray* streams)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->setStreams(streams);
    return ret;
}


int Aml_MP_DVRRecorder_Start(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->start();

    return ret;
}

int Aml_MP_DVRRecorder_Pause(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->pause();
    return ret;
}

int Aml_MP_DVRRecorder_Resume(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->resume();
    return ret;
}

int Aml_MP_DVRRecorder_Stop(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->stop();
    return ret;
}

int Aml_MP_DVRRecorder_GetStatus(AML_MP_DVRRECORDER recorder, Aml_MP_DVRRecorderStatus* status)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->getStatus(status);

    return ret;
}

int Aml_MP_DVRRecorder_IsSecureMode(AML_MP_DVRRECORDER recorder)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->isSecureMode();

    return ret;
}

int Aml_MP_DVRRecorder_SetEncryptParams(AML_MP_DVRRECORDER recorder, Aml_MP_DVRRecorderEncryptParams* encryptParams)
{
    sptr<AmlDVRRecorder> amlMpHandle = aml_handle_cast<AmlDVRRecorder>(recorder);
    RETURN_IF(-1, amlMpHandle == nullptr);

    int ret = amlMpHandle->setEncryptParams(encryptParams);

    return ret;
}

int Aml_MP_DVRRecorder_GetSegmentList(const char* location, uint32_t* segmentNums, uint64_t** segmentIds)
{
    return dvr_segment_get_list(location, segmentNums, segmentIds);
}

int Aml_MP_DVRRecorder_GetSegmentInfo(const char* location, uint64_t segmentId, Aml_MP_DVRSegmentInfo* segmentInfo)
{
    DVR_RecordSegmentInfo_t info;
    uint32_t pidNums;
    memset(&info, 0, sizeof(info));
    int ret = dvr_segment_get_info(location, segmentId, &info);

    if (ret != DVR_SUCCESS) {
        MLOGI("get segment info fail, ret: %d", ret);
        return ret;
    }

    memset(segmentInfo, 0, sizeof(*segmentInfo));
    segmentInfo->id = info.id;
    segmentInfo->streams.nbStreams = info.nb_pids;
    pidNums = info.nb_pids < AML_MP_DVR_STREAMS_COUNT ? info.nb_pids: AML_MP_DVR_STREAMS_COUNT;
    MLOGD("nb_pids:%d", pidNums);
    for (size_t i = 0; i < pidNums; ++i) {
        convertToMpDVRStream(&segmentInfo->streams.streams[i], &info.pids[i]);

        MLOGD("streamType:%d, pid:%d, codecId:%d(%s)", segmentInfo->streams.streams[i].type,
                segmentInfo->streams.streams[i].pid,
                segmentInfo->streams.streams[i].codecId,
                mpCodecId2Str(segmentInfo->streams.streams[i].codecId));
    }

    segmentInfo->duration = info.duration;
    segmentInfo->size = info.size;
    segmentInfo->nbPackets = info.nb_packets;

    return ret;
}

int Aml_MP_DVRRecorder_DeleteSegment(const char* location, uint64_t segmentId)
{
    return dvr_segment_delete(location, segmentId);
}

int Aml_MP_DVRRecorder_DeleteRecordFile (const char *location)
{
    if (location == NULL) {
        MLOGE("Aml_MP_DVRRecorder_DeleteRecordFile location was NULL!");
        return -1;
    }
    int ret = dvr_wrapper_segment_del_by_location(location);
    if (ret < 0) {
        MLOGE("Aml_MP_DVRRecorder_DeleteRecordFile failed!");
    }
    return ret;
}

int Aml_MP_DVRRecorder_GetRecordFileInfo (const char *location, Aml_MP_DVRRecodFileInfo *p_info)
{
    if (location == NULL || p_info == NULL) {
        MLOGE("Aml_MP_DVRRecorder_GetRecordFileInfo parameters was NULL!");
        return -1;
    }
    int ret = dvr_wrapper_segment_get_info_by_location(location, (DVR_WrapperInfo_t *)p_info);
    if (ret < 0) {
        MLOGE("Aml_MP_DVRRecorder_GetRecordFileInfo failed!");
    }
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
int Aml_MP_DVRPlayer_Create(Aml_MP_DVRPlayerCreateParams* createParams, AML_MP_DVRPLAYER* handle)
{
    RETURN_IF(-1, createParams == nullptr);

    AmlDVRPlayer* player(new AmlDVRPlayer(&createParams->basicParams, &createParams->decryptParams));
    if (player == nullptr) {
        MLOGE("new AmlDVRPlayer failed!");
        return -1;
    }

    player->incStrong(player);
    *handle = aml_handle_cast(player);

    return 0;
}

int Aml_MP_DVRPlayer_Destroy(AML_MP_DVRPLAYER player)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    dvrPlayer->decStrong(player);

    return 0;
}

int Aml_MP_DVRPlayer_RegisterEventCallback(AML_MP_DVRPLAYER player, Aml_MP_PlayerEventCallback cb, void* userData)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->registerEventCallback(cb, userData);

    return ret;
}

int Aml_MP_DVRPlayer_SetStreams(AML_MP_DVRPLAYER player, Aml_MP_DVRStreamArray* streams)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setStreams(streams);

    return ret;
}

int Aml_MP_DVRPlayer_OnlySetStreams(AML_MP_DVRPLAYER player, Aml_MP_DVRStreamArray* streams)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->onlySetStreams(streams);

    return ret;
}

int Aml_MP_DVRPlayer_Start(AML_MP_DVRPLAYER player, bool initialPaused)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->start(initialPaused);

    return ret;
}

int Aml_MP_DVRPlayer_Stop(AML_MP_DVRPLAYER player)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->stop();

    return ret;
}

int Aml_MP_DVRPlayer_Pause(AML_MP_DVRPLAYER player)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->pause();

    return ret;
}

int Aml_MP_DVRPlayer_Resume(AML_MP_DVRPLAYER player)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->resume();

    return ret;
}

int Aml_MP_DVRPlayer_SetLimit(AML_MP_DVRPLAYER player, uint32_t time, uint32_t limit)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);
    int ret = dvrPlayer->setLimit(time, limit);

    return ret;
}

int Aml_MP_DVRPlayer_Seek(AML_MP_DVRPLAYER player, int timeOffset)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->seek(timeOffset);

    return ret;
}

int Aml_MP_DVRPlayer_SetPlaybackRate(AML_MP_DVRPLAYER player, float rate)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setPlaybackRate(rate);

    return ret;
}

int Aml_MP_DVRPlayer_GetStatus(AML_MP_DVRPLAYER player, Aml_MP_DVRPlayerStatus* status)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->getStatus(status);

    return ret;
}

int Aml_MP_DVRPlayer_ShowVideo(AML_MP_DVRPLAYER handle)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->showVideo();

    return ret;
}

int Aml_MP_DVRPlayer_HideVideo(AML_MP_DVRPLAYER handle)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->hideVideo();

    return ret;
}

int Aml_MP_DVRPlayer_SetVolume(AML_MP_DVRPLAYER handle, float volume)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setVolume(volume);

    return ret;
}

int Aml_MP_DVRPlayer_GetVolume(AML_MP_DVRPLAYER handle, float* volume)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->getVolume(volume);

    return ret;
}

int Aml_MP_DVRPlayer_SetParameter(AML_MP_DVRPLAYER handle, Aml_MP_PlayerParameterKey key, void* parameter)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setParameter(key, parameter);

    return ret;
}

int Aml_MP_DVRPlayer_GetParameter(AML_MP_DVRPLAYER handle, Aml_MP_PlayerParameterKey key, void* parameter)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->getParameter(key, parameter);

    return ret;
}

int Aml_MP_DVRPlayer_SetANativeWindow(AML_MP_DVRPLAYER handle, ANativeWindow* nativeWindow)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setANativeWindow(nativeWindow);

    return ret;
}

int Aml_MP_DVRPlayer_SetVideoWindow(AML_MP_DVRPLAYER handle, int32_t x, int32_t y, int32_t width, int32_t height)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setVideoWindow(x, y, width, height);

    return ret;
}

int Aml_MP_DVRPlayer_SetADVolume(AML_MP_DVRPLAYER handle, float volume)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->setADVolume(volume);

    return ret;
}

int Aml_MP_DVRPlayer_GetADVolume(AML_MP_DVRPLAYER handle, float* volume)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(handle);
    RETURN_IF(-1, dvrPlayer == nullptr);

    int ret = dvrPlayer->getADVolume(volume);
    return ret;
}

int Aml_MP_DVRPlayer_GetMpPlayerHandle(AML_MP_DVRPLAYER player, AML_MP_PLAYER* handle)
{
    sptr<AmlDVRPlayer> dvrPlayer = aml_handle_cast<AmlDVRPlayer>(player);
    RETURN_IF(-1, dvrPlayer == nullptr);
    int ret = dvrPlayer->getMpPlayerHandle(handle);
    return ret;
}
