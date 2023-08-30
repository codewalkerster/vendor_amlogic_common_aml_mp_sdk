/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef __AML_MP_BUFFER_QUEUE_H_
#define __AML_MP_BUFFER_QUEUE_H_

#include <memory>
#include <thread>
#include <list>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <set>
#include <atomic>
#include <utils/AmlMpRefBase.h>
#include <Aml_MP/Common.h>
#include <utils/AmlMpBuffer.h>

namespace aml_mp {
////////////////////////////////////////////////////////////////////////////////
class BufferQueue : public AmlMpRefBase
{
public:
    class Buffer;
    class SecBuffer;
    class Consumer;

    BufferQueue(Aml_MP_StreamType streamType, size_t bufferCount, size_t bufferSize, bool isSVP=false, bool isUHD = false);
    ~BufferQueue();

    int dequeueBuffer(Buffer** buffer);
    int queueBuffer(Buffer* buffer);
    int cancelBuffer(Buffer* buffer);
    Buffer* toBuffer(const uint8_t* base);

    void getFlowStatus(bool* underflow, bool* overflow);

    size_t getBufferQueueSize() const {
        std::lock_guard<std::mutex> _l(mBufferLock);
        return mBufferQueueSize;
    }

    size_t getRecyleQueueSize() const {
        std::lock_guard<std::mutex> _l(mRecycleBufferLock);
        return mRecycleQueueSize;
    }

private:
    void initSessionIfNeeded();
    int acquireBuffer(std::shared_ptr<Buffer>* buffer);
    int releaseBuffer(const std::shared_ptr<Buffer>& buffer);
    void flush();
    bool stopReceive();
    void restoreReceive(bool state = false);

    int lastDequeueResult() const {
        return mLastDequeueBufferResult.load(std::memory_order_relaxed);
    }

    int recycleBuffer(const std::shared_ptr<Buffer>& buffer);
    std::shared_ptr<Buffer> getRecycledBuffer(int64_t handle);

private:
    Aml_MP_StreamType mStreamType;

    std::mutex mDequeuedBufferLock;
    std::set<std::shared_ptr<Buffer>> mDequeuedBuffers;

    mutable std::mutex mBufferLock;
    std::condition_variable mBufferCond;
    std::list<std::shared_ptr<Buffer>> mBuffers;
    size_t mBufferQueueSize = 0;

    std::mutex mFreeBufferLock;
    std::list <std::shared_ptr<Buffer>> mFreeBuffers;

    mutable std::mutex mRecycleBufferLock;
    std::list<std::shared_ptr<Buffer>> mRecycleBuffers;
    size_t mRecycleQueueSize = 0;

    size_t mMaxBufferCount;
    size_t mBufferCapacity;
    bool mIsSecure;
    bool mIsUHD;
    size_t mBufferCount;

    std::atomic<int> mLastDequeueBufferResult; //accessed by producer and consumer
    std::atomic<bool> mStopped;

    //secmem
    void* mSecMemSession = nullptr;

private:
    BufferQueue(const BufferQueue&) = delete;
    BufferQueue& operator=(const BufferQueue&) = delete;
};

class BufferQueue::Buffer
{
public:
    static Buffer* create(size_t size, void* secMemSession, Aml_MP_StreamType streamType);

    virtual ~Buffer();
    virtual int resize(size_t capacity __unused) { return 0; }
    virtual size_t append(const uint8_t* data, size_t size);
    virtual int prepareReaderView();
    virtual bool needRecycle() const;
    int64_t recycleHandle() const { return mRecycleHandle; }

	Buffer* readerView() const { return mReaderView; }

    uint8_t* base() { return (uint8_t*)mData; }
    uint8_t* data() { return (uint8_t*)mData + mRangeOffset; }
    size_t capacity() const { return mCapacity; }
    size_t size() const { return mRangeLength; }
    size_t offset() const { return mRangeOffset; }
    void setRange(size_t offset, size_t size);
    void reset();
    uint64_t pts() const { return mPts; }
    void setPts(uint64_t pts) { mPts = pts; }

protected:
    explicit Buffer(size_t capacity, Aml_MP_StreamType streamType);

    void* mData = nullptr; //normal or secure buffer pointer
    size_t mCapacity = 0;

    size_t mRangeOffset = 0;
    size_t mRangeLength = 0;

    int64_t mPts = 0;

