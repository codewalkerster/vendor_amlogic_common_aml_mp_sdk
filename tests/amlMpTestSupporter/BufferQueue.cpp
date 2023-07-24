/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpPlayerDemo_BufferQueue"
#include <utils/AmlMpLog.h>
#include "BufferQueue.h"
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <algorithm>
#ifdef HAVE_SECMEM
#include <secmem_ca.h>
#include <secmem_types.h>
#endif
#include <utils/AmlMpUtils.h>
#include <cutils/properties.h>

static const char* mName = LOG_TAG;

namespace aml_mp {
////////////////////////////////////////////////////////////////////////////////
BufferQueue::Buffer* BufferQueue::Buffer::create(size_t size, void* secMemSession, Aml_MP_StreamType streamType)
{
    Buffer* buffer = nullptr;

    if (secMemSession) {
        buffer = new SecBuffer(size, secMemSession, streamType);
    } else {
        buffer = new Buffer(size, streamType);
    }

    return buffer;
}

BufferQueue::Buffer::Buffer(size_t capacity, Aml_MP_StreamType streamType)
: mRangeOffset(0)
, mStreamType(streamType)
{
    if (capacity > 0) {
        mData  = malloc(capacity);
    }

    if (mData == nullptr) {
        mCapacity = 0;
        mRangeLength = 0;
    } else {
        mCapacity = capacity;
        mRangeLength = 0;
    }

    mIsNonTunnelMode = AmlMpConfig::instance().mTsPlayerNonTunnel;

    MLOGV("ctor Buffer(this:%p), mData:%p", this, mData);

}

BufferQueue::Buffer::~Buffer()
{
    if (mData) {
        free(mData);
        mData = nullptr;
    }
}

void BufferQueue::Buffer::setRange(size_t offset, size_t size)
{
    AML_MP_CHECK_LE(offset, mCapacity);
    AML_MP_CHECK_LE(offset + size, mCapacity);

    mRangeOffset = offset;
    mRangeLength = size;
}

void BufferQueue::Buffer::reset()
{
    mRangeOffset = 0;
    mRangeLength = 0;
}

size_t BufferQueue::Buffer::append(const uint8_t* data, size_t size)
{
    MLOGV("append input size:%zu, mCapacity:%zu, mRangeOffset:%zu, mRangeLength:%zu", size, mCapacity, mRangeOffset, mRangeLength);

    if (mCapacity - mRangeLength - mRangeOffset < size) { //check trailing space only, no wrap around
        MLOGE("no enough space!");
        return 0;
    }

    memcpy(this->data() + mRangeLength, data, size);
    mRangeLength += size;

    return size;
}

int BufferQueue::Buffer::prepareReaderView()
{
    if (mReaderView == nullptr) {
        mReaderView = this;
    }

    mRecycleHandle = (int64_t)mData + mRangeOffset;

    return 0;
}

bool BufferQueue::Buffer::needRecycle() const
{
    bool result = false;

    //in non tunnel mode, clear video es buffer also use asynchronous write
#ifdef ANDROID
    result = true;
#else
    if (mIsNonTunnelMode) {
        result = true;
    }
#endif

    if (mStreamType == AML_MP_STREAM_TYPE_AUDIO) {
        result = true;
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////
enum drm_level_e {
    DRM_LEVEL1 = 1,
    DRM_LEVEL2 = 2,
    DRM_LEVEL3 = 3,
    DRM_NONE = 4,
};

#define TYPE_DRMINFO_V2  0x100
#define TYPE_DRMINFO   0x80
#define TYPE_PATTERN   0x40

struct drm_info {
    enum drm_level_e drm_level;
    uint32_t drm_flag;
    uint32_t drm_hasesdata;
    uint32_t drm_priv;
    uint32_t drm_pktsize;
    uint32_t drm_pktpts;
    uint32_t drm_phy;
    uint32_t drm_vir;
    uint32_t drm_remap;
    uint32_t data_offset;
    uint32_t handle;
    uint32_t extpad[7];
} /*drminfo_t */;

BufferQueue::SecBuffer::SecBuffer(size_t capacity, void* secMemSession, Aml_MP_StreamType streamType)
: Buffer(0, streamType)
, mSecMemSession(secMemSession)
{
    int ret = 0;
    uint32_t handle = UINT32_MAX;
#ifdef HAVE_SECMEM
    ret = Secure_V2_MemCreate(mSecMemSession, &handle);
#endif
    if (ret) {
        MLOGE("secmem create handle failed:%#x", ret);
        return;
    }

    mHandle = handle;
#ifdef __LP64__
    mData = (void*)((size_t)mHandle << 32);
    MLOGV("mData:%p", mData);
#else
    mData = (void*)(size_t)(mHandle);
#endif

    mCapacity = capacity;    //defer allocation
}

BufferQueue::SecBuffer::~SecBuffer()
{
    int ret = 0;
    if (mPhyAddr != UINT32_MAX) {
#ifdef HAVE_SECMEM
        ret = Secure_V2_MemFree(mSecMemSession, mHandle);
#endif
        if (ret) {
            MLOGE("secmem free failed:%#x", ret);
        }
        mPhyAddr = UINT32_MAX;
    }

    if (mHandle != UINT32_MAX) {
#ifdef HAVE_SECMEM
        ret = Secure_V2_MemRelease(mSecMemSession, mHandle);
#endif
        if (ret) {
            MLOGE("secmem release failed:%#x", ret);
        }
        mHandle = UINT32_MAX;
    }

    if (mReaderView) {
        delete mReaderView;
        mReaderView = nullptr;
    }
}

int BufferQueue::SecBuffer::resize(size_t capacity)
{
    int ret = 0;

    if (mPhyAddr != UINT32_MAX) {
#ifdef HAVE_SECMEM
        ret = Secure_V2_MemFree(mSecMemSession, mHandle);
#endif
        if (ret) {
            MLOGE("secmem free failed:%#x, handle:%#x", ret, mHandle);
        }
        mPhyAddr = UINT32_MAX;
    }

    if (capacity == 0) {
        return 0;
    }

    uint32_t phyaddr = 0;
#ifdef HAVE_SECMEM
    ret = Secure_V2_MemAlloc(mSecMemSession, mHandle, capacity, &phyaddr);
#endif
    if (ret) {
        MLOGE("secmem alloc failed:%#x, handle:%#x", ret, mHandle);
    } else {
        mPhyAddr = phyaddr;
        MLOGV("secmem handle:%#x, phyaddr:%#x, capacity:%zu", mHandle, mPhyAddr, capacity);
    }

    return 0;
}

size_t BufferQueue::SecBuffer::append(const uint8_t* data __unused, size_t size)
{
    MLOGV("secmem append input size:%zu, mCapacity:%zu, mRangeOffset:%zu, mRangeLength:%zu", size, mCapacity, mRangeOffset, mRangeLength);

    if (mCapacity - mRangeLength - mRangeOffset < size) { //check trailing space only, no wrap around
        MLOGE("secmem no enough space!");
        return 0;
    }

    if (mHandle == UINT32_MAX) {
        MLOGE("secmem fill invalid handle!");
        return 0;
    }

    int ret = 0;
    ssize_t offset = mRangeOffset + mRangeLength;
#ifdef HAVE_SECMEM
    ret = Secure_V2_MemFill(mSecMemSession, mHandle, offset, (uint8_t*)data, size);
#endif
    if (ret) {
        MLOGE("secmem fill failed:%#x", ret);
        return 0;
    }

    mRangeLength += size;

    return size;
}

int BufferQueue::SecBuffer::prepareReaderView()
{
    if (mReaderView == nullptr) {
        mReaderView = Buffer::create(sizeof(drm_info), nullptr, mStreamType);
    }
    mRecycleHandle = mHandle;

    mReaderView->setRange(0, sizeof(drm_info));
    mReaderView->setPts(mPts);

    drm_info* info = (drm_info*)mReaderView->data();
    memset(info, 0, sizeof(drm_info));
    info->drm_level = DRM_LEVEL1;
    info->drm_pktsize = mRangeLength;
    info->drm_pktpts = mPts;
    info->drm_hasesdata = 0;
    info->drm_phy = mPhyAddr;
    info->drm_flag = TYPE_DRMINFO | TYPE_DRMINFO_V2;
    info->data_offset = mRangeOffset;
    info->handle = mHandle;
    *(void**)info->extpad  = mSecMemSession;

    MLOGV("drm_info:%p, secmem handle:%#x, phyaddr:%#x, offset:%zu, size:%zu", info, mHandle, mPhyAddr, mRangeOffset, mRangeLength);

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
BufferQueue::BufferQueue(Aml_MP_StreamType streamType, size_t bufferCount, size_t bufferSize, bool isSVP, bool isUHD)
: mStreamType(streamType)
, mMaxBufferCount(bufferCount)
, mBufferCapacity(bufferSize)
, mIsSecure(isSVP)
, mIsUHD(isUHD)
, mBufferCount(0)
, mLastDequeueBufferResult(0)
, mStopped(false)
{
    AmlMpConfig::instance().init();

    MLOGI("constructor BufferQueue(%zu, %zu), mIsSecure:%d, isUHD:%d", bufferCount, bufferSize, mIsSecure, isUHD);
}

void BufferQueue::initSessionIfNeeded()
{
#ifdef HAVE_SECMEM
    if (mIsSecure && mSecMemSession == nullptr) {
        int ret = 0;
        ret = Secure_V2_SessionCreate(&mSecMemSession);
        if (ret) {
            MLOGE("secmem session create failed:%#x", ret);
        } else {
            MLOGI("mSecMemSession:%p", mSecMemSession);
        }

        uint32_t source = SECMEM_V2_MEM_SOURCE_VDEC;
        uint32_t flags = SECMEM_V2_FLAGS_USAGE(SECMEM_V2_USAGE_DRM_PLAYBACK);
        if (mStreamType == AML_MP_STREAM_TYPE_VIDEO) {
            flags |= SECMEM_V2_FLAGS_TVP(mIsUHD ? SECMEM_TVP_TYPE_UHD : SECMEM_TVP_TYPE_FHD);
        } else if (mStreamType == AML_MP_STREAM_TYPE_AUDIO) {
            flags |= SECMEM_V2_FLAGS_VP9(SECMEM_CODEC_AUDIO);
        }
        MLOGI("secmem session source:%d, flags:%#x", source, flags);
        ret = Secure_V2_Init(mSecMemSession, source, flags, 0, 0);
        if (ret) {
            MLOGE("secmem session init failed:%#x", ret);
        }

        uint32_t count;
        uint32_t size;
        Secure_GetBufferConfig(&count, &size);
        MLOGI("secmem session count:%u, size:%u", count, size);
    }
#endif
}

BufferQueue::~BufferQueue()
{
    MLOG();

    //release all buffers first
    mDequeuedBuffers.clear();
    mBuffers.clear();
    mBufferQueueSize = 0;
    mFreeBuffers.clear();
    mRecycleBuffers.clear();
    mRecycleQueueSize = 0;

    //release session
    if (mSecMemSession) {
#ifdef HAVE_SECMEM
        Secure_V2_SessionDestroy(&mSecMemSession);
#endif
    }
}

int BufferQueue::dequeueBuffer(Buffer** buffer)
{
    std::shared_ptr<Buffer> b;

    {
        std::unique_lock<std::mutex> _l(mFreeBufferLock);
        if (!mFreeBuffers.empty()) {
            b = *mFreeBuffers.begin();
            mFreeBuffers.pop_front();
            mLastDequeueBufferResult.store(0, std::memory_order_relaxed);

            b->reset();
        } else {
            if (mBufferCount >= mMaxBufferCount) {
                MLOGV("buffer overflow!, buffers:%zu, limit:%zu", mBufferCount, mMaxBufferCount);
                mLastDequeueBufferResult.store(-1, std::memory_order_relaxed);
                goto exit;
            }

            initSessionIfNeeded();

            b.reset(Buffer::create(mBufferCapacity, mSecMemSession, mStreamType));
            if (b != nullptr) {
                ++mBufferCount;
            } else {
                MLOGE("dequeue buffer allocate failure!");
                mLastDequeueBufferResult.store(-1, std::memory_order_relaxed);
            }
        }

    }

    {
        std::unique_lock<std::mutex> _l(mDequeuedBufferLock);
        if (b != nullptr) {
            mDequeuedBuffers.insert(b);
        }
    }

    if (buffer != nullptr) {
        *buffer = b.get();
    }

exit:
    return mLastDequeueBufferResult.load(std::memory_order_relaxed);
}

int BufferQueue::queueBuffer(Buffer* buffer)
{
    if (buffer == nullptr) {
        return -1;
    }

    std::shared_ptr<Buffer> b;
    {
        std::unique_lock<std::mutex> _l(mDequeuedBufferLock);
        auto it = std::find_if(mDequeuedBuffers.begin(), mDequeuedBuffers.end(),
            [buffer](const std::shared_ptr<Buffer>& p) {
                return p.get() == buffer;
        });
        if (it != mDequeuedBuffers.end()) {
            b = *it;
            mDequeuedBuffers.erase(it);
        } else {
            MLOGE("queueBuffer with an unknown buffer:%p", buffer);
            return -1;
        }
    }

    {
        std::unique_lock<std::mutex> _l(mBufferLock);
        mBuffers.push_back(b);
        mBufferCond.notify_all();
        ++mBufferQueueSize;
    }

    return 0;
}

int BufferQueue::cancelBuffer(Buffer* buffer)
{
    if (buffer == nullptr) {
        return -1;
    }

    std::shared_ptr<Buffer> b;
    {
        std::unique_lock<std::mutex> _l(mDequeuedBufferLock);
        auto it = std::find_if(mDequeuedBuffers.begin(), mDequeuedBuffers.end(),
            [buffer](const std::shared_ptr<Buffer>& p) {
                return p.get() == buffer;
        });
        if (it != mDequeuedBuffers.end()) {
            b = *it;
            mDequeuedBuffers.erase(it);
        } else {
            MLOGE("cancelBuffer with an unknown buffer:%p", buffer);
            return -1;
        }
    }

    std::unique_lock<std::mutex> _l(mFreeBufferLock);
    mFreeBuffers.push_front(b);

    return 0;
}

BufferQueue::Buffer* BufferQueue::toBuffer(const uint8_t* base)
{
    std::shared_ptr<Buffer> b;

    {
        std::unique_lock<std::mutex> _l(mDequeuedBufferLock);
        auto it = std::find_if(mDequeuedBuffers.begin(), mDequeuedBuffers.end(),
            [base](const std::shared_ptr<Buffer>& p) {
                return p->base() == base;
        });
        if (it != mDequeuedBuffers.end()) {
            b = *it;
        } else {
            MLOGE("toBuffer with an unknown base pointer:%p", base);
        }
    }

    return b.get();
}

int BufferQueue::acquireBuffer(std::shared_ptr<Buffer>* buffer)
{
    std::unique_lock<std::mutex> _l(mBufferLock);
    std::shared_ptr<Buffer> b;

retry:
    if (!mBuffers.empty()) {
        b = *mBuffers.begin();
        mBuffers.pop_front();
        --mBufferQueueSize;
    } else {
        while (mBuffers.empty() && !mStopped.load(std::memory_order_relaxed)) {
            if (mBufferCond.wait_for(_l, std::chrono::milliseconds(10)) == std::cv_status::timeout) {
                break;
            }
        }

        if (!mBuffers.empty()) {
            goto retry;
        }
    }

    if (b != nullptr) {
        *buffer = b;
        return 0;
    }

    return -1;
}

int BufferQueue::releaseBuffer(const std::shared_ptr<Buffer>& buffer)
{
    buffer->reset();
    buffer->resize(0);

    std::unique_lock<std::mutex> _l(mFreeBufferLock);
    mFreeBuffers.push_back(buffer);

    return 0;
}

int BufferQueue::recycleBuffer(const std::shared_ptr<Buffer>& buffer)
{
    std::unique_lock<std::mutex> _l(mRecycleBufferLock);
    mRecycleBuffers.push_back(buffer);
    ++mRecycleQueueSize;

    return 0;
}

std::shared_ptr<BufferQueue::Buffer> BufferQueue::getRecycledBuffer(int64_t handle)
{
    std::shared_ptr<Buffer> b;

    {
        std::unique_lock<std::mutex> _l(mRecycleBufferLock);
        auto it = std::find_if(mRecycleBuffers.begin(), mRecycleBuffers.end(),
            [handle](const std::shared_ptr<Buffer>& p) {
                return p->recycleHandle() == handle;
        });
        if (it != mRecycleBuffers.end()) {
            b = *it;
            mRecycleBuffers.erase(it);
            --mRecycleQueueSize;
        } else {
            MLOGE("getRecycledBuffer with an unknown handle:%#" PRIx64, handle);
        }
    }

    return b;
}

bool BufferQueue::stopReceive()
{
    bool oldState = mStopped.exchange(true, std::memory_order_relaxed);

    std::unique_lock<std::mutex> _l(mBufferLock);
    mBufferCond.notify_all();

    return oldState;
}

void BufferQueue::restoreReceive(bool state)
{
    mStopped.store(state, std::memory_order_relaxed);
}

//TODO
void BufferQueue::flush()
{
    std::list<std::shared_ptr<Buffer>> buffers;
    {
        std::unique_lock<std::mutex> _l(mBufferLock);
        mBuffers.swap(buffers);
        mBufferQueueSize = 0;
    }

    std::list<std::shared_ptr<Buffer>> recycleBuffers;
    {
        std::unique_lock<std::mutex> _l(mRecycleBufferLock);
        mRecycleBuffers.swap(recycleBuffers);
        mRecycleQueueSize = 0;
    }

    for (auto& b : buffers) {
        b->resize(0);
    }

    for (auto& b : recycleBuffers) {
        b->resize(0);
    }

    {
        std::unique_lock<std::mutex> _l(mFreeBufferLock);
        mFreeBuffers.splice(mFreeBuffers.end(), recycleBuffers);
        mFreeBuffers.splice(mFreeBuffers.end(), buffers);

        MLOGI("free buffer size:%zu", mFreeBuffers.size());
    }
}

void BufferQueue::getFlowStatus(bool* underflow, bool* overflow)
{
    size_t bufferCount = 0;

    if (underflow || overflow) {
        bufferCount = getBufferQueueSize() + getRecyleQueueSize();
    }

    if (underflow) {
        *underflow = bufferCount == 0;
    }

    if (overflow) {
        *overflow = bufferCount * 100 / mMaxBufferCount > 50;
        if (*overflow) {
            MLOGD("getFlowStatus: bufferCount:%zu, mMaxBufferCount:%zu", bufferCount, mMaxBufferCount);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
BufferQueue::Consumer::Consumer(Aml_MP_StreamType streamType, const std::string& name)
: mStreamType(streamType)
{
    snprintf(mName, sizeof(mName), "%s_%s", LOG_TAG, name.c_str());
    snprintf(mConsumerName, sizeof(mConsumerName), "%s", name.c_str());
    MLOG();

    char value[PROPERTY_VALUE_MAX];
    int ret = property_get("vendor.media.dump.bufq", value, "");
    if (ret > 0) {
        bool enableDump = false;
        if ((mStreamType == AML_MP_STREAM_TYPE_AUDIO && strstr(value, "audio")) ||
            (mStreamType == AML_MP_STREAM_TYPE_VIDEO && strstr(value, "video"))) {
            enableDump = true;
        }

        if (enableDump) {
            std::string dumpPath = "/data/";
            dumpPath.append(name);

            struct timeval tv;
            gettimeofday(&tv, nullptr);

            struct tm t;
            localtime_r(&tv.tv_sec, &t);
            char tbuf[50];
            strftime(tbuf, sizeof(tbuf), "_%Y_%m_%d_%H_%M_%S_", &t);
            dumpPath.append(tbuf);
            dumpPath.append(std::to_string(tv.tv_usec/1000));

            MLOGI("try dump %s", dumpPath.c_str());

            mDumpFd = ::open(dumpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (mDumpFd < 0) {
                MLOGE("open dump %s failed, %s", dumpPath.c_str(), strerror(errno));
            }
        }
    }
}

BufferQueue::Consumer::~Consumer()
{
    MLOG();

    stop();

    if (mDumpFd >= 0) {
        ::close(mDumpFd);
        mDumpFd = -1;
    }
}

void BufferQueue::Consumer::connectQueue(BufferQueue* bufferQueue)
{
    MLOG();

    mBufferQueue = bufferQueue;
}

void BufferQueue::Consumer::connectSink(const std::function<int(const uint8_t*, size_t, uint64_t)>& fn)
{
    MLOG();

    mWriter = fn;
}

void BufferQueue::Consumer::setBufferAvailableCb(const std::function<void()>& cb)
{
    MLOG();

    mBufferAvailableCb = cb;
}

int BufferQueue::Consumer::start()
{
    MLOGD("BufferQueue start");

    if (mWorker.get_id() == std::thread::id()) {
        MLOGI("create mWorker!");
        mWorker = std::thread([this] {
            threadLoop();
        });

        std::string tName(mConsumerName);
        tName.append("_bufq");
        pthread_setname_np(mWorker.native_handle(), tName.c_str());
    }

    if (mBufferQueue) {
        mBufferQueue->restoreReceive();
    }

    sendWorkCommand(kWorkStart);

    return 0;
}

//call start to resume working
int BufferQueue::Consumer::pause()
{
    MLOGI("BufferQueue pause");

    if (mBufferQueue) {
        mBufferQueue->stopReceive();
    }

    sendWorkCommandAndWait(kWorkPause);
    MLOGI("BufferQueue pause end.");

    return 0;
}

int BufferQueue::Consumer::stop()
{
    MLOGI("BufferQueue stop");

    if (mBufferQueue) {
        mBufferQueue->stopReceive();
    }

    sendWorkCommandAndWait(kWorkQuit);
    if (mWorker.joinable()) {
        mWorker.join();
    }

    MLOG();
    return 0;
}

int BufferQueue::Consumer::notifyBufferReleased(int64_t handle)
{
    MLOGV("recycle handle:%#" PRIx64, handle);

    {
        std::lock_guard<std::mutex> _l(mRecycleHandleLock);
        mRecycleHandles.push_back(handle);
    }
    sendWorkCommand(kWorkRecycleBuffer);

    return 0;
}

int BufferQueue::Consumer::flush()
{
    bool oldState = mBufferQueue->stopReceive();
    MLOGI("bufq flush, oldState stopped:%d", oldState);

    sendWorkCommand(kWorkFlush);

    mBufferQueue->restoreReceive(oldState);

    return 0;
}

int BufferQueue::Consumer::threadLoop()
{
    std::shared_ptr<Buffer> acquiredBuffer;
    uint32_t work;
    Work workSequence;
    int ret;
    uint32_t writeCount = 0;

    MLOGI("bufq start run!");

    for (;;) {
        {
            std::unique_lock<std::mutex> _l(mLock);
            mCond.wait(_l, [this, &work] {return (work = mWork) != 0; });
        }

        if (work != 0 && !mWorkQueue.empty()) {
            workSequence = *mWorkQueue.begin();
            switch (workSequence) {
            case kWorkQuit:
            {
                MLOGW("Quit!");
                signalWorkDone(kWorkQuit);
                goto exit;

                break;
            }

            case kWorkStart:
            {
                mStarted = true;
                MLOGI("start, buffer queue size:%zu, recycle queue size:%zu", mBufferQueue->getBufferQueueSize(), mBufferQueue->getRecyleQueueSize());
                signalWorkDone(kWorkStart);
                {
                    std::lock_guard<std::mutex> _l(mLock);
                    mWork |= kWorkFeedData;
                }
                break;
            }

            case kWorkPause:
            {
                mStarted = false;
                signalWorkDone(kWorkPause);
                {
                    std::lock_guard<std::mutex> _l(mLock);
                    mWork &= ~ kWorkFeedData;
                }
                MLOGI("kWorkPause done!");
                break;
            }

            case kWorkRecycleBuffer:
            {
                bool canNotify = false;
                {
                    std::lock_guard<std::mutex> _l(mRecycleHandleLock);
                    auto it = mRecycleHandles.begin();
                    while (it != mRecycleHandles.end()) {
                        std::shared_ptr<Buffer> buffer = mBufferQueue->getRecycledBuffer(*it);
                        if (buffer == nullptr) {
                            break;
                        }
                        mBufferQueue->releaseBuffer(buffer);
                        it = mRecycleHandles.erase(it);

                        canNotify = true;
                        MLOGV("[%u] recycle releaseBuffer base:%p", ++mReleasedCount, buffer->base());
                    }
                }

                if (mBufferQueue->lastDequeueResult() != 0 && canNotify) {
                    MLOGV("try notify buffer available!");
                    if (mBufferAvailableCb) {
                        mBufferAvailableCb();
                    }
                }

                signalWorkDone(kWorkRecycleBuffer);
                break;
            }

            case kWorkFlush:
            {
                if (acquiredBuffer) {
                    if (acquiredBuffer->needRecycle()) {
                        mBufferQueue->recycleBuffer(acquiredBuffer);
                    } else {
                        mBufferQueue->releaseBuffer(acquiredBuffer);
                    }
                    acquiredBuffer = nullptr;
                }

                mBufferQueue->flush();

                {
                    std::lock_guard<std::mutex> _l(mRecycleHandleLock);
                    mRecycleHandles.clear();
                }

                MLOGI("kWorkFlush done!");
                signalWorkDone(kWorkFlush);
                break;
            }

            default:
                break;
            }
        }

        if (!mStarted) {
            continue;
        }

        if (!(work & kWorkFeedData)) {
            continue;
        }

        if (acquiredBuffer == nullptr) {
            ret = mBufferQueue->acquireBuffer(&acquiredBuffer);
            if (ret < 0) {
                continue;
            }
            acquiredBuffer->prepareReaderView();
            MLOGV("[%u] acquiredBuffer base:%p, size:%zu", ++mAcquiredCount, acquiredBuffer->base(), acquiredBuffer->size());

            if (mDumpFd >= 0) {
                if (::write(mDumpFd, acquiredBuffer->data(), acquiredBuffer->size()) != (int)acquiredBuffer->size()) {
                    MLOGE("write dump file failed!");
                }
            }
        } else if (acquiredBuffer->readerView()->size() == 0) {
            if (acquiredBuffer->needRecycle()) {
                MLOGV("[%u] recycleBuffer base:%p", ++mRecycledCount, acquiredBuffer->base());
                mBufferQueue->recycleBuffer(acquiredBuffer);
            } else {
                MLOGV("[%u] releaseBuffer base:%p", ++mReleasedCount, acquiredBuffer->base());
                mBufferQueue->releaseBuffer(acquiredBuffer);
                if (mBufferQueue->lastDequeueResult() != 0) {
                    MLOGV("try notify buffer available!");
                    if (mBufferAvailableCb) {
                        mBufferAvailableCb();
                    }
                }
            }
            acquiredBuffer = nullptr;

            continue;
        }

        if (mWriter == nullptr) {
            MLOGE("mWriter is NULL!");
            usleep(10*1000);
            continue;
        }

        int len = mWriter(acquiredBuffer->readerView()->data(), acquiredBuffer->readerView()->size(), acquiredBuffer->readerView()->pts());
        if (len < 0) {
            len = 0;
            usleep(10*1000);
        } else if (len == 0) {
            len = acquiredBuffer->readerView()->size();
        }

        acquiredBuffer->readerView()->setRange(acquiredBuffer->readerView()->offset()+len, acquiredBuffer->readerView()->size()-len);
        ++writeCount;
        if (writeCount == 1 ||  writeCount%1000 == 0) {
            MLOGV("bufferQueueSize:%zu (%u/%u/%u)", mBufferQueue->getBufferQueueSize(), mAcquiredCount, mRecycledCount, mReleasedCount);
        }
    }

exit:
    MLOGI("mWorkQueue size:%zu", mWorkQueue.size());
    MLOG();
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
StreamParser::StreamParser(Aml_MP_StreamType streamType, StreamCategory streamCategory)
: mStreamType(streamType)
, mStreamCategory(streamCategory)
{
    snprintf(mName, sizeof(mName), "%s_%s", "StreamParser", mpStreamType2Str(mStreamType));
    MLOG();
}

int StreamParser::append(const uint8_t* data, size_t size)
{
    size_t neededSize = (mBuffer == nullptr ? 0 : mBuffer->size()) + size;
    if (mBuffer == nullptr || neededSize > mBuffer->capacity()) {
        neededSize = (neededSize + 65535) & ~65535;

        if (neededSize >= 4 * 1024 * 1024) {
            MLOGE("neededSize is too large!");
            return -1;
        }

        sptr<AmlMpBuffer> buffer = new AmlMpBuffer(neededSize);
        if (mBuffer != nullptr) {
            memcpy(buffer->data(), mBuffer->data(), mBuffer->size());
            buffer->setRange(0, mBuffer->size());
        } else {
            buffer->setRange(0, 0);
        }

        mBuffer = buffer;
    }

    memcpy(mBuffer->data() + mBuffer->size(), data, size);
    mBuffer->setRange(0, mBuffer->size() + size);

    return 0;
}

int StreamParser::lockFrame(Frame* frame)
{
    int ret = 0;

    memset(frame, 0, sizeof(Frame));

    switch (mStreamCategory) {
    case PES:
    {
        const uint8_t* data = mBuffer->data();
        size_t size = mBuffer->size();
        int startPos = -1;

        startPos = findPES(data, size);
        if (startPos < 0) {
            return -1;
        }
        //MLOGI("startPos:%d", startPos);

        parsePES(data + startPos, size - startPos, frame);
        if (frame->size == 0) {
            if (size < (size_t)startPos + frame->u.pes.pes_header_length) {
                MLOGW("data not enough!");
                return -1;
            }

            int endPos = -1;
            endPos = findPES(data + startPos + frame->u.pes.pes_header_length, size - startPos - frame->u.pes.pes_header_length);
            if (endPos < 0) {
                MLOGV("can't find endPos!");
                return -1;
            }

            frame->size = endPos;
            MLOGV("endPos:%d", endPos);
        }
        frame->data = data + startPos + frame->u.pes.pes_header_length;
    }
    break;

    case ES:
    {
        break;
    }

    case MP4:
    default:
        MLOGE("unsupported streamCategory:%d", mStreamCategory);
        ret = -1;
        break;
    }

    return ret;
}

int StreamParser::unlockFrame(Frame* frame)
{
    size_t consumed = frame->data + frame->size - mBuffer->data();
    if (consumed != mBuffer->size()) {
        MLOGD("unlockFrame consumed:%zu, left:%zu", consumed, mBuffer->size()-consumed);
    }

    memmove(mBuffer->data(), mBuffer->data() + consumed, mBuffer->size() - consumed);
    mBuffer->setRange(0, mBuffer->size() - consumed);

    return 0;
}

int StreamParser::findPES(const uint8_t* data, size_t size)
{
    int pos = 0;

    while (pos < (int)size-4) {
        uint32_t token = *(uint32_t*)(data+pos);
        //CHECK it!
        if (token == 0xE0010000) {
            return pos;
        }

        ++pos;
    }

    return -1;
}

int StreamParser::parsePES(const uint8_t* data, size_t size __unused, Frame* frame)
{
    const uint8_t* p = data;
    int pesStartCode = p[0] << 16 | p[1] << 8 | p[2];
    AML_MP_CHECK_EQ(pesStartCode, 0x000001);
    p += 3;

    int stream_id = *p++;
    int pes_packet_length = p[0] << 8 | p[1];
    p += 2;

    if (stream_id != 0xbc &&
        stream_id != 0xbe &&
        stream_id != 0xbf &&
        stream_id != 0xf0 &&
        stream_id != 0xf1 &&
        stream_id != 0xff &&
        stream_id != 0xf2 &&
        stream_id != 0xf8) {

        ++p;

        int PTS_DTS_flags = p[0] >> 6;
        int pes_header_length = p[1];
        p += 2;

        int64_t pts = 0;
        int64_t dts = 0;
        if (PTS_DTS_flags & 0x02) {
            pts = (int64_t)(p[0]&0x0e) << 29;
            pts |= ((p[1]<<8 | p[2])>>1) << 15;
            pts |= ((p[3]<<8 | p[4])>>1);

            p += 5;
        }

        if (PTS_DTS_flags & 0x01) {
            dts = (int64_t)(p[0]&0x0e) << 29;
            dts |= ((p[1]<<8 | p[2])>>1) << 15;
            dts |= ((p[3]<<8 | p[4])>>1);

            p += 5;
        }

        if (pes_packet_length > 3 + pes_header_length) {
            frame->size = pes_packet_length - 3 - pes_header_length;
        }
        frame->pts = pts;
        frame->dts = dts;

        frame->u.pes.stream_id = stream_id;
        frame->u.pes.pes_header_length = 9 + pes_header_length;

        MLOGV("FRAME(%u, %zu, %#" PRIx64 ", %#" PRIx64 ")",
                frame->u.pes.pes_header_length, frame->size, frame->pts, frame->dts);
    } else {
        MLOGE("invalid stream_id:%#x", stream_id);
    }

    return 0;
}

}
