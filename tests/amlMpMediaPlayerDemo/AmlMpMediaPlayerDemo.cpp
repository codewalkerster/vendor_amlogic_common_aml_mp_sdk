/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpMediaPlayerDemo"
#include <utils/AmlMpLog.h>
#include <utils/AmlMpUtils.h>
#include <unistd.h>
#include <getopt.h>
#include "TestUtils.h"
#include <Aml_MP/Aml_MediaPlayer.h>
#include <string>
#include <vector>
#include <inttypes.h>
#include <signal.h>
#include <thread>
#include "../amlMpTestSupporter/TestModule.h"
#include <../../mediaplayer/AmlMediaPlayerBase.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <termios.h>
#include <errno.h>
#include <iostream>
#include <stdexcept>
//srand
#include <stdlib.h>
#include <time.h>

///////////////////////////////////////////////////////////////////////////////
//
struct Argument;
struct AmlMpMediaPlayerDemo;
struct playerRoster;

//func
static int tty_reset(int fd);
static int tty_cbreak(int fd);

//Track info
static void printCurTrackInfo(Aml_MP_TrackInfo *trackInfo, Aml_MP_MediaInfo* mediaInfo, Aml_MP_StreamType streamType);
static int getNextTrack(Aml_MP_TrackInfo *trackInfo, Aml_MP_MediaInfo* mediaInfo, Aml_MP_StreamType streamType);
static void printMediaInfo(Aml_MP_MediaInfo *mediaInfo);

//Multi
static int multiPlaybackThread(int id, bool isMain);
static int createMultiPlayback(int id, AML_MP_MEDIAPLAYER* player, struct Argument* argument, AmlMpMediaPlayerDemo* commandsProcess);
static int destroyMultiPlayback(int id);
static void closeMultiPlayback(int id);
static void closeAllMultiPlayback();

//utils
static int isUrlValid(const std::string url, std::string& urlHead, std::string& urlFile);
static int startingPlayCheck();

//callback func
static const char* mediaPlayerMediaErrorType2Str(Aml_MP_MediaPlayerMediaErrorType type);
void demoCallback(void* userData, Aml_MP_MediaPlayerEventType event, int64_t param);

//info
static void showOption();
static void showUsage();

///////////////////////////////////////////////////////////////////////////////
//static
static const char* mName = LOG_TAG;
using namespace aml_mp;

///////////////////////////////////////////////////////////////////////////////
//define
#define WRESET   "\033[0m"
#define RED      "\033[31m"
#define GREEN    "\033[32m"

#define TTY_RESET_FD(fd)                                    \
    do {                                                    \
        if (tty_reset(fd) < 0) {                            \
            printf("\n tty_reset error\n");                 \
        }                                                   \
    } while (0)

#define TTY_CBREAK_FD(fd)                                   \
    do {                                                    \
        if (tty_cbreak(fd) < 0) {                           \
            printf("\n tty_cbreak error\n");                \
        }                                                   \
    } while (0)


