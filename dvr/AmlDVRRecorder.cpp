/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlDVRRecorder"
#include <utils/AmlMpLog.h>
#include "AmlDVRRecorder.h"
#include <Aml_MP/Dvr.h>
#include <utils/AmlMpHandle.h>
#include <cutils/properties.h>
#include <dvr_utils.h>
#include <utils/AmlMpUtils.h>
#if !defined (ANDROID) || ANDROID_PLATFORM_SDK_VERSION >= 30
#include <segment_dataout.h>
#endif

namespace aml_mp {
static void convertToMpDVRRecorderStatus(Aml_MP_DVRRecorderStatus* mpStatus, DVR_WrapperRecordStatus_t* dvrStatus);

///////////////////////////////////////////////////////////////////////////////
AmlDVRRecorder::AmlDVRRecorder(Aml_MP_DVRRecorderBasicParams* basicParams, Aml_MP_DVRRecorderTimeShiftParams* timeShiftParams, Aml_MP_DVRRecorderEncryptParams* encryptParams)
{
    snprintf(mName, sizeof(mName), "%s", LOG_TAG);
    MLOG();

    memset(&mRecOpenParams, 0, sizeof(DVR_WrapperRecordOpenParams_t));
    memset(&mRecStartParams, 0, sizeof(DVR_WrapperRecordStartParams_t));
    memset(&mRecordPids, 0, sizeof(mRecordPids));

    setBasicParams(basicParams);
    mIsOutData = mRecOpenParams.flags & AML_MP_DVRRECORDER_DATAOUT;

    if (basicParams->isTimeShift) {
        setTimeShiftParams(timeShiftParams);
    }

    if (mIsOutData) {
        setSharedParams(basicParams);
    }

    if (encryptParams != nullptr) {
        setEncryptParams(encryptParams);
    }

    mRecOpenParams.event_fn = [] (DVR_RecordEvent_t event, void* params, void* userData) {
        AmlDVRRecorder* recorder = static_cast<AmlDVRRecorder*>(userData);
        return recorder->eventHandler(event, params);
    };
    mRecOpenParams.event_userdata = this;

    int ret = dvr_wrapper_open_record(&mRecoderHandle, &mRecOpenParams);
    if (ret < 0) {
        MLOGE("Open dvr record fail");
    }
}

AmlDVRRecorder::~AmlDVRRecorder()
{
    MLOG();

    int ret = dvr_wrapper_close_record(mRecoderHandle);
    if (ret) {
        MLOGE("close recorder failed!");
    }
}

int AmlDVRRecorder::registerEventCallback(Aml_MP_DVRRecorderEventCallback cb, void* userData)
{
    AML_MP_TRACE(10);

    mEventCb = cb;
    mEventUserData = userData;

    return 0;
}

int AmlDVRRecorder::setStreams(Aml_MP_DVRStreamArray* streams)
{
    MLOGI("Recording PIDs:");

    int count = 0;
    DVR_StreamPid_t pids[DVR_MAX_RECORD_PIDS_COUNT];
    memset(pids, 0, sizeof(pids));
    for (int i = 0; i < streams->nbStreams; ++i) {
        MLOGI("streamType:%d(%s), codecId:%d(%s), pid:%d", streams->streams[i].type,
                mpStreamType2Str(streams->streams[i].type),
                streams->streams[i].codecId,
                mpCodecId2Str(streams->streams[i].codecId),
                streams->streams[i].pid);

        switch (streams->streams[i].type) {
        case AML_MP_STREAM_TYPE_VIDEO:
            pids[count].type =  (DVR_StreamType_t)(convertToDVRStreamType(streams->streams[i].type) << 24 |
                convertToDVRVideoFormat(streams->streams[i].codecId));
            pids[count].pid = streams->streams[i].pid;
            count++;
            MLOGI("  VIDEO 0x%x", streams->streams[i].pid);
            break;

        case AML_MP_STREAM_TYPE_AUDIO:
        case AML_MP_STREAM_TYPE_AD:
            pids[count].type =  (DVR_StreamType_t)(convertToDVRStreamType(streams->streams[i].type) << 24 |
                convertToDVRAudioFormat(streams->streams[i].codecId));
            pids[count].pid = streams->streams[i].pid;
            count++;
            MLOGI("  AUDIO 0x%x", streams->streams[i].pid);
            break;

        case AML_MP_STREAM_TYPE_SUBTITLE:
            pids[count].type = (DVR_StreamType_t)(convertToDVRStreamType(streams->streams[i].type) << 24 |
                streams->streams[i].codecId);
            pids[count].pid = streams->streams[i].pid;
            count++;
            MLOGI("  SUBTITLE 0x%x", streams->streams[i].pid);
            break;

        case AML_MP_STREAM_TYPE_TELETEXT:
            pids[count].type = (DVR_StreamType_t)(convertToDVRStreamType(streams->streams[i].type) << 24 |
                streams->streams[i].codecId);
            pids[count].pid = streams->streams[i].pid;
            count++;
            MLOGI("  TELETEXT 0x%x", streams->streams[i].pid);
            break;

        case AML_MP_STREAM_TYPE_SECTION:
            pids[count].type = (DVR_StreamType_t)(convertToDVRStreamType(streams->streams[i].type) << 24);
            pids[count].pid = streams->streams[i].pid;
            count++;
            MLOGI("  SECTION 0x%x", streams->streams[i].pid);
            break;

        default:
            MLOGI("  Not recording 0x%x, type %s", streams->streams[i].pid, mpStreamType2Str(streams->streams[i].type));
            break;
        }
    }

    MLOG("request nbStreams:%d, actual count:%d, mStarted:%d", streams->nbStreams, count, mStarted);

    //update streams
    if (mStarted) {
        DVR_WrapperUpdatePidsParams_t updatePidParams;
        updatePidParams.nb_pids = count;
        memcpy(updatePidParams.pids, pids, sizeof(updatePidParams.pids));
        for (size_t i = 0; i < updatePidParams.nb_pids; ++i) {
            updatePidParams.pid_action[i] = DVR_RECORD_PID_CREATE;
        }

        for (size_t i = 0; i < mRecordPids.nb_pids; ++i) {
            bool found = false;

            for (size_t j = 0; j < updatePidParams.nb_pids; ++j) {
                if (updatePidParams.pids[j].pid == mRecordPids.pids[i].pid) {
                    found = true;
                    updatePidParams.pid_action[j] = DVR_RECORD_PID_KEEP;
                    MLOGI("keep 0x%x", mRecordPids.pids[i].pid);

                    break;
                }
            }

            if (!found) {
                //not found this pid ,so need del this pid.
                if (updatePidParams.nb_pids < AML_MP_DVR_STREAMS_COUNT-1) {
                    updatePidParams.pid_action[updatePidParams.nb_pids] = DVR_RECORD_PID_CLOSE;
                    updatePidParams.pids[updatePidParams.nb_pids].pid = mRecordPids.pids[i].pid;
                    updatePidParams.pids[updatePidParams.nb_pids].type = mRecordPids.pids[i].type;
                    updatePidParams.nb_pids++;
                    MLOGI("close 0x%x", mRecordPids.pids[i].pid);
                }
            }
        }

        int ret =dvr_wrapper_update_record_pids(mRecoderHandle, &updatePidParams);
        if (ret < 0) {
            MLOGE("Update record pids fail");

            //if update pids failed, we don't update mRecordPids.
            return -1;
        }
    }

    //save current pids info
    mRecordPids.nb_pids = count;
    memcpy(mRecordPids.pids, pids, sizeof(mRecordPids.pids));

    //save/update start pids info
    mRecStartParams.pids_info.nb_pids = count;
    memcpy(mRecStartParams.pids_info.pids, pids, sizeof(mRecStartParams.pids_info.pids));

    return 0;
}

int AmlDVRRecorder::start()
{
    int ret = 0;
    MLOGI("Call DVRRecorderStart");

#if !defined (ANDROID) || ANDROID_PLATFORM_SDK_VERSION >= 30
    if (mIsOutData) {
        Segment_DataoutCallback_t share_cb = { mSharedCb, mSharedUserData};
        MLOGI("DVRRecorder Start ioctl AML_MP_SEGMENT_DATAOUT_CMD_SET_CALLBACK");
        dvr_wrapper_ioctl_record(mRecoderHandle, SEGMENT_DATAOUT_CMD_SET_CALLBACK, &share_cb , sizeof(share_cb));
    }
#endif
    if (mSecureBuffer != nullptr) {
        MLOGI("set secureBuffer:%p, secureBufferSize:%zu", mSecureBuffer, mSecureBufferSize);
        dvr_wrapper_set_record_secure_buffer(mRecoderHandle, mSecureBuffer, mSecureBufferSize);
    }

    if (mRecOpenParams.crypto_fn) {
        MLOGI("set cryptoFn:%p, cryptoData:%p", mRecOpenParams.crypto_fn, mRecOpenParams.crypto_data);
        dvr_wrapper_set_record_decrypt_callback(mRecoderHandle,
                mRecOpenParams.crypto_fn,
                mRecOpenParams.crypto_data);
    }

    if (mRecStartParams.pids_info.nb_pids > 0) {
        ret = dvr_wrapper_start_record(mRecoderHandle, &mRecStartParams);
        if (ret == DVR_SUCCESS) {
            mStarted = true;
        } else {
            MLOGE("Failed to start recording.");
            return -1;
        }
    } else {
        MLOGE("Need set start params before start");
    }

    return 0;
}

int AmlDVRRecorder::stop()
{
    MLOGI("Call DVRRecorderStop");

    int ret = dvr_wrapper_stop_record(mRecoderHandle);
    if (ret == DVR_SUCCESS) {
        mStarted = false;
    } else {
        MLOGI("Stop recoder failed");
    }

    return ret;
}

int AmlDVRRecorder::pause()
{
    MLOGI("Call DVRRecorderPause");

    int ret = dvr_wrapper_pause_record(mRecoderHandle);
    if (ret) {
        MLOGI("Pause recoder fail");
    }
    return ret;
}

int AmlDVRRecorder::resume()
{
    MLOGI("Call DVRRecorderResume");

    int ret = dvr_wrapper_resume_record(mRecoderHandle);
    if (ret) {
        MLOGI("resume recoder fail");
    }
    return ret;
}
int AmlDVRRecorder::getStatus(Aml_MP_DVRRecorderStatus* status)
{
    DVR_WrapperRecordStatus_t dvrStatus;
    int ret = dvr_wrapper_get_record_status(mRecoderHandle, &dvrStatus);
    if (ret < 0) {
        MLOGW("get record status failed!");
        return -1;
    }

    convertToMpDVRRecorderStatus(status, &dvrStatus);
    return 0;
}

int AmlDVRRecorder::isSecureMode() const
{
    return dvr_wrapper_record_is_secure_mode(mRecoderHandle);
}

int AmlDVRRecorder::setEncryptParams(Aml_MP_DVRRecorderEncryptParams* encryptParams)
{
    mRecOpenParams.crypto_period.interval_bytes = encryptParams->intervalBytes;
    mRecOpenParams.crypto_period.notify_clear_periods = encryptParams->notifyClearPeriods;
    mRecOpenParams.crypto_fn = (DVR_CryptoFunction_t)encryptParams->cryptoFn;
    mRecOpenParams.crypto_data = encryptParams->cryptoData;

    mSecureBuffer = encryptParams->secureBuffer;
    mSecureBufferSize = encryptParams->secureBufferSize;

    mRecOpenParams.clearkey = encryptParams->clearKey;
    mRecOpenParams.cleariv = encryptParams->clearIV;
    mRecOpenParams.keylen = encryptParams->keyLength;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
int AmlDVRRecorder::setBasicParams(Aml_MP_DVRRecorderBasicParams* basicParams)
{
    mRecOpenParams.fend_dev_id = basicParams->fend_dev_id;
    mRecOpenParams.dmx_dev_id = basicParams->demuxId;
    memcpy(&(mRecOpenParams.location), &(basicParams->location), DVR_MAX_LOCATION_SIZE);
    mRecOpenParams.segment_size = basicParams->segmentSize;
    mRecOpenParams.flags = (DVR_RecordFlag_t)basicParams->flags;
    mRecOpenParams.flush_size = basicParams->bufferSize;
    mRecOpenParams.ringbuf_size = basicParams->ringbufSize;
    mRecOpenParams.force_sysclock = basicParams->forceSysClock;
    MLOGI("location:%s", basicParams->location);

    if (basicParams->appendMode) {
        mRecStartParams.save_rec_file = basicParams->appendMode;
        MLOGI("save_rec_file=%d with same location", basicParams->appendMode);
    }

    return 0;
}
int AmlDVRRecorder::setSharedParams(Aml_MP_DVRRecorderBasicParams* basicParams)
{
    mSharedCb = (Aml_MP_CB_Data)basicParams->dataCBFn;
    mSharedUserData = basicParams->cryptoData;
    MLOGI("setSharedParams set CB");
    return 0;
}

int AmlDVRRecorder::setTimeShiftParams(Aml_MP_DVRRecorderTimeShiftParams* timeShiftParams)
{
    mRecOpenParams.max_size = timeShiftParams->maxSize;
    mRecOpenParams.max_time = timeShiftParams->maxTime;
    mRecOpenParams.is_timeshift = DVR_TRUE;

    return 0;
}

DVR_Result_t AmlDVRRecorder::eventHandler(DVR_RecordEvent_t event, void* params)
{
    DVR_Result_t ret = DVR_SUCCESS;
    Aml_MP_DVRRecorderStatus mpStatus;
    convertToMpDVRRecorderStatus(&mpStatus, (DVR_WrapperRecordStatus_t*)params);

    switch (event) {
    case DVR_RECORD_EVENT_ERROR:
        if (mEventCb)  mEventCb(mEventUserData, AML_MP_DVRRECORDER_EVENT_ERROR, (int64_t)&mpStatus);
        break;

    case DVR_RECORD_EVENT_STATUS:
        if (mEventCb)  mEventCb(mEventUserData, AML_MP_DVRRECORDER_EVENT_STATUS, (int64_t)&mpStatus);
        break;

    case DVR_RECORD_EVENT_SYNC_END:
        if (mEventCb)  mEventCb(mEventUserData, AML_MP_DVRRECORDER_EVENT_SYNC_END, (int64_t)&mpStatus);
        break;

    case DVR_RECORD_EVENT_CRYPTO_STATUS:
        if (mEventCb)  mEventCb(mEventUserData, AML_MP_DVRRECORDER_EVENT_CRYPTO_STATUS, (int64_t)&mpStatus);
        break;

    case DVR_RECORD_EVENT_WRITE_ERROR:
        if (mEventCb)  mEventCb(mEventUserData, AML_MP_DVRRECORDER_EVENT_WRITE_ERROR, (int64_t)&mpStatus);
        break;

    default:
        MLOG("unhandled event:%d", event);
        break;
    }

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
static Aml_MP_DVRRecorderState convertToMpDVRRecordState(DVR_RecordState_t state)
{
    switch (state) {
    case DVR_RECORD_STATE_OPENED:
        return AML_MP_DVRRECORDER_STATE_OPENED;

    case DVR_RECORD_STATE_STARTED:
        return AML_MP_DVRRECORDER_STATE_STARTED;

    case DVR_RECORD_STATE_STOPPED:
        return AML_MP_DVRRECORDER_STATE_STOPPED;

    case DVR_RECORD_STATE_CLOSED:
    default:
        return AML_MP_DVRRECORDER_STATE_CLOSED;
    }
}

static void convertToMpDVRRecorderStatus(Aml_MP_DVRRecorderStatus* mpStatus, DVR_WrapperRecordStatus_t* dvrStatus)
{
    mpStatus->state = convertToMpDVRRecordState(dvrStatus->state);
    convertToMpDVRSourceInfo(&mpStatus->info, &dvrStatus->info);
    mpStatus->streams.nbStreams = dvrStatus->pids.nb_pids;
    for (int i = 0; i < mpStatus->streams.nbStreams; i++) {
        convertToMpDVRStream(&mpStatus->streams.streams[i], &dvrStatus->pids.pids[i]);
    }
    convertToMpDVRSourceInfo(&mpStatus->infoObsolete, &dvrStatus->info_obsolete);
}


}
