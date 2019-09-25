/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Tuner_hidl_hal_test"

#include <VtsHalHidlTargetTestBase.h>
#include <VtsHalHidlTargetTestEnvBase.h>
#include <android-base/logging.h>
#include <android/hardware/tv/tuner/1.0/IDemux.h>
#include <android/hardware/tv/tuner/1.0/IDemuxCallback.h>
#include <android/hardware/tv/tuner/1.0/IDescrambler.h>
#include <android/hardware/tv/tuner/1.0/IFrontend.h>
#include <android/hardware/tv/tuner/1.0/IFrontendCallback.h>
#include <android/hardware/tv/tuner/1.0/ITuner.h>
#include <android/hardware/tv/tuner/1.0/types.h>
#include <binder/MemoryDealer.h>
#include <fmq/MessageQueue.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/Status.h>
#include <hidlmemory/FrameworkUtils.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <fstream>
#include <iostream>
#include <map>

#define WAIT_TIMEOUT 3000000000

using android::Condition;
using android::IMemory;
using android::IMemoryHeap;
using android::MemoryDealer;
using android::Mutex;
using android::sp;
using android::hardware::EventFlag;
using android::hardware::fromHeap;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::HidlMemory;
using android::hardware::kSynchronizedReadWrite;
using android::hardware::MessageQueue;
using android::hardware::MQDescriptorSync;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::tv::tuner::V1_0::DemuxDataFormat;
using android::hardware::tv::tuner::V1_0::DemuxFilterEvent;
using android::hardware::tv::tuner::V1_0::DemuxFilterPesEvent;
using android::hardware::tv::tuner::V1_0::DemuxFilterSectionEvent;
using android::hardware::tv::tuner::V1_0::DemuxFilterSettings;
using android::hardware::tv::tuner::V1_0::DemuxFilterStatus;
using android::hardware::tv::tuner::V1_0::DemuxFilterType;
using android::hardware::tv::tuner::V1_0::DemuxInputSettings;
using android::hardware::tv::tuner::V1_0::DemuxInputStatus;
using android::hardware::tv::tuner::V1_0::DemuxOutputStatus;
using android::hardware::tv::tuner::V1_0::DemuxQueueNotifyBits;
using android::hardware::tv::tuner::V1_0::FrontendAtscModulation;
using android::hardware::tv::tuner::V1_0::FrontendAtscSettings;
using android::hardware::tv::tuner::V1_0::FrontendDvbtSettings;
using android::hardware::tv::tuner::V1_0::FrontendEventType;
using android::hardware::tv::tuner::V1_0::FrontendId;
using android::hardware::tv::tuner::V1_0::FrontendInnerFec;
using android::hardware::tv::tuner::V1_0::FrontendScanMessage;
using android::hardware::tv::tuner::V1_0::FrontendScanMessageType;
using android::hardware::tv::tuner::V1_0::FrontendSettings;
using android::hardware::tv::tuner::V1_0::IDemux;
using android::hardware::tv::tuner::V1_0::IDemuxCallback;
using android::hardware::tv::tuner::V1_0::IDescrambler;
using android::hardware::tv::tuner::V1_0::IFrontend;
using android::hardware::tv::tuner::V1_0::IFrontendCallback;
using android::hardware::tv::tuner::V1_0::ITuner;
using android::hardware::tv::tuner::V1_0::Result;