static bool AMMP_DEBUG_MODE_ENABLE = false;
#define TERMINAL_DEBUG(fmt, ...)                                                \
    do {                                                                        \
        if (AMMP_DEBUG_MODE_ENABLE) {                                           \
            printf("[%s:%d] " fmt , __FUNCTION__, __LINE__, ##__VA_ARGS__);     \
        }                                                                       \
    } while (0)

#define TERMINAL_ERROR(fmt, ...)                                                                \
    do {                                                                                        \
        printf(RED "---------------------------ERROR---------------------------\n" WRESET);     \
        printf(RED "-----------------------------------------------------------\n" WRESET);     \
        printf(RED fmt "\n", ##__VA_ARGS__ WRESET);                                             \
        printf(RED "-----------------------------------------------------------\n" WRESET);     \
        printf(RED "---------------------------ERROR---------------------------\n" WRESET);     \
    } while (0)


#define MIN(A, B) ( (A) > (B) ? (B) : (A) )
#define MAX(A, B) ( (A) > (B) ? (A) : (B) )
#define MEDIAPLAYERDEMO_MAIN (0)
#define MEDIAPLAYERDEMO_PIP (1)

#define ENUM_TO_STR(e) case e: return TO_STR(e); break
#define TO_STR(v) #v

///////////////////////////////////////////////////////////////////////////////
//Argument
struct Argument
{
    std::string url;
    int x = 0;
    int y = 0;
    int viewWidth = 0;
    int viewHeight = 0;
    int channelId = -1;
    bool onlyVideo = false;
    bool onlyAudio = false;
    int tunnelId = -1;
    int loopMode = 0;
    bool noSignalHandler = 0;
};

///////////////////////////////////////////////////////////////////////////////
//playerRoster
#define MaxPlayerNum 4
struct playerRoster
{
    static playerRoster& instance() {
        static playerRoster sInstance;
        return sInstance;
    }

    int registerAll(AML_MP_MEDIAPLAYER player, int id, const struct Argument* argument, const struct AmlMpMediaPlayerDemo* demo) {
        if (id < 0 || id >= MaxPlayerNum || argument == NULL) {
            printf("%s invalid, id:%d, argument:%p, demo:%p \n", __FUNCTION__, id, argument, demo);
            return -1;
        }

        mLock.lock();

        mPlayers[id] = player;
        mPlayerArgument[id].url             = argument->url;
        mPlayerArgument[id].x               = argument->x;
        mPlayerArgument[id].y               = argument->y;
        mPlayerArgument[id].viewWidth       = argument->viewWidth;
        mPlayerArgument[id].viewHeight      = argument->viewHeight;
        mPlayerArgument[id].channelId       = argument->channelId;
        mPlayerArgument[id].onlyVideo       = argument->onlyVideo;
        mPlayerArgument[id].onlyAudio       = argument->onlyAudio;
        mPlayerArgument[id].tunnelId        = argument->tunnelId;
        mPlayerArgument[id].loopMode        = argument->loopMode;
        mPlayerArgument[id].noSignalHandler = argument->noSignalHandler;

        mPlayerPlayerDemo[id] = const_cast<struct AmlMpMediaPlayerDemo*>(demo);

        if (NULL != player) {
            ++mPlayerHandlerCount;
        }
        mLock.unlock();

        return id;
    }

    int registerPlayerArgument(int id, const struct Argument* argument) {
        if (id < 0 || id >= MaxPlayerNum || argument == NULL) {
            printf("%s invalid, id:%d, argument:%p \n", __FUNCTION__, id, argument);
            return -1;
        }

        mLock.lock();
        mPlayerArgument[id].url             = argument->url;
        mPlayerArgument[id].x               = argument->x;
        mPlayerArgument[id].y               = argument->y;
        mPlayerArgument[id].viewWidth       = argument->viewWidth;
        mPlayerArgument[id].viewHeight      = argument->viewHeight;
        mPlayerArgument[id].channelId       = argument->channelId;
        mPlayerArgument[id].onlyVideo       = argument->onlyVideo;
        mPlayerArgument[id].onlyAudio       = argument->onlyAudio;
        mPlayerArgument[id].tunnelId        = argument->tunnelId;
        mPlayerArgument[id].loopMode        = argument->loopMode;
        mPlayerArgument[id].noSignalHandler = argument->noSignalHandler;
        mLock.unlock();

        return id;
    }

    int registerPlayerThreadHandle(int id, std::thread thread) {
        if (id < 0 || id >= MaxPlayerNum) {
            printf("%s invalid, id:%d\n", __FUNCTION__, id);
            return -1;
        }

        mLock.lock();
        mThread[id] = std::move(thread);
        mLock.unlock();

        return id;
    }

    int unregisterPlayer(AML_MP_MEDIAPLAYER player) {
        if (player == NULL) {
            printf("%s player invalid, player=%p \n", __FUNCTION__, player);
            return -1;
        }

        mLock.lock();
        for (size_t i = 0; i < MaxPlayerNum; ++i) {
            if (player == mPlayers[i]) {
                mPlayers[i] = NULL;
                //memset(&mPlayerArgument[i], 0, sizeof(struct Argument));
                mPlayerPlayerDemo[i] = NULL;

                if (mPlayerHandlerCount > 0) {
                    --mPlayerHandlerCount;
                }
                break;
            }
        }
        mLock.unlock();

        return 0;
    }

    bool isMultiPlayerExist(int id) {
        bool isExist = false;
        if (id < 0 || id >= MaxPlayerNum ) {
            printf("%s invalid, id:%d,\n", __FUNCTION__, id);
            return isExist;
        }

        mLock.lock();

        if (NULL != mPlayers[id]) {
            isExist = true;
        }

        mLock.unlock();

        return isExist;
    }

    void getPlayerHandle(int id, AML_MP_MEDIAPLAYER *player) {
        if (id < 0 || id >= MaxPlayerNum) {
            printf("%s invalid, id:%d \n", __FUNCTION__, id);
            return;
        }

        mLock.lock();
        if (mPlayers[id] != NULL) {
            *player = mPlayers[id];
        }
        mLock.unlock();
    }

    void getPlayerArgument(int id, struct Argument** argument) {
        if (id < 0 || id >= MaxPlayerNum) {
            printf("%s invalid, id:%d \n", __FUNCTION__, id);
            return;
        }

        mLock.lock();
        *argument = (&mPlayerArgument[id]);
        mLock.unlock();
    }

    void getPlayerDemoHandle(int id, struct AmlMpMediaPlayerDemo** playerDemo) {
        if (id < 0 || id >= MaxPlayerNum) {
            printf("%s invalid, id:%d \n", __FUNCTION__, id);
            return;
        }

        mLock.lock();
        if (NULL != mPlayerPlayerDemo[id]) {
            *playerDemo = (mPlayerPlayerDemo[id]);
        }
        mLock.unlock();
    }

    void getPlayerThreadHandle(int id, std::thread** thread) {
        if (id < 0 || id >= MaxPlayerNum) {
            printf("%s invalid, id:%d \n", __FUNCTION__, id);
            return;
        }

        mLock.lock();
        *thread = (&mThread[id]);
        mLock.unlock();
    }

    int playerHandlerCount() {
        int count = 0;

        mLock.lock();
        count = mPlayerHandlerCount;
        mLock.unlock();

        return count;
    }

private:
    playerRoster() {
        for (int i = 0; i < MaxPlayerNum; ++i) {
            mPlayers[i] = NULL;
            mPlayerArgument[i].url             = "";
            mPlayerArgument[i].x               = -1;
            mPlayerArgument[i].y               = -1;
            mPlayerArgument[i].viewWidth       = -1;
            mPlayerArgument[i].viewHeight      = -1;
            mPlayerArgument[i].channelId       = -1;
            mPlayerArgument[i].onlyVideo       = false;
            mPlayerArgument[i].onlyAudio       = false;
            mPlayerArgument[i].tunnelId        = -1;
            mPlayerArgument[i].loopMode        = 0;
            mPlayerArgument[i].noSignalHandler = 0;
            mPlayerPlayerDemo[i] = NULL;

            mPlayerHandlerCount = 0;
        }
    }

    std::mutex mLock;
    int mPlayerHandlerCount = 0;
    AML_MP_MEDIAPLAYER mPlayers[MaxPlayerNum];
    struct Argument mPlayerArgument[MaxPlayerNum];
    struct AmlMpMediaPlayerDemo* mPlayerPlayerDemo[MaxPlayerNum];
    std::thread mThread[MaxPlayerNum];

    playerRoster(const playerRoster&) = delete;
    playerRoster& operator=(const playerRoster&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
//AmlMpMediaPlayerCommandProcessor
struct AmlMpMediaPlayerCommandProcessor : CommandProcessor
{
    AmlMpMediaPlayerCommandProcessor(const std::string& prompt = ">");
    ~AmlMpMediaPlayerCommandProcessor();

    virtual int fetchAndProcessCommands();

private:

    AmlMpMediaPlayerCommandProcessor(const AmlMpMediaPlayerCommandProcessor&) = delete;
    AmlMpMediaPlayerCommandProcessor& operator=(const AmlMpMediaPlayerCommandProcessor&) = delete;
};

AmlMpMediaPlayerCommandProcessor::AmlMpMediaPlayerCommandProcessor(const std::string& prompt)
    : CommandProcessor(prompt)
{
    MLOGI();
}

AmlMpMediaPlayerCommandProcessor::~AmlMpMediaPlayerCommandProcessor()
{
    MLOGI();
}

int AmlMpMediaPlayerCommandProcessor::fetchAndProcessCommands()
{
    bool prompt = false;
    std::string lastCommand = "-1";

    for (;;) {
        if (mInterrupter()) {
            break;
        }

        if (tcgetpgrp(0) != getpid()) {
            usleep(10 * 1000);
            continue;
        }

        if (prompt) {
            prompt = false;
            fprintf(stderr, "%s", mPrompt.c_str());
        }

        struct pollfd fds;
        fds.fd = STDIN_FILENO;
        fds.events = POLL_IN;
        fds.revents = 0;
        // change the wait time from 1s to 100ms to avoid
        // the situation that the signal change is not detected in time
        int ret = ::poll(&fds, 1, 100);
        if (ret < 0) {
            //printf("poll STDIN_FILENO failed! %d\n", -errno);
        } else if (ret > 0) {
            if (fds.revents & POLL_ERR) {
                MLOGE("poll error!");
                continue;
            } else if (!(fds.revents & POLL_IN)) {
                continue;
            }

            prompt = true;

            char buffer[100]{0};
            int len = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (len <= 0) {
                MLOGE("read failed! %d", -errno);
                continue;
            }

            if (len > -1)
                buffer[len] = '\0';

            {
                //debug
                TERMINAL_DEBUG("read buf:%s, %d, size:%d\n", buffer, buffer[0], len);
                int i = 0;
                for (i = 0; i < len; i++) {
                    TERMINAL_DEBUG("%c(%d)\n", buffer[i], buffer[i]);
                }
            }

            //check for whitespace characters
            std::string buf(buffer);
            size_t b = 0;
            size_t e = buf.size();
            while (b < e) {
                if (isspace(buf[b]) || iscntrl(buf[b])) ++b;
                else if (isspace(buf[e]) || iscntrl(buf[e])) --e;
                else break;
            }
            TERMINAL_DEBUG("read b:%zu, e:%zu\n", b, e);

            if (b < e) {
                buf = buf.substr(b, e - b + 1);
                lastCommand = buf;
            } else if (b == e) {
                //direct choice buf[0]
                buf = buf[0];
            } else {
                continue;
            }

            std::vector<std::string> args = split(buf);
            mCommandVisitor(args);
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//AmlMpMediaPlayerDemo
static const int forwardRateFactorMap[] = {
    8,
    16,
    32,
    64,
    1,
};

static const int rewindRateFactorMap[] = {
    -8,
    -16,
    -32,
    -64,
};

struct AmlMpMediaPlayerDemo : public TestModule
{
        AmlMpMediaPlayerDemo(int id);
        ~AmlMpMediaPlayerDemo();

        int fetchAndProcessCommands();
        virtual void* getCommandHandle() const;
        void setCommandHandle(void * handle);
        int waitPreparedEvent();
        void broadcast(bool result);
        static int installSignalHandler();

public:
        bool mQuitPending = false;
        int mId;

private:
        AML_MP_MEDIAPLAYER mPlayer = NULL;
        std::condition_variable mCondition;
        std::mutex mMutex;
        bool mPrepared = false;

protected:
        const TestModule::Command* getCommandTable() const;
        bool processCommand(const std::vector<std::string>& args);

private:
        //Rewind Forward
        char mRewindStateIndex = 0, mForwardStateIndex = 0;
        bool isRewindOrForward = false;
        std::string RewindOrForward = "";
        void resetRewindAndForwardState() { mRewindStateIndex = 0; mForwardStateIndex = 0; }

private:
        sptr<CommandProcessor> mCommandProcessor;
        static std::thread mSignalHandleThread;
};

std::thread AmlMpMediaPlayerDemo::mSignalHandleThread;

AmlMpMediaPlayerDemo::AmlMpMediaPlayerDemo(int id)
    : mId(id)
{
    MLOGI("mId:%d", mId);
}

AmlMpMediaPlayerDemo::~AmlMpMediaPlayerDemo()
{
    MLOGI();
}

int AmlMpMediaPlayerDemo::fetchAndProcessCommands()
{
    char promptBuf[50];
    snprintf(promptBuf, sizeof(promptBuf), "AmlMpMediaPlayerDemo (press 'h' for help) > ");

    mCommandProcessor = new AmlMpMediaPlayerCommandProcessor(promptBuf);
    if (mCommandProcessor == nullptr) {
        return -1;
    }

    mCommandProcessor->setCommandVisitor(std::bind(&AmlMpMediaPlayerDemo::processCommand, this, std::placeholders::_1));
    mCommandProcessor->setInterrupter([this]() {return mQuitPending;});
    return mCommandProcessor->fetchAndProcessCommands();
}

bool AmlMpMediaPlayerDemo::processCommand(const std::vector<std::string>& args)
{
    bool ret = true;
    std::string cmd = *args.begin();

    if (cmd == "q") {
        MLOGI("%s,%d set mQuitPending = true", __FUNCTION__, __LINE__);
        //mQuitPending = true;

        Argument* argument = nullptr;
        playerRoster::instance().getPlayerArgument(MEDIAPLAYERDEMO_MAIN, &argument);
        if (argument && argument->loopMode) {
            TERMINAL_DEBUG("q command, reset loop mode!\n");
            argument->loopMode = 0;
        }
        closeAllMultiPlayback();
    } else {
        if (isRewindOrForward && (RewindOrForward != cmd || (cmd.compare("<") && cmd.compare(">")))) {
            isRewindOrForward = false;
            resetRewindAndForwardState();
        }

        if (">" == cmd) {
            isRewindOrForward = true;
            RewindOrForward = cmd;
            mRewindStateIndex = 0;

            const_cast<std::vector<std::string>&>(args).emplace_back(std::to_string(forwardRateFactorMap[mForwardStateIndex]));
            if (++mForwardStateIndex >= sizeof(forwardRateFactorMap) / sizeof(forwardRateFactorMap[0])) {
                mForwardStateIndex = 0;
            }

        } else if ("<" == cmd) {
            isRewindOrForward = true;
            RewindOrForward = cmd;
            mForwardStateIndex = 0;

            const_cast<std::vector<std::string>&>(args).emplace_back(std::to_string(rewindRateFactorMap[mRewindStateIndex]));
            if (++mRewindStateIndex >= sizeof(rewindRateFactorMap) / sizeof(rewindRateFactorMap[0])) {
                mRewindStateIndex = 0;
            }
        }

        {//debug
            int i = 0, size = args.size();
            TERMINAL_DEBUG("processCommand: size:%d\n", size);
            for (auto i = args.begin(); i != args.end(); i++) {
                //std::cout << *i << ' ' << std::endl;
                std::string pStr = *i;
                TERMINAL_DEBUG("%s\n", pStr.c_str());
            }
        }

        TestModule::processCommand(args);
    }

    return ret;
}

static struct TestModule::Command g_commandTable[] = {
    {
        "h", 0, "help",
        [](AML_MP_MEDIAPLAYER player __unused, const std::vector<std::string>& args __unused) -> int {
            showOption();//TestModule::printCommands(g_commandTable, true);
            return 0;
        }
    },

    {
        "p", 0, "pause player",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Pause(player);
            printf("call pause player, ret:%d\n", ret);
            return ret;
        }
    },

    {
        "r", 0, "resume player",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Resume(player);
            printf("call resume player, ret:%d\n", ret);
            return ret;
        }
    },
/*
    {
        "u", 0, "set data source",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            printf("call setdatasource,url address: ");
            int ret=0;
            std::string urlStr;
            const char* url;

            try {
                std::getline (std::cin, urlStr);
                url = urlStr.c_str();
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
            }

            if (url < 0) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetDataSource(player, url);

exit:
            printf("call set data source:%s ret: %d\n", url, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },
*/
    {
        "d", 0, "hide video",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_HideVideo(player);
            printf("call hide video, ret : %d\n",ret);
            return ret;
        }
    },

    {
        "D", 0, "show video",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_ShowVideo(player);
            printf("call show video,ret : %d\n",ret);
            return ret;
        }
    },

    {
        "K", 0, "call seek",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            printf("call seek, press position [milliseconds] : ");
            int ret = 0;
            std::string msecStr;
            int msec = -1;

            try {
                std::getline (std::cin, msecStr);
                msec = std::stoi(msecStr);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
                msec = -1;
            }

            if (msec < 0) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SeekTo(player, msec);

exit:
            printf("call seek, msec:%d, ret:%d\n", msec, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "v", 0, "get volume",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            float volume;
            int ret = Aml_MP_MediaPlayer_GetVolume(player, &volume);
            printf("Current volume: %f, ret: %d\n", volume, ret);
            return ret;
        }
    },

    {
        "V", 0, "set volume",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            printf("set volume, press volume [0 ~ 100] : ");
            int ret = 0;
            std::string volumeStr;
            float volume = -1;

            try {
                std::getline (std::cin, volumeStr);
                volume = std::stof(volumeStr);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
                volume = -1;
            }

            if (volume < 0 || volume > 100) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetVolume(player, volume);

exit:
            printf("set volume: %f, ret: %d\n", volume, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "F", 0, "set Fast rate",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            printf("call set Fast rate, press rate [0.25/0.5/1/1.25/1.5/2] : ");
            int ret = 0;
            std::string rateStr;
            float rate = -1;

            try {
                std::getline (std::cin, rateStr);
                rate = std::stof(rateStr);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
                rate = -1;
            }

            if (rate < 0 || rate > 2) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetPlaybackRate(player, rate);

exit:
            printf("call set Fast rate, rate:%f, ret:%d\n", rate, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "W", 0, "set video window",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            int ret = 0;
            std::string xStr;
            std::string yStr;
            std::string widthStr;
            std::string heightStr;

            int x = -1;
            int y = -1;
            int width = -1;
            int height = -1;

            try {
                printf("call set video window, press X pos : ");
                std::getline (std::cin, xStr);
                printf("call set video window, press Y pos : ");
                std::getline (std::cin, yStr);
                printf("call set video window, press width pos : ");
                std::getline (std::cin, widthStr);
                printf("call set video window, press height pos : ");
                std::getline (std::cin, heightStr);

                x = std::stoi(xStr);
                y = std::stoi(yStr);
                width = std::stoi(widthStr);
                height = std::stoi(heightStr);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
                x = -1;
            }

            if (x < 0 || y < 0 || width < 0 || height < 0) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetVideoWindow(player, x, y, width, height);

exit:
            printf("call set video window, x:%d, y:%d, width:%d, height:%d, ret:%d\n",x, y, width, height, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "sOnlyHint", 0, "set only hint",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0;
            Aml_MP_MediaPlayerOnlyHintType type = (Aml_MP_MediaPlayerOnlyHintType)atoi(args.at(1).c_str());

            ret = Aml_MP_MediaPlayer_SetParameter(player, AML_MP_MEDIAPLAYER_PARAMETER_ONLYHINT_TYPE, (void*)&type);
            printf("AML_MP_MEDIAPLAYER_PARAMETER_ONLYHINT_TYPE set type: %d, ret: %d\n", type, ret);

            return ret;
        }
    },
/*
    {
        "y", 0, "set AVSync Source",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0;
            Aml_MP_MediaPlayerAVSyncSource syncSource;

            memset(&syncSource, 0, sizeof(Aml_MP_MediaPlayerAVSyncSource));
            syncSource = AML_MP_AVSYNC_SOURCE_VIDEO;
            ret = Aml_MP_MediaPlayer_SetAVSyncSource(player,syncSource);
            printf("AML_MP_AVSYNC_SOURCE_VIDEO set SyncSource: 0x%x,ret: %d\n\n", syncSource, ret);

            return ret;
        }
    },
*/
    {
        "M", 0, "set mute umute",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            printf("call set mute/umute, press mute/umute [1/0] : ");
            int ret = 0;
            std::string muteStr;
            int mute = -1;
            try {
                std::getline (std::cin, muteStr);
                mute = std::stoi(muteStr, NULL, 10);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
                mute = -1;
            }

            if (mute < 0 || mute > 1) {
                ret = -1;
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetMute(player, mute);

exit:
            printf("Set mute: %d, ret: %d\n", mute, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;

        }
    },

    {
        "<", 0, "set Rewind",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            int ret = -1;
            float rewind = 0;

            if (args.size() >= 2) {
                rewind = stof(args.at(1));
            }

            if (rewind >= 0) {
                goto exit;
            }

            if (-1 == rewind) {
                rewind = 1;//maybe we need speed=1
            }

            ret = Aml_MP_MediaPlayer_SetPlaybackRate(player, rewind);

exit:
            printf("call set Rewind, rateFactor:%f, ret:%d\n", rewind, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        ">", 0, "set Forward",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);

            int ret = -1;
            float forward = 0;

            if (args.size() >= 2) {
                forward = stof(args.at(1));
            }

            if (forward <= 0) {
                goto exit;
            }

            ret = Aml_MP_MediaPlayer_SetPlaybackRate(player, forward);

exit:
            printf("call set Forward, rateFactor:%f, ret:%d\n", forward, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "a", 0, "Get Audio Info Channels",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0;
            printf("\n\n Available Audios : \n");

            Aml_MP_MediaPlayerInvokeRequest trackRequest;
            Aml_MP_MediaPlayerInvokeReply trackReply;
            memset(&trackRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&trackReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            trackRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &trackRequest, &trackReply);

            Aml_MP_MediaPlayerInvokeRequest mediaRequest;
            Aml_MP_MediaPlayerInvokeReply mediaReply;
            memset(&mediaRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&mediaReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            mediaRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &mediaRequest, &mediaReply);

            printCurTrackInfo(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_AUDIO);

            return ret;
        }
    },

    {
        "A", 0, "Set Next Audio component",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = -1, index = -1;

            Aml_MP_MediaPlayerInvokeRequest trackRequest;
            Aml_MP_MediaPlayerInvokeReply trackReply;
            memset(&trackRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&trackReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            trackRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &trackRequest, &trackReply);

            Aml_MP_MediaPlayerInvokeRequest mediaRequest;
            Aml_MP_MediaPlayerInvokeReply mediaReply;
            memset(&mediaRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&mediaReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            mediaRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &mediaRequest, &mediaReply);

            index = getNextTrack(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_AUDIO);

            if (index < 0) {
                goto exit;
            }

            //TODO: selectTrack
            ret = 0;
            Aml_MP_MediaPlayerInvokeRequest selectrequest;
            Aml_MP_MediaPlayerInvokeReply selectreply;
            memset(&selectrequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&selectreply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));

            selectrequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_SELECT_TRACK;
            selectrequest.u.data32 = index;
            ret = Aml_MP_MediaPlayer_Invoke(player, &selectrequest, &selectreply);

exit:
            printf("Set Next Audio component, ret:%d, index:%d\n", ret, index);
            return ret;
        }
    },

    {
        "s", 0, "Get Subtitles Info channels",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0;
            printf("\n\n Available Subtitles : \n");

            Aml_MP_MediaPlayerInvokeRequest trackRequest;
            Aml_MP_MediaPlayerInvokeReply trackReply;
            memset(&trackRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&trackReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            trackRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &trackRequest, &trackReply);

            Aml_MP_MediaPlayerInvokeRequest mediaRequest;
            Aml_MP_MediaPlayerInvokeReply mediaReply;
            memset(&mediaRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&mediaReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            mediaRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &mediaRequest, &mediaReply);

            printCurTrackInfo(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_SUBTITLE);

            return ret;
        }
    },

    {
        "S", 0, "Set Next Subtitle component",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = -1, index = -1;

            Aml_MP_MediaPlayerInvokeRequest trackRequest;
            Aml_MP_MediaPlayerInvokeReply trackReply;
            memset(&trackRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&trackReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            trackRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &trackRequest, &trackReply);

            Aml_MP_MediaPlayerInvokeRequest mediaRequest;
            Aml_MP_MediaPlayerInvokeReply mediaReply;
            memset(&mediaRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&mediaReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            mediaRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &mediaRequest, &mediaReply);


            index = getNextTrack(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_SUBTITLE);
            if (index < 0) {
                goto exit;
            }

            //TODO: selectTrack
            ret = 0;

exit:
            printf("Set Next Subtitle component, ret:%d, index:%d\n", ret, index);
            return ret;

        }
    },

    {
        "n", 0, "get current position",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0, position = 0;
            ret = Aml_MP_MediaPlayer_GetCurrentPosition(player, &position);
            printf("Aml_MP_MediaPlayer_GetCurrentPosition get position: %d(ms), ret: %d\n", position, ret);

            return ret;
        }
    },

    {
        "N", 0, "get duration",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0, duration = 0;
            ret = Aml_MP_MediaPlayer_GetDuration(player, &duration);
            printf("Aml_MP_MediaPlayer_GetDuration get duration: %d(ms), ret: %d\n", duration, ret);

            return ret;
        }
    },

    {
        "i", 0, "invoke generic method",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = 0;

            Aml_MP_MediaPlayerInvokeRequest trackRequest;
            Aml_MP_MediaPlayerInvokeReply trackReply;
            memset(&trackRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&trackReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            trackRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &trackRequest, &trackReply);
            printf("\nAML_MP_MEDIAPLAYER_INVOKE_ID_GET_TRACK_INFO, reply id:0x%x ret: %d\n",trackReply.requestId, ret);

            Aml_MP_MediaPlayerInvokeRequest mediaRequest;
            Aml_MP_MediaPlayerInvokeReply mediaReply;
            memset(&mediaRequest, 0, sizeof(Aml_MP_MediaPlayerInvokeRequest));
            memset(&mediaReply, 0, sizeof(Aml_MP_MediaPlayerInvokeReply));
            mediaRequest.requestId = AML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO;
            ret = Aml_MP_MediaPlayer_Invoke(player, &mediaRequest, &mediaReply);
            printf("\nAML_MP_MEDIAPLAYER_INVOKE_ID_GET_MEDIA_INFO,reply id:0x%x ret: %d\n",mediaReply.requestId, ret);

            printCurTrackInfo(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_VIDEO);
            printCurTrackInfo(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_AUDIO);
            printCurTrackInfo(&trackReply.u.trackInfo, &mediaReply.u.mediaInfo, AML_MP_STREAM_TYPE_SUBTITLE);
            printMediaInfo(&mediaReply.u.mediaInfo);
            printf("call isplaying ret: %d\n", Aml_MP_MediaPlayer_IsPlaying(player));

            return ret;
        }
    },

    {
        "O", 0, "Open PIP",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            TTY_RESET_FD(STDIN_FILENO);
            int ret = -1;
            std::thread thread;
            struct Argument argument;

            printf("Open PIP url : ");
            argument.url = "-1";
            argument.x = 0;
            argument.y = 0;
            argument.viewWidth = 960;
            argument.viewHeight = 540;
            argument.channelId = MEDIAPLAYERDEMO_PIP;//maybe need demux=1
            argument.tunnelId = 1;
            argument.onlyVideo = false;
            argument.onlyAudio = false;

            try {
                std::getline (std::cin, argument.url);
            } catch (const std::invalid_argument & ia) {
                printf("Invalid argument: %s. \n", ia.what());
            }

            if (argument.url.compare("-1") == 0 || playerRoster::instance().isMultiPlayerExist(MEDIAPLAYERDEMO_PIP)) {
                ret = -1;
                goto exit;
            }

            playerRoster::instance().registerPlayerArgument(MEDIAPLAYERDEMO_PIP, &argument);//only register argument
            playerRoster::instance().registerPlayerThreadHandle(MEDIAPLAYERDEMO_PIP, std::thread(multiPlaybackThread, MEDIAPLAYERDEMO_PIP, false));

            ret = 0;
exit:
            printf("call Open PIP, id:%d ret:%d\n", argument.channelId, ret);
            TTY_CBREAK_FD(STDIN_FILENO);

            return ret;
        }
    },

    {
        "C", 0, "Close PIP",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = -1;

            closeMultiPlayback(MEDIAPLAYERDEMO_PIP);

            ret = 0;

            printf("call Close PIP, ret:%d\n", ret);
            return ret;
        }
    },