    Buffer* mReaderView = nullptr;
    int64_t mRecycleHandle = -1;
    Aml_MP_StreamType mStreamType = AML_MP_STREAM_TYPE_UNKNOWN;
    int mIsNonTunnelMode = true;

private:
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
class BufferQueue::SecBuffer : public Buffer
{
public:
    virtual ~SecBuffer();

protected:
    friend Buffer* Buffer::create(size_t, void*, Aml_MP_StreamType);
    SecBuffer(size_t capacity, void* secMemSession, Aml_MP_StreamType streamType);

    virtual int resize(size_t capacity) override;
    virtual size_t append(const uint8_t* data, size_t size) override;
    virtual int prepareReaderView() override;
    virtual bool needRecycle() const override { return true; }

private:
    void* mSecMemSession = nullptr;
    uint32_t mHandle = UINT32_MAX;
    uint32_t mPhyAddr = UINT32_MAX;

    SecBuffer(const SecBuffer&) = delete;
    SecBuffer& operator=(const SecBuffer&) = delete;
};

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
class BufferQueue::Consumer : public AmlMpRefBase
{
public:
    explicit Consumer(Aml_MP_StreamType streamType, const std::string& name);
    ~Consumer();
    void connectQueue(BufferQueue* bufferQueue);
    void connectSink(const std::function<int(const uint8_t*, size_t, uint64_t pts)>& fn);
    void setBufferAvailableCb(const std::function<void()>& cb);
    int start();
    int pause();
    int stop();
    int notifyBufferReleased(int64_t handle);
    int flush();

private:
    int threadLoop();

    enum Work {
        kWorkFeedData = 1 << 0,
        kWorkStart = 1 << 1,
        kWorkPause = 1 << 2,
        kWorkRecycleBuffer = 1 << 3,
        kWorkFlush = 1 << 4,
        kWorkQuit = 1 << 31,
    };

    void sendWorkCommand(Work work) {
        std::lock_guard<std::mutex> _l(mLock);
        mWork |= work;
        mWorkQueue.push_back(work);
        mCond.notify_all();
    }

    void sendWorkCommandAndWait(Work work) {
        if (mWorker.get_id() == std::thread::id()) {
            return;
        }

        {
            std::unique_lock<std::mutex> _l(mLock);
            mWork |= work;
            mWorkQueue.push_back(work);
            mCond.notify_all();
        }

        std::unique_lock<std::mutex> _l(mLock);
        mCond.wait(_l, [this, work]{ return (mWork&work) == 0; });
    }

    void signalWorkDone(Work work) {
        std::lock_guard<std::mutex> _l(mLock);
        mWork &= ~ work;
        mWorkQueue.pop_front();
        mCond.notify_all();
    }

    Aml_MP_StreamType mStreamType;
    char mConsumerName[40];
    char mName[40];

    std::mutex mLock;
    std::condition_variable mCond;
    std::list<Work> mWorkQueue;
    uint32_t mWork{};

    bool mStarted = false;

    BufferQueue* mBufferQueue = nullptr;
    std::thread mWorker;
    std::function<int(const uint8_t*, size_t, uint64_t)> mWriter;
    std::function<void()> mBufferAvailableCb;

    uint32_t mAcquiredCount = 0;
    uint32_t mReleasedCount = 0;
    uint32_t mRecycledCount = 0;

    int mDumpFd = -1;

    std::mutex mRecycleHandleLock;
    std::list<int64_t> mRecycleHandles;

private:
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
class StreamParser : public AmlMpRefBase {
public:
    typedef enum {
        PES,
        ES,
        MP4,
    } StreamCategory;

    struct PESHeader {
        uint8_t stream_id;
        uint16_t pes_header_length;
    };

    struct Frame {
        const uint8_t* data;
        size_t size;
        uint64_t pts;
        uint64_t dts;
        union {
            struct PESHeader pes;
        } u;
    };

    StreamParser(Aml_MP_StreamType streamType, StreamCategory category);
    ~StreamParser() = default;

    int append(const uint8_t* data, size_t size);
    size_t totalBufferSize() const {
        return mBuffer ? mBuffer->size() : 0;
    }
    int lockFrame(Frame* frame);
    int unlockFrame(Frame* frame);

private:
    int findPES(const uint8_t* data, size_t size);
    int parsePES(const uint8_t* data, size_t size, Frame* frame);

    char mName[40];
    Aml_MP_StreamType mStreamType;
    StreamCategory mStreamCategory;
    sptr<AmlMpBuffer> mBuffer;

    StreamParser(const StreamParser&) = delete;
    StreamParser& operator=(const StreamParser&) = delete;
};

}








#endif