namespace {

using FilterMQ = MessageQueue<uint8_t, kSynchronizedReadWrite>;
using MQDesc = MQDescriptorSync<uint8_t>;

const std::vector<uint8_t> goldenDataOutputBuffer{
        0x00, 0x00, 0x00, 0x01, 0x09, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x1e, 0xdb,
        0x01, 0x40, 0x16, 0xec, 0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0f, 0x03,
        0xc5, 0x8b, 0xb8, 0x00, 0x00, 0x00, 0x01, 0x68, 0xca, 0x8c, 0xb2, 0x00, 0x00, 0x01, 0x06,
        0x05, 0xff, 0xff, 0x70, 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8,
        0x20, 0xd9, 0x23, 0xee, 0xef, 0x78, 0x32, 0x36, 0x34, 0x20, 0x2d, 0x20, 0x63, 0x6f, 0x72,
        0x65, 0x20, 0x31, 0x34, 0x32, 0x20, 0x2d, 0x20, 0x48, 0x2e, 0x32, 0x36, 0x34, 0x2f, 0x4d,
        0x50, 0x45, 0x47, 0x2d, 0x34, 0x20, 0x41, 0x56, 0x43, 0x20, 0x63, 0x6f, 0x64, 0x65, 0x63,
        0x20, 0x2d, 0x20, 0x43, 0x6f, 0x70, 0x79, 0x6c, 0x65, 0x66, 0x74, 0x20, 0x32, 0x30, 0x30,
        0x33, 0x2d, 0x32, 0x30, 0x31, 0x34, 0x20, 0x2d, 0x20, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f,
        0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76, 0x69, 0x64, 0x65, 0x6f, 0x6c, 0x61, 0x6e, 0x2e, 0x6f,
        0x72, 0x67, 0x2f, 0x78, 0x32, 0x36, 0x34, 0x2e, 0x68, 0x74, 0x6d, 0x6c, 0x20, 0x2d, 0x20,
        0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x3a, 0x20, 0x63, 0x61, 0x62, 0x61, 0x63, 0x3d,
        0x30, 0x20, 0x72, 0x65, 0x66, 0x3d, 0x32, 0x20, 0x64, 0x65, 0x62, 0x6c, 0x6f, 0x63, 0x6b,
        0x3d, 0x31, 0x3a, 0x30, 0x3a, 0x30, 0x20, 0x61, 0x6e, 0x61, 0x6c, 0x79, 0x73, 0x65, 0x3d,
        0x30, 0x78, 0x31, 0x3a, 0x30, 0x78, 0x31, 0x31, 0x31, 0x20, 0x6d, 0x65, 0x3d, 0x68, 0x65,
        0x78, 0x20, 0x73, 0x75, 0x62, 0x6d, 0x65, 0x3d, 0x37, 0x20, 0x70, 0x73, 0x79, 0x3d, 0x31,
        0x20, 0x70, 0x73, 0x79, 0x5f, 0x72, 0x64, 0x3d, 0x31, 0x2e, 0x30, 0x30, 0x3a, 0x30, 0x2e,
        0x30, 0x30, 0x20, 0x6d, 0x69, 0x78, 0x65, 0x64, 0x5f, 0x72, 0x65, 0x66, 0x3d, 0x31, 0x20,
        0x6d, 0x65, 0x5f, 0x72, 0x61, 0x6e, 0x67, 0x65, 0x3d, 0x31, 0x36, 0x20, 0x63, 0x68, 0x72,
        0x6f, 0x6d, 0x61, 0x5f, 0x6d, 0x65, 0x3d, 0x31, 0x20, 0x74, 0x72, 0x65, 0x6c, 0x6c, 0x69,
        0x73, 0x3d, 0x31, 0x20, 0x38, 0x78, 0x38, 0x64, 0x63, 0x74, 0x3d, 0x30, 0x20, 0x63, 0x71,
        0x6d, 0x3d, 0x30, 0x20, 0x64, 0x65, 0x61, 0x64, 0x7a, 0x6f, 0x6e, 0x65, 0x3d, 0x32, 0x31,
        0x2c, 0x31, 0x31, 0x20, 0x66, 0x61, 0x73, 0x74, 0x5f, 0x70, 0x73, 0x6b, 0x69, 0x70, 0x3d,
        0x31, 0x20, 0x63, 0x68, 0x72, 0x6f, 0x6d, 0x61, 0x5f, 0x71, 0x70, 0x5f, 0x6f, 0x66, 0x66,
        0x73, 0x65, 0x74, 0x3d, 0x2d, 0x32, 0x20, 0x74, 0x68, 0x72, 0x65, 0x61, 0x64, 0x73, 0x3d,
        0x36, 0x30, 0x20, 0x6c, 0x6f, 0x6f, 0x6b, 0x61, 0x68, 0x65, 0x61, 0x64, 0x5f, 0x74, 0x68,
        0x72, 0x65, 0x61, 0x64, 0x73, 0x3d, 0x35, 0x20, 0x73, 0x6c, 0x69, 0x63, 0x65, 0x64, 0x5f,
        0x74, 0x68, 0x72, 0x65, 0x61, 0x64, 0x73, 0x3d, 0x30, 0x20, 0x6e, 0x72, 0x3d, 0x30, 0x20,
        0x64, 0x65, 0x63, 0x69, 0x6d, 0x61, 0x74, 0x65, 0x3d, 0x31, 0x20, 0x69, 0x6e, 0x74, 0x65,
        0x72, 0x6c, 0x61, 0x63, 0x65, 0x64, 0x3d, 0x30, 0x20, 0x62, 0x6c, 0x75, 0x72, 0x61, 0x79,
        0x5f, 0x63, 0x6f, 0x6d, 0x70, 0x61, 0x74, 0x3d, 0x30, 0x20, 0x63, 0x6f, 0x6e, 0x73, 0x74,
        0x72, 0x61, 0x69, 0x6e, 0x65, 0x64, 0x5f, 0x69, 0x6e, 0x74, 0x72, 0x61, 0x3d, 0x30, 0x20,
        0x62, 0x66, 0x72, 0x61, 0x6d, 0x65, 0x73, 0x3d, 0x30, 0x20, 0x77, 0x65, 0x69, 0x67, 0x68,
        0x74, 0x70, 0x3d, 0x30, 0x20, 0x6b, 0x65, 0x79, 0x69, 0x6e, 0x74, 0x3d, 0x32, 0x35, 0x30,
        0x20, 0x6b, 0x65, 0x79, 0x69, 0x6e, 0x74, 0x5f, 0x6d, 0x69, 0x6e, 0x3d, 0x32, 0x35, 0x20,
        0x73, 0x63, 0x65, 0x6e, 0x65,
};

const uint16_t FMQ_SIZE_4K = 0x1000;
const uint32_t FMQ_SIZE_1M = 0x100000;
// Equal to SECTION_WRITE_COUNT on the HAL impl side
// The HAL impl will repeatedly write to the FMQ the count times
const uint16_t SECTION_READ_COUNT = 10;

struct FilterConf {
    DemuxFilterType type;
    DemuxFilterSettings setting;
};

struct InputConf {
    string inputDataFile;
    DemuxInputSettings setting;
};

class FrontendCallback : public IFrontendCallback {
  public:
    virtual Return<void> onEvent(FrontendEventType frontendEventType) override {
        android::Mutex::Autolock autoLock(mMsgLock);
        mEventReceived = true;
        mEventType = frontendEventType;
        mMsgCondition.signal();
        return Void();
    }