/*
    {
        "R", 0, "call reset",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Reset(player);
            printf("call reset ret: %d\n", ret);
            return ret;
        }
    },

    {
        "b", 0, "call prepare",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Prepare(player);
            printf("call prepare ret: %d\n", ret);
            return ret;
        }
    },

    {
        "T", 0, "call start",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Start(player);
            printf("call start ret: %d\n", ret);
            return ret;
        }
    },

    {
        "t", 0, "call stop",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_MediaPlayer_Stop(player);
            printf("call stop ret: %d\n", ret);
            return ret;
        }
    },
*/
    {
        "q", 0, "exit playback",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            printf("exit playback\n");
            return 0;
        }
    },

    {
        "\n", 0, "\n",
        [](AML_MP_MEDIAPLAYER player, const std::vector<std::string>& args __unused) -> int {
            printf("\n");
            return 0;
        }
    },

    {nullptr, 0, nullptr, nullptr}
};

const TestModule::Command* AmlMpMediaPlayerDemo::getCommandTable() const
{
    return g_commandTable;
}

void* AmlMpMediaPlayerDemo::getCommandHandle() const
{
    return mPlayer;
}

void AmlMpMediaPlayerDemo::setCommandHandle(void * handle)
{
    if (handle != NULL)
        mPlayer = handle;
}

