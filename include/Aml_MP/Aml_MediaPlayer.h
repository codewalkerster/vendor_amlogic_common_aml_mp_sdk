/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_MP_MEDIAPLAYER_H_
#define _AML_MP_MEDIAPLAYER_H_

/**
 * all interfaces need to be thread safe and nonblocking.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "Common.h"
///////////////////////////////////////////////////////////////////////////////
//                                  Player                                   //
///////////////////////////////////////////////////////////////////////////////
typedef void* AML_MP_MEDIAPLAYER;

typedef Aml_MP_AVSyncSource Aml_MP_MediaPlayerAVSyncSource;
typedef Aml_MP_PlayerEventType Aml_MP_MediaPlayerEventType;
typedef void (*Aml_MP_MediaPlayerEventCallback)(void* userData, Aml_MP_MediaPlayerEventType event, int64_t param);

///////////////////////////////////////////////////////////////////////////////
typedef enum {
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_UNKNOWN = -1,

    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_BAD_REQUEST = 0x1000,  //400 Bad Request
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_UNAUTHORIZED,          //401 Unauthorized
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_FORBIDDEN,             //403 Forbidden
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_NOT_FOUND,             //404 Not Found
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_OTHER_4XX,             //Other 4xx status codes
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_SERVER_ERROR,          //5xx status codes (Server Error)

    AML_MP_MEDIAPLAYER_MEDIA_ERROR_ENOENT      = 0x2000,  //No such file or directory
    AML_MP_MEDIAPLAYER_MEDIA_ERROR_ETIMEDOUT,             //Timed out
} Aml_MP_MediaPlayerMediaErrorType;

///////////////////////////////////////////////////////////////////////////////
#define AML_MP_MAX_STREAM_PARAMETER_SIZE 64
#define AML_MP_MAX_STREAMS_COUNT 64
#define AML_MP_MAX_MEDIA_PARAMETER_SIZE 256

typedef struct {
    uint16_t                index;
    uint16_t                id;
    Aml_MP_CodecID          videoCodec;
    uint32_t                width;
    uint32_t                height;
    char mine[AML_MP_MAX_STREAM_PARAMETER_SIZE];
    long reserved[8];
} Aml_MP_VideoTrackInfo;

typedef struct {
    uint16_t                index;
    uint16_t                id;
    Aml_MP_CodecID          audioCodec;
    uint32_t                nChannels;
    uint32_t                nSampleRate;
    char mine[AML_MP_MAX_STREAM_PARAMETER_SIZE];
    char language[AML_MP_MAX_STREAM_PARAMETER_SIZE];
    long reserved[8];
} Aml_MP_AudioTrackInfo;

typedef struct {
    uint16_t                index;
    uint16_t                id;
    Aml_MP_CodecID subtitleCodec;
    char mine[AML_MP_MAX_STREAM_PARAMETER_SIZE];
    char language[AML_MP_MAX_STREAM_PARAMETER_SIZE];
    long reserved[8];
} Aml_MP_SubTrackInfo;

typedef struct {
    Aml_MP_StreamType streamType;
    union {
        Aml_MP_VideoTrackInfo vInfo;
        Aml_MP_AudioTrackInfo aInfo;
        Aml_MP_SubTrackInfo   sInfo;
    } u;
    long reserved[8];
} Aml_MP_StreamInfo;

typedef enum {
    AML_MP_MEDIAPLAYER_INVOKE_ID_BASE = 0x100,
    AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO,            //getTrackInfo(..., Aml_MP_TrackInfo)
    AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO,            //getMediaInfo(..., Aml_MP_MediaInfo)
    AML_MP_MEDIAPLAYER_INVOKE_ID_SELECT_TRACK,              //setSelectTrack(int32_t, ...)
    AML_MP_MEDIAPLAYER_INVOKE_ID_UNSELECT_TRACK,            //setUnselectTrack(int32_t, ...)
} Aml_MP_MediaPlayerInvokeID;

////////////////////////////////////////
//AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO
typedef struct {
    int nb_streams;
    Aml_MP_StreamInfo streamInfo[AML_MP_MAX_STREAMS_COUNT];
    long reserved[8];
} Aml_MP_TrackInfo;

////////////////////////////////////////
//AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO
typedef struct {
    char    filename[AML_MP_MAX_MEDIA_PARAMETER_SIZE];
    int64_t duration;
    int64_t file_size;
    int64_t bitrate;
    int     nb_streams;
    int64_t curVideoIndex;
    int64_t curAudioIndex;
    int64_t curSubIndex;
    long reserved[8];
} Aml_MP_MediaInfo;

////////////////////////////////////////
//Aml_MP_MediaPlayerInvokeRequest
typedef struct {
    Aml_MP_MediaPlayerInvokeID requestId;
    union {
    int32_t data32;
    int64_t data64;
    } u;
    long reserved[8];
} Aml_MP_MediaPlayerInvokeRequest;

////////////////////////////////////////
//Aml_MP_MediaPlayerInvokeReply
typedef struct {
    Aml_MP_MediaPlayerInvokeID requestId;
    union {
        int32_t data32;
        int64_t data64;
        Aml_MP_TrackInfo trackInfo;
        Aml_MP_MediaInfo mediaInfo;
    } u;
    long reserved[8];
} Aml_MP_MediaPlayerInvokeReply;

///////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
typedef enum {
    //set/get
    AML_MP_MEDIAPLAYER_PARAMETER_SET_BASE  = 0x6000,
    AML_MP_MEDIAPLAYER_PARAMETER_ONLYHINT_TYPE,                        //setOnlyHintType(Aml_MP_MediaPlayerOnlyHintType*)
    AML_MP_MEDIAPLAYER_PARAMETER_VIDEO_TUNNEL_ID,                      //setVideoTunnelID(int*)
    AML_MP_MEDIAPLAYER_PARAMETER_CHANNEL_ID,                           //0:main,others:pip
    AML_MP_MEDIAPLAYER_PARAMETER_ID3_INFO,                             //getID3Info(Aml_MP_MediaPlayerID3Info*)
} Aml_MP_MediaPlayerParameterKey;

//AML_MP_MEDIAPLAYER_PARAMETER_ONLYHINT_TYPE
typedef enum {
    AML_MP_MEDIAPLAYER_ONLYNONE       = 0,
    AML_MP_MEDIAPLAYER_VIDEO_ONLYHIT  = (1 << 0),
    AML_MP_MEDIAPLAYER_AUDIO_ONLYHIT  = (1 << 1),
} Aml_MP_MediaPlayerOnlyHintType;

////////////////////////////////////////////
//Aml_MP_MediaPlayerID3Info
typedef struct
{
    char title[40];                                                 //MP3_ID3_title
    char author[40];                                                //MP3_ID3_artist
    char album[40];                                                 //MP3_ID3_album
    char comment[40];                                               //MP3_ID3_comment
    char year[4];                                                   //MP3_ID3_year
    int track;                                                      //MP3_ID3_track
    char genre[32];                                                 //MP3_ID3_genre
    char copyright[40];                                             //MP3_ID3_copyright
    long reserved[8];
} Aml_MP_MediaPlayerID3Info;

///////////////////////////////////////////////////////////////////////////////
//********************* BASIC INTERFACES BEGIN **************************/
/**
 * \brief Aml_MP_MediaPlayer_Create
 * Create player
 *
 * \param [out] player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Create(AML_MP_MEDIAPLAYER* handle);

/**
 * \brief Aml_MP_MediaPlayer_Destroy
 * Release and destroy player
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Destroy(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_SetVideoWindow
 * Set video window.
 * if set nativeWindow, video window set will not effect.
 *
 * \param [in]  player handle
 * \param [in]  video location (x)
 * \param [in]  video location (y)
 * \param [in]  video width
 * \param [in]  video height
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetVideoWindow(AML_MP_MEDIAPLAYER handle, int32_t x, int32_t y, int32_t width, int32_t height);

/**
 * \brief Aml_MP_MediaPlayer_SetDataSource
 * Set data source.
 *
 * \param [in]  player handle
 * \param [in]  playback url
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetDataSource(AML_MP_MEDIAPLAYER handle, const char *uri);

/**
 * \brief Aml_MP_MediaPlayer_Prepare
 * Prepare player in sync mode
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Prepare(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_PrepareAsync
 * Prepare player in async mode
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_PrepareAsync(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_Start
 * Start player
 * Please make sure have set playback url call this function.
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Start(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_SeekTo
 * Seek player
 *
 * \param [in]  player handle
 * \param [in]  target time
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SeekTo(AML_MP_MEDIAPLAYER handle, int msec);

/**
 * \brief Aml_MP_MediaPlayer_Pause
 * Pause player
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Pause(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_Resume
 * Resume player
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Resume(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_Stop
 * Stop player
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Stop(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_Reset
 * Reset player
 *
 * \param [in]  player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Reset(AML_MP_MEDIAPLAYER handle);

/**
 * \brief Aml_MP_MediaPlayer_GetCurrentPosition
 * Get current position
 *
 * \param [in]   player handle
 * \param [out]  current position
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_GetCurrentPosition(AML_MP_MEDIAPLAYER handle, int* msec);

/**
 * \brief Aml_MP_MediaPlayer_GetDuration
 * Get duration
 *
 * \param [in]   player handle
 * \param [out]  duration
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_GetDuration(AML_MP_MEDIAPLAYER handle, int* msec);

/**
 * \brief Aml_MP_MediaPlayer_SetMute
 * Set mute
 *
 * \param [in]  player handle
 * \param [in]  mute if
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetMute(AML_MP_MEDIAPLAYER handle, bool mute);

/**
 * \brief Aml_MP_MediaPlayer_Invoke
 * Invoke a generic method on the player by using opaque parcels
 *
 * \param [in]   player handle
 * \param [in]   request value
 * \param [out]  reply value
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_Invoke(AML_MP_MEDIAPLAYER handle, Aml_MP_MediaPlayerInvokeRequest* request, Aml_MP_MediaPlayerInvokeReply* reply);

/**
 * \brief Aml_MP_MediaPlayer_SetParameter
 * Set expand player parameter
 *
 * \param [in]  player handle
 * \param [in]  player expand parameter key
 * \param [in]  player expand parameter value
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetParameter(AML_MP_MEDIAPLAYER handle, Aml_MP_MediaPlayerParameterKey key, void* parameter);

/**
 * \brief Aml_MP_MediaPlayer_GetParameter
 * Get expand player parameter
 *
 * \param [in]  player handle
 * \param [in]  player expand parameter key
 * \param [out] player expand parameter value
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_GetParameter(AML_MP_MEDIAPLAYER handle, Aml_MP_MediaPlayerParameterKey key, void* parameter);

/**
 * \brief Aml_MP_MediaPlayer_RegisterEventCallBack
 * Register event callback function
 *
 * \param [in]  player handle
 * \param [in]  callback function
 * \param [in]  user data
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_RegisterEventCallBack(AML_MP_MEDIAPLAYER handle, Aml_MP_MediaPlayerEventCallback cb, void* userData);

/**
 * \brief Aml_MP_MediaPlayer_SetVolume
 * Set playback volume
 *
 * \param [in]  player handle
 * \param [in]  volume [0.0 ~ 100.0]
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetVolume(AML_MP_MEDIAPLAYER handle, float volume);

/**
 * \brief Aml_MP_MediaPlayer_GetVolume
 * Get playback volume
 *
 * \param [in]   player handle
 * \param [out]  volume
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_GetVolume(AML_MP_MEDIAPLAYER handle, float* volume);

/**
 * \brief Aml_MP_MediaPlayer_SetPlaybackRate
 * Set playback rate
 *
 * \param [in]  player handle
 * \param [in]  rate
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetPlaybackRate(AML_MP_MEDIAPLAYER handle, float rate);

/**
 * \brief  Aml_MP_MediaPlayer_IsPlaying
 * Is Playing
 *
 * \param [in] player handle
 *
 * \return 0 if success
 */