    virtual Return<void> onDiseqcMessage(const hidl_vec<uint8_t>& diseqcMessage) override {
        android::Mutex::Autolock autoLock(mMsgLock);
        mDiseqcMessageReceived = true;
        mEventMessage = diseqcMessage;
        mMsgCondition.signal();
        return Void();
    }

    virtual Return<void> onScanMessage(FrontendScanMessageType /* type */,
                                       const FrontendScanMessage& /* message */) override {
        android::Mutex::Autolock autoLock(mMsgLock);
        mScanMessageReceived = true;
        mMsgCondition.signal();
        return Void();
    };

    void testOnEvent(sp<IFrontend>& frontend, FrontendSettings settings);
    void testOnDiseqcMessage(sp<IFrontend>& frontend, FrontendSettings settings);

  private:
    bool mEventReceived = false;
    bool mDiseqcMessageReceived = false;
    bool mScanMessageReceived = false;
    FrontendEventType mEventType;
    hidl_vec<uint8_t> mEventMessage;
    android::Mutex mMsgLock;
    android::Condition mMsgCondition;
};

void FrontendCallback::testOnEvent(sp<IFrontend>& frontend, FrontendSettings settings) {
    Result result = frontend->tune(settings);

    EXPECT_TRUE(result == Result::SUCCESS);

    android::Mutex::Autolock autoLock(mMsgLock);
    while (!mEventReceived) {
        if (-ETIMEDOUT == mMsgCondition.waitRelative(mMsgLock, WAIT_TIMEOUT)) {
            EXPECT_TRUE(false) << "event not received within timeout";
            return;
        }
    }
}

void FrontendCallback::testOnDiseqcMessage(sp<IFrontend>& frontend, FrontendSettings settings) {
    Result result = frontend->tune(settings);

    EXPECT_TRUE(result == Result::SUCCESS);

    android::Mutex::Autolock autoLock(mMsgLock);
    while (!mDiseqcMessageReceived) {
        if (-ETIMEDOUT == mMsgCondition.waitRelative(mMsgLock, WAIT_TIMEOUT)) {
            EXPECT_TRUE(false) << "diseqc message not received within timeout";
            return;
        }
    }
}

class DemuxCallback : public IDemuxCallback {
  public:
    virtual Return<void> onFilterEvent(const DemuxFilterEvent& filterEvent) override {
        ALOGW("[VTS] FILTER EVENT %d", filterEvent.filterId);
        android::Mutex::Autolock autoLock(mMsgLock);
        mFilterEventReceived = true;
        mFilterIdToEvent[filterEvent.filterId] = filterEvent;
        startFilterEventThread(filterEvent);
        mMsgCondition.signal();
        return Void();
    }

    virtual Return<void> onFilterStatus(uint32_t /*filterId*/,
                                        const DemuxFilterStatus /*status*/) override {
        return Void();
    }

    virtual Return<void> onOutputStatus(DemuxOutputStatus /*status*/) override { return Void(); }

    virtual Return<void> onInputStatus(DemuxInputStatus status) override {
        // android::Mutex::Autolock autoLock(mMsgLock);
        switch (status) {
            case DemuxInputStatus::SPACE_EMPTY:
            case DemuxInputStatus::SPACE_ALMOST_EMPTY:
                mKeepWritingInputFMQ = true;
                break;
            case DemuxInputStatus::SPACE_ALMOST_FULL:
            case DemuxInputStatus::SPACE_FULL:
                mKeepWritingInputFMQ = false;
                break;
        }
        return Void();
    }

    void testOnFilterEvent(uint32_t filterId);
    void testOnSectionFilterEvent(sp<IDemux>& demux, uint32_t filterId, MQDesc& filterMQDescriptor,
                                  MQDesc& inputMQDescriptor);
    void testFilterDataOutput();
    // Legacy
    bool readAndCompareSectionEventData(uint32_t filterId);