int AmlMpMediaPlayerDemo::waitPreparedEvent()
{
    printf("waitPreparedEvent \n");

    std::unique_lock<std::mutex> autoLock(mMutex);
    mCondition.wait(autoLock);
    printf("waitPreparedEvent mPrepared:%d\n", mPrepared);

    return (mPrepared == true ? 0 : -1);
}

void AmlMpMediaPlayerDemo::broadcast(bool result)
{
    printf("broadcast result:%d\n", result);
    std::unique_lock<std::mutex> autoLock(mMutex);
    mPrepared = result;
    mCondition.notify_all();
}

int AmlMpMediaPlayerDemo::installSignalHandler()
{
    int ret = 0;
    sigset_t blockSet, oldMask;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGINT);
    sigaddset(&blockSet, SIGQUIT);
    ret = pthread_sigmask(SIG_SETMASK, &blockSet, &oldMask);
    if (ret != 0) {
        MLOGE("pthread_sigmask failed! %d", ret);
        return -1;
    }

    mSignalHandleThread = std::thread([blockSet, oldMask] {
        int signo;
        int err;

        for (;;) {
            err = sigwait(&blockSet, &signo);
            if (err != 0) {
                MLOGE("sigwait error! %d", err);
                exit(0);
            }
            printf("Signal Capture:%s\n", strsignal(signo));

            switch (signo) {
            case SIGINT:
            case SIGQUIT:
            {
                //quit
                //mQuitPending = true;
                Argument* argument = nullptr;
                playerRoster::instance().getPlayerArgument(MEDIAPLAYERDEMO_MAIN, &argument);
                if (argument && argument->loopMode) {
                    TERMINAL_DEBUG("reset loop mode!\n");
                    argument->loopMode = 0;
                }
                closeAllMultiPlayback();

            }
            break;

            default:
                break;
            }

            if (pthread_sigmask(SIG_SETMASK, &oldMask, nullptr) != 0) {
                MLOGE("restore sigmask failed!");
            }
        }
    });
    mSignalHandleThread.detach();

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//
static void printCurTrackInfo(Aml_MP_TrackInfo *trackInfo, Aml_MP_MediaInfo* mediaInfo, Aml_MP_StreamType streamType)
{
    if (trackInfo == NULL || mediaInfo == NULL)
        return;

    int index = 0;
    Aml_MP_TrackInfo *info = trackInfo;
    char isSelect[5] = " ";

    //printf("nb_streams:%d \n", info->nb_streams);
    for (index = 0; index < info->nb_streams; ++index) {
        memset(isSelect, 0, sizeof(isSelect));
        strcpy(isSelect, " ");

        if (info->streamInfo[index].streamType == streamType && streamType == AML_MP_STREAM_TYPE_VIDEO) {
            if (index == mediaInfo->curVideoIndex) {
                sprintf(isSelect, "%s", ">");
            }

            printf("   %s array index:%d, actual index:%d [video] id:%d, videoCodec:%d(%s), width:%d, height:%d, mine:%s\n"
                ,isSelect
                ,index
                ,info->streamInfo[index].u.vInfo.index
                ,info->streamInfo[index].u.vInfo.id
                ,info->streamInfo[index].u.vInfo.videoCodec
                ,mpCodecId2Str(info->streamInfo[index].u.vInfo.videoCodec)
                ,info->streamInfo[index].u.vInfo.width
                ,info->streamInfo[index].u.vInfo.height
                ,info->streamInfo[index].u.vInfo.mine);
        } else if (info->streamInfo[index].streamType == streamType && streamType == AML_MP_STREAM_TYPE_AUDIO) {
            if (index == mediaInfo->curAudioIndex) {
                sprintf(isSelect, "%s", ">");
            }

            printf("   %s array index:%d, actual index:%d [audio] id:%d, audioCodec:%d(%s), nChannels:%d, nSampleRate:%d, mine:%s, language:%s\n"
                ,isSelect
                ,index
                ,info->streamInfo[index].u.aInfo.index
                ,info->streamInfo[index].u.aInfo.id
                ,info->streamInfo[index].u.aInfo.audioCodec
                ,mpCodecId2Str(info->streamInfo[index].u.aInfo.audioCodec)
                ,info->streamInfo[index].u.aInfo.nChannels
                ,info->streamInfo[index].u.aInfo.nSampleRate
                ,info->streamInfo[index].u.aInfo.mine
                ,info->streamInfo[index].u.aInfo.language);
        } else if (info->streamInfo[index].streamType == streamType && streamType == AML_MP_STREAM_TYPE_SUBTITLE) {
            if (index == mediaInfo->curSubIndex) {
                sprintf(isSelect, "%s", ">");
            }

            printf("   %s array index:%d, actual index:%d [subtitle] id:%d, subtitleCodec:%d(%s), mine:%s, language:%s\n"
                ,isSelect
                ,index
                ,info->streamInfo[index].u.sInfo.index
                ,info->streamInfo[index].u.sInfo.id
                ,info->streamInfo[index].u.sInfo.subtitleCodec
                ,mpCodecId2Str(info->streamInfo[index].u.sInfo.subtitleCodec)
                ,info->streamInfo[index].u.sInfo.mine
                ,info->streamInfo[index].u.sInfo.language);
        } else {
            ;
        }
    }

    printf("\n");
}

