/*
 * Copyright 2015 The Android Open Source Project
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

#include <cstring>
#include <cstdlib>
#include "audio_decoder.h"

void PlayEventCallback(SLPlayItf caller, void *pContext,  SLuint32 recordevent)
{
    LOGI("AudioRecorder::PlayEventCallback - recordevent: %d", recordevent);
    (static_cast<AudioDecoder *>(pContext))->Rewind(caller);
}

/*
 * bqDecoderCallback(): called for every buffer is full;
 *                       pass directly to handler
 */
void bqDecoderCallback(SLAndroidSimpleBufferQueueItf bq, void *rec) {
    (static_cast<AudioDecoder *>(rec))->ProcessSLCallback(bq);
}

void AudioDecoder::ProcessSLCallback(SLAndroidSimpleBufferQueueItf bq) {
#ifdef ENABLE_LOG
    recLog_->logTime();
#endif
    assert(bq == recBufQueueItf_);
    sample_buf *dataBuf = NULL;
    devShadowQueue_->front(&dataBuf);
    devShadowQueue_->pop();
    dataBuf->size_ = dataBuf->cap_;           //device only calls us when it is really full
    recQueue_->push(dataBuf);

    sample_buf* freeBuf;
    while (freeQueue_->front(&freeBuf) && devShadowQueue_->push(freeBuf)) {
        freeQueue_->pop();
        SLresult result = (*bq)->Enqueue(bq, freeBuf->buf_, freeBuf->cap_);
        SLASSERT(result);
    }

    /*
     * PLAY_KICKSTART_BUFFER_COUNT: # of buffers cached in the queue before
     * STARTING player. it is defined in audio_common.h. Whatever buffered
     * here is the part of the audio LATENCY! adjust to fit your bill [ until
     * it busts ]
     */
    if(++audioBufCount == PLAY_KICKSTART_BUFFER_COUNT && callback_) {
        callback_(ctx_, ENGINE_SERVICE_MSG_KICKSTART_PLAYER, NULL);
    }

    // should leave the device to sleep to save power if no buffers
    if (devShadowQueue_->size() == 0) {
        (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_STOPPED);
    }

    //if the record queue is full, pause the PCM decoder
    if (recQueue_->size()>=BUF_COUNT-2*DEVICE_SHADOW_BUFFER_QUEUE_LEN)
    {
        LOGI("AudioRecorder::ProcessSLCallback - SL_PLAYSTATE_PAUSED");
        (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_PAUSED);
    }

}

AudioDecoder::AudioDecoder(SampleFormat *sampleFormat, SLEngineItf slEngine, const char* uri) :
        freeQueue_(nullptr), devShadowQueue_(nullptr), recQueue_(nullptr),
        callback_(nullptr)
{
    SLresult result;
    sampleInfo_ = *sampleFormat;
    SLAndroidDataFormat_PCM_EX format_pcm;
    ConvertToSLSampleFormat(&format_pcm, &sampleInfo_);


    // configure audio sink
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            DEVICE_SHADOW_BUFFER_QUEUE_LEN };

    //although we provide PCM format to the player, the output PCM is matching the PCM format of the
    //source file (it's not converted to the PCM format we specify). To use the fast path conversion
    //of PCM data must be implemented
    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    // configure audio source
    // (requires the INTERNET permission depending on the uri parameter)
    SLDataLocator_URI loc_uri = {SL_DATALOCATOR_URI, (SLchar *) uri};
    SLDataFormat_MIME format_mime = {SL_DATAFORMAT_MIME, NULL, SL_CONTAINERTYPE_UNSPECIFIED};
    SLDataSource audioSrc = {&loc_uri, &format_mime};

    // create audio decoder
    // (requires the RECORD_AUDIO permission)
    const SLInterfaceID id[2] = {SL_IID_SEEK, SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*slEngine)->CreateAudioPlayer(slEngine,
                                              &recObjectItf_,
                                              &audioSrc,
                                              &audioSnk,
                                              2, id, req);
    SLASSERT(result);

    result = (*recObjectItf_)->Realize(recObjectItf_, SL_BOOLEAN_FALSE);
    SLASSERT(result);
    result = (*recObjectItf_)->GetInterface(recObjectItf_,
                                            SL_IID_PLAY, &recItf_);
    SLASSERT(result);

    // get the seek interface
    result = (*recObjectItf_)->GetInterface(recObjectItf_, SL_IID_SEEK, &seekItf_);
    SLASSERT(result);

    result = (*recObjectItf_)->GetInterface(recObjectItf_,
                                            SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &recBufQueueItf_);
    SLASSERT(result);

    result = (*recBufQueueItf_)->RegisterCallback(recBufQueueItf_,
                                                  bqDecoderCallback, this);
    SLASSERT(result);

    // make sure the URI audio player was created
    if (NULL != seekItf_) {

        // set the looping state. Doesn't work for decoding it seems:
        //Decode to PCM supports pause and initial seek. Volume control, effects, looping, and playback rate are not supported.
        //result = (*seekItf_)->SetLoop(seekItf_, SL_BOOLEAN_TRUE, 0, SL_TIME_UNKNOWN);
        //SLASSERT(result);
    }

    /* Setup to receive position event callbacks */
    result = (*recItf_)->RegisterCallback(recItf_, PlayEventCallback, this);
    SLASSERT(result);

    result = (*recItf_)->SetCallbackEventsMask( recItf_, SL_PLAYEVENT_HEADATEND);
    SLASSERT(result);

    devShadowQueue_ = new AudioQueue(DEVICE_SHADOW_BUFFER_QUEUE_LEN);
    assert(devShadowQueue_);