    void startPlaybackInputThread(InputConf inputConf, MQDesc& inputMQDescriptor);
    void startFilterEventThread(DemuxFilterEvent event);
    static void* __threadLoopInput(void* threadArgs);
    static void* __threadLoopFilter(void* threadArgs);
    void inputThreadLoop(InputConf inputConf, bool* keepWritingInputFMQ, MQDesc& inputMQDescriptor);
    void filterThreadLoop(DemuxFilterEvent& event);

    void updateFilterMQ(uint32_t filterId, MQDesc& filterMQDescriptor);
    void updateGoldenOutputMap(uint32_t filterId, string goldenOutputFile);

  private:
    struct InputThreadArgs {
        DemuxCallback* user;
        InputConf inputConf;
        bool* keepWritingInputFMQ;
        MQDesc& inputMQDesc;
    };
    struct FilterThreadArgs {
        DemuxCallback* user;
        DemuxFilterEvent& event;
    };
    uint16_t mDataLength = 0;
    std::vector<uint8_t> mDataOutputBuffer;

    bool mFilterEventReceived;
    std::map<uint32_t, string> mFilterIdToGoldenOutput;
    std::map<uint32_t, DemuxFilterEvent> mFilterIdToEvent;

    std::map<uint32_t, std::unique_ptr<FilterMQ>> mFilterIdToMQ;
    std::unique_ptr<FilterMQ> mInputMQ;
    std::map<uint32_t, EventFlag*> mFilterIdToMQEventFlag;
    EventFlag* mInputMQEventFlag;

    android::Mutex mMsgLock;
    android::Mutex mFilterOutputLock;
    android::Condition mMsgCondition;
    android::Condition mFilterOutputCondition;

    bool mKeepWritingInputFMQ;
    bool mInputThreadRunning;
    pthread_t mInputThread;
    pthread_t mFilterThread;
};

// Legacy
void DemuxCallback::testOnFilterEvent(uint32_t filterId) {
    android::Mutex::Autolock autoLock(mMsgLock);
    while (!mFilterEventReceived) {
        if (-ETIMEDOUT == mMsgCondition.waitRelative(mMsgLock, WAIT_TIMEOUT)) {
            EXPECT_TRUE(false) << "filter event not received within timeout";
            return;
        }
    }
    // Reset the filter event recieved flag
    mFilterEventReceived = false;
    // Check if filter id match
    EXPECT_TRUE(filterId == mFilterIdToEvent[filterId].filterId) << "filter id match";
}

void DemuxCallback::startPlaybackInputThread(InputConf inputConf, MQDesc& inputMQDescriptor) {
    struct InputThreadArgs* threadArgs =
            (struct InputThreadArgs*)malloc(sizeof(struct InputThreadArgs));
    threadArgs->user = this;
    threadArgs->inputConf = inputConf;
    threadArgs->keepWritingInputFMQ = &mKeepWritingInputFMQ;
    threadArgs->inputMQDesc = inputMQDescriptor;

    pthread_create(&mInputThread, NULL, __threadLoopInput, (void*)threadArgs);
    pthread_setname_np(mInputThread, "test_playback_input_loop");
}

void DemuxCallback::startFilterEventThread(DemuxFilterEvent event) {
    struct FilterThreadArgs* threadArgs =
            (struct FilterThreadArgs*)malloc(sizeof(struct FilterThreadArgs));
    threadArgs->user = this;
    threadArgs->event = event;

    pthread_create(&mFilterThread, NULL, __threadLoopFilter, (void*)threadArgs);
    pthread_setname_np(mFilterThread, "test_playback_input_loop");
}

void DemuxCallback::testFilterDataOutput() {
    android::Mutex::Autolock autoLock(mFilterOutputLock);
    while (!mFilterIdToMQ.empty()) {
        if (-ETIMEDOUT == mFilterOutputCondition.waitRelative(mFilterOutputLock, WAIT_TIMEOUT)) {
            EXPECT_TRUE(false) << "filter output does not match golden output within timeout";
            return;
        }
    }
}

// Legacy
void DemuxCallback::testOnSectionFilterEvent(sp<IDemux>& demux, uint32_t filterId,
                                             MQDesc& filterMQDescriptor,
                                             MQDesc& inputMQDescriptor) {
    Result status;
    // Create MQ to read the output into the local buffer
    mFilterIdToMQ[filterId] =
            std::make_unique<FilterMQ>(filterMQDescriptor, true /* resetPointers */);
    EXPECT_TRUE(mFilterIdToMQ[filterId]);
    // Get the MQ to write the input to the HAL
    mInputMQ = std::make_unique<FilterMQ>(inputMQDescriptor, true /* resetPointers */);
    EXPECT_TRUE(mInputMQ);
    // Create the EventFlag that is used to signal the HAL impl that data have been
    // read the Filter FMQ
    EXPECT_TRUE(EventFlag::createEventFlag(mFilterIdToMQ[filterId]->getEventFlagWord(),
                                           &mFilterIdToMQEventFlag[filterId]) == android::OK);
    // Create the EventFlag that is used to signal the HAL impl that data have been
    // written into the Input FMQ
    EXPECT_TRUE(EventFlag::createEventFlag(mInputMQ->getEventFlagWord(), &mInputMQEventFlag) ==
                android::OK);
    // Start filter
    status = demux->startFilter(filterId);
    status = demux->startInput();

    EXPECT_EQ(status, Result::SUCCESS);
    // Test start filter and receive callback event
    for (int i = 0; i < SECTION_READ_COUNT; i++) {
        // Write input FMQ and notify the Tuner Implementation
        EXPECT_TRUE(mInputMQ->write(goldenDataOutputBuffer.data(), goldenDataOutputBuffer.size()));
        mInputMQEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
        testOnFilterEvent(filterId);
        // checksum of mDataOutputBuffer and Input golden input
        if (readAndCompareSectionEventData(filterId) && i < SECTION_READ_COUNT - 1) {
            mFilterIdToMQEventFlag[filterId]->wake(
                    static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_CONSUMED));
        }
    }
}