static int getNextTrack(Aml_MP_TrackInfo *trackInfo, Aml_MP_MediaInfo* mediaInfo, Aml_MP_StreamType streamType)
{
    if (trackInfo == NULL || mediaInfo == NULL)
        return -1;

    Aml_MP_TrackInfo *info = trackInfo;
    bool isReachFirst = false;
    int preTrackIndex = streamType == AML_MP_STREAM_TYPE_VIDEO ? mediaInfo->curVideoIndex :
        (streamType == AML_MP_STREAM_TYPE_AUDIO ? mediaInfo->curAudioIndex : mediaInfo->curSubIndex);
    int findTrackIndex = preTrackIndex;

    if (info->nb_streams > 0) {
        do {
            if (++findTrackIndex >= info->nb_streams) {
                findTrackIndex = 0;

                if (isReachFirst) {
                    //printf("loop again break:%d \n", info->nb_streams);
                    findTrackIndex = preTrackIndex;
                    break;
                }

                isReachFirst = true;
            }

            if (streamType == info->streamInfo[findTrackIndex].streamType) {
                break;
            }
        } while (1);
    }
    return findTrackIndex;
}

static void printMediaInfo(Aml_MP_MediaInfo *mediaInfo)
{
    if (mediaInfo == NULL)
        return;

    int index = 0;
    Aml_MP_MediaInfo *info = mediaInfo;

    printf("[mediaInfo] filename:%s, duration:%" PRId64 ", file_size:%" PRId64 ", bitrate:%" PRId64 ", nb_streams:%d" \
            ", curVideoIndex:%" PRId64 ", curAudioIndex:%" PRId64 ", curSubIndex:%" PRId64 "\n"
            ,info->filename
            ,info->duration
            ,info->file_size
            ,info->bitrate
            ,info->nb_streams
            ,info->curVideoIndex
            ,info->curAudioIndex
            ,info->curSubIndex);
    printf("\n");
}