#ifdef ENABLE_LOG
    std::string name = "rec";
    recLog_ = new AndroidLog(name);
#endif
}

SLboolean AudioDecoder::Resume(void) {
    LOGI("AudioDecoder::Resume - SL_PLAYSTATE_PLAYING");
    if (recItf_ != NULL) {
        (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_PLAYING);
    }
    else
    {
        LOGE("AudioDecoder::Resume - recItf_ IS null");
    }
}

void AudioDecoder::Rewind(SLPlayItf caller) {
    LOGI("AudioRecorder::Rewind");

    //I could not find another way to restart the decoding, but to recreate the decoder again.
    //I tried setting play head position but it didn't work...
    callback_(ctx_,ENGINE_SERVICE_DECODING_FINISHED, NULL);
}


SLboolean AudioDecoder::Start(void) {
    if(!freeQueue_ || !recQueue_ || !devShadowQueue_) {
        LOGE("====NULL poiter to Start(%p, %p, %p)", freeQueue_, recQueue_, devShadowQueue_);
        return SL_BOOLEAN_FALSE;
    }
    audioBufCount = 0;

    SLresult result;
    // in case already recording, stop recording and clear buffer queue
    result = (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_STOPPED);
    SLASSERT(result);
    //result = (*recBufQueueItf_)->Clear(recBufQueueItf_);
    //SLASSERT(result);

    for(int i =0; i < RECORD_DEVICE_KICKSTART_BUF_COUNT; i++ ) {
        sample_buf *buf = NULL;
        if(!freeQueue_->front(&buf)) {
            LOGE("=====OutOfFreeBuffers @ startingRecording @ (%d)", i);
            break;
        }
        freeQueue_->pop();
        assert(buf->buf_ && buf->cap_ && !buf->size_);

        result = (*recBufQueueItf_)->Enqueue(recBufQueueItf_, buf->buf_,
                                             buf->cap_);
        SLASSERT(result);
        devShadowQueue_->push(buf);
    }

    result = (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_PLAYING);
    SLASSERT(result);

    return (result == SL_RESULT_SUCCESS? SL_BOOLEAN_TRUE:SL_BOOLEAN_FALSE);
}

SLboolean AudioDecoder::SoftStart(void) {
    audioBufCount = PLAY_KICKSTART_BUFFER_COUNT+1;//we don't want to kickstart the player as it's supposed to be running already
    SLresult result;

    result = (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_STOPPED);
    SLASSERT(result);

    sample_buf *buf = NULL;
    if(!freeQueue_->front(&buf)) {
        LOGE("=====OutOfFreeBuffers @ softStartingRecording");
    }
    else
    {
        freeQueue_->pop();
        assert(buf->buf_ && buf->cap_ && !buf->size_);
        result = (*recBufQueueItf_)->Enqueue(recBufQueueItf_, buf->buf_, buf->cap_);
        SLASSERT(result);
        devShadowQueue_->push(buf);
    }

    result = (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_PLAYING);
    SLASSERT(result);

    return (result == SL_RESULT_SUCCESS? SL_BOOLEAN_TRUE:SL_BOOLEAN_FALSE);
}

SLboolean  AudioDecoder::Stop(void) {
    // in case already recording, stop recording and clear buffer queue
    SLuint32 curState;

    SLresult result = (*recItf_)->GetPlayState(recItf_, &curState);
    SLASSERT(result);
    if( curState == SL_PLAYSTATE_STOPPED) {
        return SL_BOOLEAN_TRUE;
    }
    result = (*recItf_)->SetPlayState(recItf_, SL_PLAYSTATE_STOPPED);
    SLASSERT(result);
    //result = (*recBufQueueItf_)->Clear(recBufQueueItf_);
    //SLASSERT(result);

    sample_buf *buf = NULL;
    while(devShadowQueue_->front(&buf)) {
        devShadowQueue_->pop();
        freeQueue_->push(buf);
    }

#ifdef ENABLE_LOG
    recLog_->flush();
#endif

    return SL_BOOLEAN_TRUE;
}

AudioDecoder::~AudioDecoder() {
    // destroy audio decoder object, and invalidate all associated interfaces
    if (recObjectItf_ != NULL) {
        (*recObjectItf_)->Destroy(recObjectItf_);
    }

    if(devShadowQueue_)
        delete (devShadowQueue_);
#ifdef  ENABLE_LOG
    if(recLog_) {
        delete recLog_;
    }
#endif
}

void AudioDecoder::SetBufQueues(AudioQueue *freeQ, AudioQueue *recQ) {
    assert(freeQ && recQ);
    freeQueue_ = freeQ;
    recQueue_ = recQ;
}

void AudioDecoder::RegisterCallback(ENGINE_CALLBACK cb, void *ctx) {
    callback_ = cb;
    ctx_ = ctx;
}

int32_t AudioDecoder::dbgGetDevBufCount(void) {
    return devShadowQueue_->size();
}