// Legacy
bool DemuxCallback::readAndCompareSectionEventData(uint32_t filterId) {
    bool result = false;
    DemuxFilterEvent filterEvent = mFilterIdToEvent[filterId];
    for (int i = 0; i < filterEvent.events.size(); i++) {
        DemuxFilterSectionEvent event = filterEvent.events[i].section();
        mDataLength = event.dataLength;
        EXPECT_TRUE(mDataLength == goldenDataOutputBuffer.size()) << "buffer size does not match";

        mDataOutputBuffer.resize(mDataLength);
        result = mFilterIdToMQ[filterId]->read(mDataOutputBuffer.data(), mDataLength);
        EXPECT_TRUE(result) << "can't read from Filter MQ";

        for (int i = 0; i < mDataLength; i++) {
            EXPECT_TRUE(goldenDataOutputBuffer[i] == mDataOutputBuffer[i]) << "data does not match";
        }
    }
    return result;
}

void DemuxCallback::updateFilterMQ(uint32_t filterId, MQDesc& filterMQDescriptor) {
    mFilterIdToMQ[filterId] =
            std::make_unique<FilterMQ>(filterMQDescriptor, true /* resetPointers */);
    EXPECT_TRUE(mFilterIdToMQ[filterId]);
    EXPECT_TRUE(EventFlag::createEventFlag(mFilterIdToMQ[filterId]->getEventFlagWord(),
                                           &mFilterIdToMQEventFlag[filterId]) == android::OK);
}

void DemuxCallback::updateGoldenOutputMap(uint32_t filterId, string goldenOutputFile) {
    mFilterIdToGoldenOutput[filterId] = goldenOutputFile;
}

void* DemuxCallback::__threadLoopInput(void* threadArgs) {
    DemuxCallback* const self =
            static_cast<DemuxCallback*>(((struct InputThreadArgs*)threadArgs)->user);
    self->inputThreadLoop(((struct InputThreadArgs*)threadArgs)->inputConf,
                          ((struct InputThreadArgs*)threadArgs)->keepWritingInputFMQ,
                          ((struct InputThreadArgs*)threadArgs)->inputMQDesc);
    return 0;
}

void DemuxCallback::inputThreadLoop(InputConf inputConf, bool* keepWritingInputFMQ,
                                    MQDesc& inputMQDescriptor) {
    mInputThreadRunning = true;

    std::unique_ptr inputMQ =
            std::make_unique<FilterMQ>(inputMQDescriptor, true /* resetPointers */);
    EXPECT_TRUE(inputMQ);

    // Create the EventFlag that is used to signal the HAL impl that data have been
    // written into the Input FMQ
    EventFlag* inputMQEventFlag;
    EXPECT_TRUE(EventFlag::createEventFlag(inputMQ->getEventFlagWord(), &inputMQEventFlag) ==
                android::OK);

    // open the stream and get its length
    std::ifstream inputData(inputConf.inputDataFile /*"ts/test1.ts"*/, std::ifstream::binary);
    int writeSize = inputConf.setting.packetSize * 6;
    char* buffer = new char[writeSize];
    if (!inputData) {
        // log
        mInputThreadRunning = false;
    }

    while (mInputThreadRunning) {
        // move the stream pointer for packet size * 2k? every read until end
        while (*keepWritingInputFMQ) {
            inputData.read(buffer, writeSize);
            if (!inputData) {
                int leftSize = inputData.gcount();
                inputData.clear();
                inputData.read(buffer, leftSize);
                // Write the left over of the input data and quit the thread
                if (leftSize > 0) {
                    EXPECT_TRUE(inputMQ->write((unsigned char*)&buffer[0],
                                               leftSize / inputConf.setting.packetSize));
                    inputMQEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
                }
                mInputThreadRunning = false;
                break;
            }
            // Write input FMQ and notify the Tuner Implementation
            EXPECT_TRUE(inputMQ->write((unsigned char*)&buffer[0], 6));
            inputMQEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
            inputData.seekg(writeSize, inputData.cur);
        }
    }

    delete[] buffer;
    inputData.close();
}