///////////////////////////////////////////////////////////////////////////////
//serial
static struct termios save_termios;
static int ttysavefd = -1;
static enum { RESET, RAW, CBREAK } ttystate = RESET;
static int tty_cbreak(int fd) /* put terminal into a cbreak mode */
{
    int err;
    struct termios buf;
    if (ttystate != RESET) {
        errno = EINVAL;
        return(-1);
    }
    if (tcgetattr(fd, &buf) < 0)
        return(-1);

    save_termios = buf; /* structure copy */
    /*
    * Echo off, canonical mode off.
    */
    buf.c_lflag &= ~(ECHO | ICANON);
    /*
    * Case B: 1 byte at a time, no timer.
    */
    buf.c_cc[VMIN] = 1;
    buf.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &buf) < 0)
        return(-1);
    /*
    * Verify that the changes stuck. tcsetattr can return 0 on
    * partial success.
    */
    if (tcgetattr(fd, &buf) < 0) {
        err = errno;
        tcsetattr(fd, TCSAFLUSH, &save_termios);
        errno = err;
        return(-1);
    }

    if ((buf.c_lflag & (ECHO | ICANON)) || buf.c_cc[VMIN] != 1 ||
    buf.c_cc[VTIME] != 0) {
        /*
        * Only some of the changes were made. Restore the
        * original settings.
        */
        tcsetattr(fd, TCSAFLUSH, &save_termios);
        errno = EINVAL;
        return(-1);
    }

    ttystate = CBREAK;
    ttysavefd = fd;

    return(0);
}

static int tty_reset(int fd) /* restore terminal’s mode */
{
    if (ttystate == RESET)
        return(0);
    if (tcsetattr(fd, TCSAFLUSH, &save_termios) < 0)
        return(-1);
    ttystate = RESET;

    return(0);
}

///////////////////////////////////////////////////////////////////////////////
//PIP/MAIN
static int multiPlaybackThread(int id, bool isMain)
{
    printf("\nAmlMpMediaPlayerDemo:%d ----------play enter! isMain:%d\n", id, isMain);

    int ret = -1;
    std::string urlHead;
    std::string urlFile;
    struct Argument* argument = NULL;
    AML_MP_MEDIAPLAYER player = NULL;

    playerRoster::instance().getPlayerArgument(id, &argument);
    if (argument == NULL || argument->url.empty() || 0 != isUrlValid(argument->url, urlHead, urlFile)) {
        showUsage();
        return ret;
    }

    sptr<AmlMpMediaPlayerDemo> commandsProcess = new AmlMpMediaPlayerDemo(id);
    if (commandsProcess == NULL) {
        return ret;
    }

    //create
    ret = createMultiPlayback(id, &player, argument, commandsProcess.get());
    if (ret < 0) {
        goto error;
    }

    if (isMain) {
        //commands process
        printf("\nAmlMpMediaPlayerDemo:%d ----------fetchAndProcessCommands\n", id);
        commandsProcess->setCommandHandle(player);
        commandsProcess->fetchAndProcessCommands();

    } else {
        //sleep
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (commandsProcess->mQuitPending == false);
    }

error:
    //destroy
    ret = destroyMultiPlayback(id);

    printf("\nAmlMpMediaPlayerDemo:%d ----------play exited!\n", id);

    return ret;
}

static int createMultiPlayback(int id, AML_MP_MEDIAPLAYER* player, struct Argument* argument, AmlMpMediaPlayerDemo* commandsProcess)
{
    int ret = -1;

    if (argument == NULL) {
        return -1;
    }

    //create
    Aml_MP_MediaPlayer_Create(player);

    //prre-registration
    playerRoster::instance().registerAll(*player, id, argument, commandsProcess);//register player/commandsProcess

    //only settings
    // channelid is used for pip function which start the demo in a different process.
    // it means different play channels,0 : main, else pip
    if (argument->channelId != -1)
        Aml_MP_MediaPlayer_SetParameter(*player, AML_MP_MEDIAPLAYER_PARAMETER_CHANNEL_ID, (void*)(&argument->channelId));
    Aml_MP_MediaPlayerOnlyHintType type = AML_MP_MEDIAPLAYER_ONLYNONE;
    if (argument->onlyVideo)
        type = AML_MP_MEDIAPLAYER_VIDEO_ONLYHIT;
    if (argument->onlyAudio)
        type = AML_MP_MEDIAPLAYER_AUDIO_ONLYHIT;
    Aml_MP_MediaPlayer_SetParameter(*player, AML_MP_MEDIAPLAYER_PARAMETER_ONLYHINT_TYPE, (void*)&type);

    if (argument->loopMode == 2) {
        Aml_MP_MediaPlayer_SetLooping(*player, argument->loopMode);
    }

    //setdatasource
    Aml_MP_MediaPlayer_SetDataSource(*player, argument->url.c_str());
    printf("\nAmlMpMediaPlayerDemo:%d ----------url: %s\n", id, argument->url.c_str());

    //registerEventCallBack
    Aml_MP_MediaPlayer_RegisterEventCallBack(*player, demoCallback, reinterpret_cast<void*>(commandsProcess));

    //set video window after setting datasource and before starting, take effect after starting.
    //or use --size options when amlMpMediaPlayerDemo
    //passing (0, 0, 0, 0) is necessary to enable full-screen display by default.
    Aml_MP_MediaPlayer_SetVideoWindow(*player, argument->x, argument->y, argument->viewWidth, argument->viewHeight);

    //prepare
    Aml_MP_MediaPlayer_PrepareAsync(*player);
    ret = commandsProcess->waitPreparedEvent();
    if (ret < 0) {
        return ret;
    }

    int iD = argument->tunnelId == -1 ? 0 : argument->tunnelId;
    Aml_MP_MediaPlayer_SetParameter(*player, AML_MP_MEDIAPLAYER_PARAMETER_VIDEO_TUNNEL_ID, (void*)(&iD));

    //start
    Aml_MP_MediaPlayer_Start(*player);
    printf("\nAmlMpMediaPlayerDemo:%d ----------Aml_MP_MediaPlayer_Start\n", id);

    return 0;
}