bool Aml_MP_MediaPlayer_IsPlaying(AML_MP_MEDIAPLAYER handle);

/**
 * \brief  Aml_MP_MediaPlayer_ShowVideo
 * Show Video
 *
 * \param [in] player handle
 *
 * \return true if playing
 */
int Aml_MP_MediaPlayer_ShowVideo(AML_MP_MEDIAPLAYER handle);

/**
 * \brief  Aml_MP_MediaPlayer_HideVideo
 * Hide Video
 *
 * \param [in] player handle
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_HideVideo(AML_MP_MEDIAPLAYER handle);

/**
 * \brief  Aml_MP_MediaPlayer_SetAVSyncSource
 * Set AVSync Source
 *
 * \param [in] player handle
 * \param [in] sync   Source
 *
 * \return 0 if success
 */
int Aml_MP_MediaPlayer_SetAVSyncSource(AML_MP_MEDIAPLAYER handle, Aml_MP_MediaPlayerAVSyncSource syncSource);

/**
 * \brief Aml_MP_MediaPlayer_SetLooping
 *
 * \param [in] player handle
 * \param [in] loop
 *
 * \return
 */
int Aml_MP_MediaPlayer_SetLooping(AML_MP_MEDIAPLAYER handle, int loop);

#ifdef __cplusplus
}
#endif

#endif