void* DemuxCallback::__threadLoopFilter(void* threadArgs) {
    DemuxCallback* const self =
            static_cast<DemuxCallback*>(((struct FilterThreadArgs*)threadArgs)->user);
    self->filterThreadLoop(((struct FilterThreadArgs*)threadArgs)->event);
    return 0;
}

void DemuxCallback::filterThreadLoop(DemuxFilterEvent& /*event*/) {
    android::Mutex::Autolock autoLock(mFilterOutputLock);
    // Read from MQ[event.filterId] per event and filter type

    // Assemble to filterOutput[filterId]

    // check if filterOutput[filterId] matches goldenOutput[filterId]

    // If match, remove filterId entry from MQ map

    // end thread
}

// Test environment for Tuner HIDL HAL.
class TunerHidlEnvironment : public ::testing::VtsHalHidlTargetTestEnvBase {
  public:
    // get the test environment singleton
    static TunerHidlEnvironment* Instance() {
        static TunerHidlEnvironment* instance = new TunerHidlEnvironment;
        return instance;
    }

    virtual void registerTestServices() override { registerTestService<ITuner>(); }
};

class TunerHidlTest : public ::testing::VtsHalHidlTargetTestBase {
  public:
    virtual void SetUp() override {
        mService = ::testing::VtsHalHidlTargetTestBase::getService<ITuner>(
                TunerHidlEnvironment::Instance()->getServiceName<ITuner>());
        ASSERT_NE(mService, nullptr);
    }

    sp<ITuner> mService;

  protected:
    static void description(const std::string& description) {
        RecordProperty("description", description);
    }

    sp<IFrontend> mFrontend;
    sp<FrontendCallback> mFrontendCallback;
    sp<IDescrambler> mDescrambler;
    sp<IDemux> mDemux;
    sp<DemuxCallback> mDemuxCallback;
    MQDesc mFilterMQDescriptor;
    MQDesc mInputMQDescriptor;

    uint32_t mDemuxId;
    uint32_t mFilterId;

    pthread_t mInputThread;
    bool mInputThreadRunning;

    ::testing::AssertionResult createFrontend(int32_t frontendId);
    ::testing::AssertionResult tuneFrontend(int32_t frontendId);
    ::testing::AssertionResult stopTuneFrontend(int32_t frontendId);
    ::testing::AssertionResult closeFrontend(int32_t frontendId);
    ::testing::AssertionResult createDemux();
    ::testing::AssertionResult createDemuxWithFrontend(int32_t frontendId);
    ::testing::AssertionResult getInputMQDescriptor();
    ::testing::AssertionResult addInputToDemux(DemuxInputSettings setting);
    ::testing::AssertionResult addFilterToDemux(DemuxFilterType type, DemuxFilterSettings setting);
    ::testing::AssertionResult getFilterMQDescriptor(const uint32_t filterId);
    ::testing::AssertionResult closeDemux();
    ::testing::AssertionResult createDescrambler();
    ::testing::AssertionResult closeDescrambler();

    ::testing::AssertionResult playbackDataFlowTest(vector<FilterConf> filterConf,
                                                    InputConf inputConf,
                                                    vector<string> goldenOutputFiles);

    // Legacy
    ::testing::AssertionResult addSectionFilterToDemux();
    ::testing::AssertionResult readSectionFilterDataOutput();
};

::testing::AssertionResult TunerHidlTest::createFrontend(int32_t frontendId) {
    Result status;

    mService->openFrontendById(frontendId, [&](Result result, const sp<IFrontend>& frontend) {
        mFrontend = frontend;
        status = result;
    });
    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    mFrontendCallback = new FrontendCallback();
    auto callbackStatus = mFrontend->setCallback(mFrontendCallback);

    return ::testing::AssertionResult(callbackStatus.isOk());
}