static int destroyMultiPlayback(int id)
{
    int ret = -1;
    AML_MP_MEDIAPLAYER player = NULL;
    playerRoster::instance().getPlayerHandle(id, &player);

    if (player != NULL) {
        //stop
        ret = Aml_MP_MediaPlayer_Stop(player);

        //reset
        ret = Aml_MP_MediaPlayer_Reset(player);
        playerRoster::instance().unregisterPlayer(player);

        //destroy
        ret= Aml_MP_MediaPlayer_Destroy(player);
        printf("\nAmlMpMediaPlayerDemo:%d ----------Aml_MP_MediaPlayer_Destroy\n", id);
    } else {
        printf("\nAmlMpMediaPlayerDemo:%d ----------is NULl\n", id);
    }

    return 0;
}

static void closeMultiPlayback(int id)
{
    printf("closeMultiPlayback id:%d\n", id);

    if (id < 0) {
        return;
    }

    struct AmlMpMediaPlayerDemo* playerDemo = NULL;
    playerRoster::instance().getPlayerDemoHandle(id, &playerDemo);
    if (playerDemo != NULL)
        playerDemo->mQuitPending = true;

    //join pip threads
    std::thread *thread = NULL;
    playerRoster::instance().getPlayerThreadHandle(id, &thread);
    if (thread != NULL && thread->joinable()) {
        thread->join();
        printf("join multiPlaybackThread id:%d\n", id);
    }

}

static void closeAllMultiPlayback()
{
    //close pip first
    closeMultiPlayback(MEDIAPLAYERDEMO_PIP);
    closeMultiPlayback(MEDIAPLAYERDEMO_MAIN);
}

///////////////////////////////////////////////////////////////////////////////
static int isUrlValid(const std::string url, std::string& urlHead, std::string& urlFile)
{
    int ret = -1;
    std::size_t pos = std::string::npos;

    urlHead = "-1";
    urlFile = "-1";

    if ((pos = url.find(":")) != std::string::npos) {
        urlHead = url.substr(0, pos);
    }

    if (urlHead.compare("dclr") == 0) {
        urlFile = url.substr(pos + 1);
        ret = 0;
    }
    /*
    hclr-->HLS clear
    vstb-->verimatrix IPTV
    vwch-->verimatrix WEB Client HLS
    nwch-->nagra WEB Client HLS
    wcas-->widevine cas client
    ncas-->nagra cas client
    icas-->irdeto cas client
    */
    else if (urlHead.compare("hclr") == 0 ||
            urlHead.compare("vstb") == 0 ||
            urlHead.compare("vwch") == 0 ||
            urlHead.compare("nwch") == 0 ||
            urlHead.compare("wcas") == 0 ||
            urlHead.compare("ncas") == 0 ||
            urlHead.compare("icas") == 0) {
        ret = 0;
    }

    //printf("isUrlValid, ret:%d, url:%s, urlHead:%s, urlHead:%s\n", ret, url.c_str(), urlHead.c_str(), urlFile.c_str());
    return ret;
}

static int startingPlayCheck()
{
    int ret = 0;
    char errorInfo[50] = {0};

#ifndef ANDROID
#define YOCTO_libAmIptvMedia "/usr/lib/libAmIptvMedia.so"
    if (access(YOCTO_libAmIptvMedia, F_OK) != 0) {
        ret = -1;
        strcpy(errorInfo, YOCTO_libAmIptvMedia);
        goto EXIT;
    }

#define YOCTO_libaml_mp_mediaplayer "/usr/lib/libaml_mp_mediaplayer.so"
    if (access(YOCTO_libaml_mp_mediaplayer, F_OK) != 0) {
        ret = -1;
        strcpy(errorInfo, YOCTO_libaml_mp_mediaplayer);
        goto EXIT;
    }

#define YOCTO_libaml_mp_sdk "/usr/lib/libaml_mp_sdk.so"
    if (access(YOCTO_libaml_mp_sdk, F_OK) != 0) {
        ret = -1;
        strcpy(errorInfo, YOCTO_libaml_mp_sdk);
        goto EXIT;
    }

#define YOCTO_libffmpeg_ctc "/usr/lib/libffmpeg_ctc.so"
    if (access(YOCTO_libffmpeg_ctc, F_OK) != 0) {
        ret = -1;
        strcpy(errorInfo, YOCTO_libffmpeg_ctc);
        goto EXIT;
    }
#endif

    return ret;

EXIT:
    TERMINAL_ERROR("%s%s", errorInfo, " No Exist!!!");

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//Command Line
static int parseCommandArgs(int argc, char* argv[], Argument* argument)
{
    static const struct option longopts[] = {
        {"help",        no_argument,        nullptr, 'h'},
        {"size",        required_argument,  nullptr, 's'},
        {"pos",         required_argument,  nullptr, 'p'},
        {"channelId",   required_argument,  nullptr, 'c'},
        {"debug",       required_argument,  nullptr, 'd'},
        {"onlyVideo",   no_argument,        nullptr, 'V'},
        {"onlyAudio",   no_argument,        nullptr, 'A'},
        {"id",          required_argument,  nullptr, 't'},
        {"loopMode",    required_argument,  nullptr, 'L'},
        {"noSignalHandler", no_argument,    nullptr, 'S'},
        {nullptr,       no_argument,        nullptr, 0},
    };

    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "", longopts, &longindex)) != -1) {
        //printf("opt = %d\n", opt);
        switch (opt) {
        case 's':
        {
            int width, height;
            if (sscanf(optarg, "%dx%d", &width, &height) != 2) {
                printf("parse %s failed! %s\n", longopts[longindex].name, optarg);
            } else {
                argument->viewWidth = width;
                argument->viewHeight = height;
                printf("size:%dx%d\n", width, height);
            }
        }
        break;
        case 'p':
        {
            int x, y;
            //printf("optarg:%s\n", optarg);
            if (sscanf(optarg, "%dx%d", &x, &y) != 2) {
                printf("parse %s failed! %s\n", longopts[longindex].name, optarg);
            } else {
                argument->x = x;
                argument->y = y;
                printf("pos:%dx%d\n", x, y);
            }
        }
        break;
        case 'c':
        {
            int channelId = strtol(optarg, nullptr, 0);
            printf("channel id:%d\n", channelId);
            argument->channelId = channelId;
        }
        break;
        case 'd':
        {
            int debuglevel = strtol(optarg, nullptr, 0);
            printf("debug level:%d\n", debuglevel);
            AMMP_DEBUG_MODE_ENABLE = (bool)debuglevel;
        }
        break;
        case 'V':
        {
             argument->onlyVideo = true;
             printf("onlyVideo\n");
        }
        break;
        case 'A':
        {
            argument->onlyAudio = true;
            printf("onlyAudio\n");
        }
        break;
        case 't':
        {
            int tunnelId = strtol(optarg, nullptr, 0);
            printf("tunnel id:%d\n", tunnelId);
            argument->tunnelId = tunnelId;
        }
        break;
        case 'L':
        {
            int loopMode = strtol(optarg, nullptr, 0);
            printf("loop mode:%d\n", loopMode);
            argument->loopMode = loopMode;
        }
        break;
        case 'S':
        {
            printf("no signal handler will be installed!\n");
            argument->noSignalHandler = true;
        }
        break;

        case 'h':
        default:
            return -1;
        }
    }

    //printf("optind:%d, argc:%d\n", optind, argc);
    if (optind < argc) {
        argument->url = argv[argc-1];
        printf("url:%s\n", argument->url.c_str());
    }

    return 0;
}

static const char* mediaPlayerMediaErrorType2Str(Aml_MP_MediaPlayerMediaErrorType type)
{
    switch (type) {
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_UNKNOWN);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_BAD_REQUEST);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_UNAUTHORIZED);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_FORBIDDEN);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_NOT_FOUND);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_OTHER_4XX);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_HTTP_SERVER_ERROR);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_ENOENT);
        ENUM_TO_STR(AML_MP_MEDIAPLAYER_MEDIA_ERROR_ETIMEDOUT);
        default:
            return "unknowmn media error type";
    }
}

void demoCallback(void* userData, Aml_MP_MediaPlayerEventType event, int64_t param)
{
    AmlMpMediaPlayerDemo* demo = (AmlMpMediaPlayerDemo*)(userData);

    switch (event) {
        case AML_MP_MEDIAPLAYER_EVENT_PLAYBACK_COMPLETE:
        {
            printf("%s at #%d AML_MP_MEDIAPLAYER_EVENT_PLAYBACK_COMPLETE, id:%d\n",__FUNCTION__,__LINE__, demo->mId);
            closeMultiPlayback(demo->mId);
            break;
        }
        case AML_MP_MEDIAPLAYER_EVENT_PREPARED:
        {
            if (demo) {
                demo->broadcast(true);
            }
            printf("%s at #%d AML_MP_MEDIAPLAYER_EVENT_PREPARED, id:%d\n",__FUNCTION__,__LINE__, demo->mId);
            break;
        }
        case AML_MP_MEDIAPLAYER_EVENT_MEDIA_ERROR:
        {
            if (demo) {
                demo->broadcast(false);
            }
            closeMultiPlayback(demo->mId);
            printf("%s at #%d AML_MP_MEDIAPLAYER_EVENT_MEDIA_ERROR, id:%d, type:%s\n",__FUNCTION__,__LINE__, demo->mId, mediaPlayerMediaErrorType2Str(static_cast<Aml_MP_MediaPlayerMediaErrorType>(param)));
            break;
        }
        case AML_MP_MEDIAPLAYER_EVENT_STOPPED:
        {
            printf("%s at #%d AML_MP_MEDIAPLAYER_EVENT_STOPPED, id:%d\n",__FUNCTION__,__LINE__, demo->mId);
            break;
        }
        default:
            break;
    }
}

static void showOption()
{
    printf(
    "AmlMpMediaPlayerDemo\n"
    "Type one option\n"
    "\n"
    "Main:\n"
    "         o.................Open URL\n"
    "         c.................Close URL\n"
    "         +.................Zap To Following URL\n"
    "         -.................Zap To Previous URL\n"
    "                               CHANNEL LIST\n"
    "                             ------------\n"
    "                                - igmp://239.0.0.185:8208\n"
    "                                - igmp://239.0.0.183:8208\n"
    "                                - igmp://239.0.0.186:8208\n"
    "                                - igmp://239.0.0.177:8208\n"
    "                                - igmp://239.0.0.176:8208\n"
    "                                - igmp://239.0.0.187:8208\n"
    "                                - igmp://239.0.5.246:8208\n"
    "                                - igmp://239.0.5.111:8208\n"
    "                                - igmp://239.0.5.114:8208\n"
    "         p.................Pause\n"
    "         r.................Resume\n"
    "         K.................Seek\n"
    "         v.................Get Volume\n"
    "         V.................Set Volume\n"
    "         F.................Set fast rate\n"
    "         M.................Set mute\n"
    "         <.................Rewind  -> -x8 -> -x16 -> -x32 -> -x64\n"
    "         >.................Forward ->  x8 ->  x16 ->  x32 ->  x64 -> x1\n"
    "         a.................Get Audio Info Channels\n"
    "         A.................Set Next Audio component\n"
    "         s.................Get Subtitles Info channels\n"
    "         S.................Set Next Subtitle component\n"
    "         i.................Get Track/Media info\n"
    "         n.................Get Position [milliseconds]\n"
    "         N.................Get Duration [milliseconds]\n"
    "         w.................Get Main Window Position\n"
    "         W.................Set Main Window Position\n"
    "         D.................Show Video\n"
    "         d.................Hide Video\n"
    "\n"
    "PIP:\n"
    "         O.................Open PIP\n"
    "         C.................Close PIP\n"
    "         e.................Get PIP Window Position\n"
    "         E.................Set PIP Window Position\n"
    "\n"
    "         q................ Exit\n"
    "\n"
    );
}

static void showUsage()
{
    printf("Usage: amlMpMediaPlayerDemo <options> <url>\n"
            "options:\n"
            "    --size:       eg: 1920x1080\n"
            "    --pos:        eg: 0x0\n"
            "    --debug:      eg: 1\n"
            "    --channelId:  eg: 0 main, others: pip\n"
            "    --id:         specify the corresponding ui channel id\n"
            "    --onlyVideo         \n"
            "    --onlyAudio         \n"
            "   --loopMode <value>  \n"
            "              1: wait play completed, then destroy player and recreate player\n"
            "              2: seek to beginning internally when play completed\n"
            "   --noSignalHandler       don't install signal handler\n"
            "\n"
            "url format:\n"
            "    clear ts, eg: dclr:http://10.28.8.30:8881/data/a.ts\n"
            "    clear local file, eg: dclr:/data/a.ts\n"
            "\n"
            );
}

int main(int argc, char *argv[])
{
    // support run in background
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    int ret = -1;

    if (startingPlayCheck() < 0) {
        return ret;
    }

    Argument argument{};
    if (parseCommandArgs(argc, argv, &argument) < 0) {
        showUsage();
        return ret;
    }

    if (tty_reset(STDIN_FILENO) < 0) {
        printf("\n tty_reset error\n");
        return 0;
    }

    if (tty_cbreak(STDIN_FILENO) < 0) {
        printf("\n tty_cbreak error\n");
    }

    playerRoster::instance().registerPlayerArgument(MEDIAPLAYERDEMO_MAIN, &argument);//only register argument

    if (!argument.noSignalHandler) {
        AmlMpMediaPlayerDemo::installSignalHandler();
    }
    int loopCount = 0;
begin:
    ret = multiPlaybackThread(MEDIAPLAYERDEMO_MAIN, true/*isMain*/);
    if (ret < 0) {
        printf("\n multiPlaybackThread id:%d error\n", (int)MEDIAPLAYERDEMO_MAIN);
    }

    {
        ++loopCount;
        Argument* argument = nullptr;
        playerRoster::instance().getPlayerArgument(MEDIAPLAYERDEMO_MAIN, &argument);
        if (argument && argument->loopMode == 1) {
            printf("loop play count:%d\n", loopCount);
            goto begin;
        }
    }


    if (tty_reset(STDIN_FILENO) < 0)
        printf("\n tty_reset error\n");

    return ret;
}