::testing::AssertionResult TunerHidlTest::tuneFrontend(int32_t frontendId) {
    if (createFrontend(frontendId) == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    // Frontend Settings for testing
    FrontendSettings frontendSettings;
    FrontendAtscSettings frontendAtscSettings{
            .frequency = 0,
            .modulation = FrontendAtscModulation::UNDEFINED,
    };
    frontendSettings.atsc() = frontendAtscSettings;
    mFrontendCallback->testOnEvent(mFrontend, frontendSettings);

    FrontendDvbtSettings frontendDvbtSettings{
            .frequency = 0,
    };
    frontendSettings.dvbt(frontendDvbtSettings);
    mFrontendCallback->testOnEvent(mFrontend, frontendSettings);

    return ::testing::AssertionResult(true);
}

::testing::AssertionResult TunerHidlTest::stopTuneFrontend(int32_t frontendId) {
    Result status;
    if (!mFrontend && createFrontend(frontendId) == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mFrontend->stopTune();
    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::closeFrontend(int32_t frontendId) {
    Result status;
    if (!mFrontend && createFrontend(frontendId) == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mFrontend->close();
    mFrontend = nullptr;
    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::createDemux() {
    Result status;

    mService->openDemux([&](Result result, uint32_t demuxId, const sp<IDemux>& demux) {
        mDemux = demux;
        mDemuxId = demuxId;
        status = result;
    });
    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::createDemuxWithFrontend(int32_t frontendId) {
    Result status;

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    if (!mFrontend && createFrontend(frontendId) == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mDemux->setFrontendDataSource(frontendId);

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::closeDemux() {
    Result status;
    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mDemux->close();
    mDemux = nullptr;
    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::createDescrambler() {
    Result status;

    mService->openDescrambler([&](Result result, const sp<IDescrambler>& descrambler) {
        mDescrambler = descrambler;
        status = result;
    });
    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mDescrambler->setDemuxSource(mDemuxId);
    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    // Test if demux source can be set more than once.
    status = mDescrambler->setDemuxSource(mDemuxId);
    return ::testing::AssertionResult(status == Result::INVALID_STATE);
}

::testing::AssertionResult TunerHidlTest::closeDescrambler() {
    Result status;
    if (!mDescrambler && createDescrambler() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    status = mDescrambler->close();
    mDescrambler = nullptr;
    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::addInputToDemux(DemuxInputSettings setting) {
    Result status;

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    // Create demux callback
    if (!mDemuxCallback) {
        mDemuxCallback = new DemuxCallback();
    }

    // Add section filter to the local demux
    status = mDemux->addInput(FMQ_SIZE_1M, mDemuxCallback);

    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    status = mDemux->configureInput(setting);

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::getInputMQDescriptor() {
    Result status;

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    mDemux->getInputQueueDesc([&](Result result, const MQDesc& inputMQDesc) {
        mInputMQDescriptor = inputMQDesc;
        status = result;
    });

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

// Legacy
::testing::AssertionResult TunerHidlTest::addSectionFilterToDemux() {
    Result status;

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    // Create demux callback
    if (!mDemuxCallback) {
        mDemuxCallback = new DemuxCallback();
    }

    // Add section filter to the local demux
    mDemux->addFilter(DemuxFilterType::SECTION, FMQ_SIZE_4K, mDemuxCallback,
                      [&](Result result, uint32_t filterId) {
                          mFilterId = filterId;
                          status = result;
                      });

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::addFilterToDemux(DemuxFilterType type,
                                                           DemuxFilterSettings setting) {
    Result status;

    if (!mDemux && createDemux() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    // Create demux callback
    if (!mDemuxCallback) {
        mDemuxCallback = new DemuxCallback();
    }

    // Add filter to the local demux
    mDemux->addFilter(type, FMQ_SIZE_4K, mDemuxCallback, [&](Result result, uint32_t filterId) {
        // TODO use a map to save all the filter id and FMQ
        mFilterId = filterId;
        status = result;
    });

    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    // Configure the filter
    status = mDemux->configureFilter(mFilterId, setting);

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

::testing::AssertionResult TunerHidlTest::getFilterMQDescriptor(const uint32_t filterId) {
    Result status;

    if (!mDemux) {
        return ::testing::AssertionFailure();
    }

    mDemux->getFilterQueueDesc(filterId, [&](Result result, const MQDesc& filterMQDesc) {
        mFilterMQDescriptor = filterMQDesc;
        status = result;
    });

    return ::testing::AssertionResult(status == Result::SUCCESS);
}

// Legacy
::testing::AssertionResult TunerHidlTest::readSectionFilterDataOutput() {
    // Filter Configuration Module
    DemuxInputSettings setting{
            .statusMask = 0xf,
            .lowThreshold = 0x1000,
            .highThreshold = 0x100000,
            .dataFormat = DemuxDataFormat::TS,
            .packetSize = 188,
    };
    if (addSectionFilterToDemux() == ::testing::AssertionFailure() ||
        getFilterMQDescriptor(mFilterId) == ::testing::AssertionFailure() ||
        addInputToDemux(setting) == ::testing::AssertionFailure() ||
        getInputMQDescriptor() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }

    // Data Verify Module
    // Test start filter and read the output data
    mDemuxCallback->testOnSectionFilterEvent(mDemux, mFilterId, mFilterMQDescriptor,
                                             mInputMQDescriptor);

    // Clean Up Module
    return closeDemux();  //::testing::AssertionSuccess();
}

::testing::AssertionResult TunerHidlTest::playbackDataFlowTest(vector<FilterConf> filterConf,
                                                               InputConf inputConf,
                                                               vector<string> goldenOutputFiles) {
    Result status;
    // Filter Configuration Module
    for (int i = 0; i < filterConf.size(); i++) {
        if (addFilterToDemux(filterConf[i].type, filterConf[i].setting) ==
                    ::testing::AssertionFailure() ||
            // TODO use a map to save the FMQs/EvenFlags and pass to callback
            getFilterMQDescriptor(mFilterId) == ::testing::AssertionFailure()) {
            return ::testing::AssertionFailure();
        }
        mDemuxCallback->updateFilterMQ(mFilterId, mFilterMQDescriptor);
        mDemuxCallback->updateGoldenOutputMap(mFilterId, goldenOutputFiles[i]);
        status = mDemux->startFilter(mFilterId);
        if (status != Result::SUCCESS) {
            return ::testing::AssertionFailure();
        }
    }

    // Playback Input Module
    DemuxInputSettings inputSetting = inputConf.setting;
    if (addInputToDemux(inputSetting) == ::testing::AssertionFailure() ||
        getInputMQDescriptor() == ::testing::AssertionFailure()) {
        return ::testing::AssertionFailure();
    }
    mDemuxCallback->startPlaybackInputThread(inputConf, mInputMQDescriptor);
    status = mDemux->startInput();
    if (status != Result::SUCCESS) {
        return ::testing::AssertionFailure();
    }

    // Data Verify Module
    mDemuxCallback->testFilterDataOutput();

    // Clean Up Module
    return closeDemux();
}

/*
 * API STATUS TESTS
 */
TEST_F(TunerHidlTest, CreateFrontend) {
    Result status;
    hidl_vec<FrontendId> feIds;

    description("Create Frontends");
    mService->getFrontendIds([&](Result result, const hidl_vec<FrontendId>& frontendIds) {
        status = result;
        feIds = frontendIds;
    });

    if (feIds.size() == 0) {
        ALOGW("[   WARN   ] Frontend isn't available");
        return;
    }

    for (size_t i = 0; i < feIds.size(); i++) {
        ASSERT_TRUE(createFrontend(feIds[i]));
    }
}

TEST_F(TunerHidlTest, TuneFrontend) {
    Result status;
    hidl_vec<FrontendId> feIds;

    description("Tune Frontends and check callback onEvent");
    mService->getFrontendIds([&](Result result, const hidl_vec<FrontendId>& frontendIds) {
        status = result;
        feIds = frontendIds;
    });

    if (feIds.size() == 0) {
        ALOGW("[   WARN   ] Frontend isn't available");
        return;
    }

    for (size_t i = 0; i < feIds.size(); i++) {
        ASSERT_TRUE(tuneFrontend(feIds[i]));
    }
}

TEST_F(TunerHidlTest, StopTuneFrontend) {
    Result status;
    hidl_vec<FrontendId> feIds;

    description("stopTune Frontends");
    mService->getFrontendIds([&](Result result, const hidl_vec<FrontendId>& frontendIds) {
        status = result;
        feIds = frontendIds;
    });

    if (feIds.size() == 0) {
        ALOGW("[   WARN   ] Frontend isn't available");
        return;
    }

    for (size_t i = 0; i < feIds.size(); i++) {
        ASSERT_TRUE(stopTuneFrontend(feIds[i]));
    }
}

TEST_F(TunerHidlTest, CloseFrontend) {
    Result status;
    hidl_vec<FrontendId> feIds;

    description("Close Frontends");
    mService->getFrontendIds([&](Result result, const hidl_vec<FrontendId>& frontendIds) {
        status = result;
        feIds = frontendIds;
    });

    if (feIds.size() == 0) {
        ALOGW("[   WARN   ] Frontend isn't available");
        return;
    }

    for (size_t i = 0; i < feIds.size(); i++) {
        ASSERT_TRUE(closeFrontend(feIds[i]));
    }
}

TEST_F(TunerHidlTest, CreateDemuxWithFrontend) {
    Result status;
    hidl_vec<FrontendId> feIds;

    description("Create Demux with Frontend");
    mService->getFrontendIds([&](Result result, const hidl_vec<FrontendId>& frontendIds) {
        status = result;
        feIds = frontendIds;
    });

    if (feIds.size() == 0) {
        ALOGW("[   WARN   ] Frontend isn't available");
        return;
    }

    for (size_t i = 0; i < feIds.size(); i++) {
        ASSERT_TRUE(createDemuxWithFrontend(feIds[i]));
    }
}

TEST_F(TunerHidlTest, CreateDemux) {
    description("Create Demux");
    ASSERT_TRUE(createDemux());
}

TEST_F(TunerHidlTest, CloseDemux) {
    description("Close Demux");
    ASSERT_TRUE(closeDemux());
}

TEST_F(TunerHidlTest, CreateDescrambler) {
    description("Create Descrambler");
    ASSERT_TRUE(createDescrambler());
}

TEST_F(TunerHidlTest, CloseDescrambler) {
    description("Close Descrambler");
    ASSERT_TRUE(closeDescrambler());
}

/*
 * DATA FLOW TESTS
 */
TEST_F(TunerHidlTest, ReadSectionFilterOutput) {
    description("Read data output from FMQ of a Section Filter");
    ASSERT_TRUE(readSectionFilterDataOutput());
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::AddGlobalTestEnvironment(TunerHidlEnvironment::Instance());
    ::testing::InitGoogleTest(&argc, argv);
    TunerHidlEnvironment::Instance()->init(&argc, argv);
    int status = RUN_ALL_TESTS();
    LOG(INFO) << "Test result = " << status;
    return status;
}
