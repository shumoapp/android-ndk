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
#include <cassert>
#include <cstring>
#include <jni.h>

#include <sys/types.h>
#include <SLES/OpenSLES.h>

#include "audio_common.h"
#include "audio_decoder.h"
#include "audio_recorder.h"
#include "audio_player.h"

struct EchoAudioEngine {
    SLmilliHertz fastPathSampleRate_;
    uint32_t     fastPathFramesPerBuf_;
    uint16_t     sampleChannels_;
    uint16_t     bitsPerSample_;

    SLObjectItf  slEngineObj_;
    SLEngineItf  slEngineItf_;

    AudioRecorder  *recorder_;
    AudioDecoder   *decoder_;
    AudioPlayer    *player_;
    AudioQueue     *freeBufQueue_;    //Owner of the queue
    AudioQueue     *recBufQueue_;     //Owner of the queue

    sample_buf  *bufs_;
    uint32_t     bufCount_;
    uint32_t     frameCount_;
};
static EchoAudioEngine engine;

std::string uriString;

bool EngineService(void* ctx, uint32_t msg, void* data );

extern "C" {
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_createSLEngine(JNIEnv *env, jclass, jint, jint);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_deleteSLEngine(JNIEnv *env, jclass type);
JNIEXPORT jboolean JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_createSLBufferQueueAudioPlayer(JNIEnv *env, jclass);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_deleteSLBufferQueueAudioPlayer(JNIEnv *env, jclass type);

JNIEXPORT jboolean JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_createAudioRecorder(JNIEnv *env, jclass type);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_deleteAudioRecorder(JNIEnv *env, jclass type);
JNIEXPORT jboolean JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_createAudioDecoder(JNIEnv *env, jclass type, jbyteArray uri);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_deleteAudioDecoder(JNIEnv *env, jclass type);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_startPlay(JNIEnv *env, jclass type);
JNIEXPORT void JNICALL
        Java_com_google_sample_echo_NativeFastPlayer_stopPlay(JNIEnv *env, jclass type);
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_createSLEngine(
        JNIEnv *env, jclass type, jint sampleRate, jint framesPerBuf) {
    SLresult result;
    memset(&engine, 0, sizeof(engine));

    engine.fastPathSampleRate_   = static_cast<SLmilliHertz>(sampleRate) * 1000;
    engine.fastPathFramesPerBuf_ = static_cast<uint32_t>(framesPerBuf);
    engine.sampleChannels_   = AUDIO_SAMPLE_CHANNELS;
    engine.bitsPerSample_    = SL_PCMSAMPLEFORMAT_FIXED_16;

    result = slCreateEngine(&engine.slEngineObj_, 0, NULL, 0, NULL, NULL);
    SLASSERT(result);

    result = (*engine.slEngineObj_)->Realize(engine.slEngineObj_, SL_BOOLEAN_FALSE);
    SLASSERT(result);

    result = (*engine.slEngineObj_)->GetInterface(engine.slEngineObj_, SL_IID_ENGINE, &engine.slEngineItf_);
    SLASSERT(result);

    // compute the RECOMMENDED fast audio buffer size:
    //   the lower latency required
    //     *) the smaller the buffer should be (adjust it here) AND
    //     *) the less buffering should be before starting player AFTER
    //        receiving the recordered buffer
    //   Adjust the bufSize here to fit your bill [before it busts]
    uint32_t bufSize = engine.fastPathFramesPerBuf_ * engine.sampleChannels_
                       * engine.bitsPerSample_;
    bufSize = (bufSize + 7) >> 3;            // bits --> byte
    engine.bufCount_ = BUF_COUNT;
    engine.bufs_ = allocateSampleBufs(engine.bufCount_, bufSize);
    assert(engine.bufs_);

    engine.freeBufQueue_ = new AudioQueue (engine.bufCount_);
    engine.recBufQueue_  = new AudioQueue (engine.bufCount_);
    assert(engine.freeBufQueue_ && engine.recBufQueue_);
    for(int i=0; i<engine.bufCount_; i++) {
        engine.freeBufQueue_->push(&engine.bufs_[i]);
    }
}

jboolean createSLBufferQueueAudioPlayer(void)
{
    SampleFormat sampleFormat;
    memset(&sampleFormat, 0, sizeof(sampleFormat));
    sampleFormat.pcmFormat_ = (uint16_t)engine.bitsPerSample_;
    sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_;

    // SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
    sampleFormat.channels_ = (uint16_t)engine.sampleChannels_;
    sampleFormat.sampleRate_ = engine.fastPathSampleRate_;

    engine.player_ = new AudioPlayer(&sampleFormat, engine.slEngineItf_);
    assert(engine.player_);
    if(engine.player_ == nullptr)
        return JNI_FALSE;

    engine.player_->SetBufQueue(engine.recBufQueue_, engine.freeBufQueue_);
    engine.player_->RegisterCallback(EngineService, (void*)&engine);

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_NativeFastPlayer_createSLBufferQueueAudioPlayer(JNIEnv *env, jclass type) {
    return createSLBufferQueueAudioPlayer();
}

void deleteSLBufferQueueAudioPlayer(void)
{
    if(engine.player_) {
        delete engine.player_;
        engine.player_= nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_deleteSLBufferQueueAudioPlayer(JNIEnv *env, jclass type) {
    deleteSLBufferQueueAudioPlayer();
}

JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_NativeFastPlayer_createAudioRecorder(JNIEnv *env, jclass type) {
    SampleFormat sampleFormat;
    memset(&sampleFormat, 0, sizeof(sampleFormat));
    sampleFormat.pcmFormat_ = static_cast<uint16_t>(engine.bitsPerSample_);

    // SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
    sampleFormat.channels_ = engine.sampleChannels_;
    sampleFormat.sampleRate_ = engine.fastPathSampleRate_;
    sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_;
    engine.recorder_ = new AudioRecorder(&sampleFormat, engine.slEngineItf_);
    if(!engine.recorder_) {
        return JNI_FALSE;
    }
    engine.recorder_->SetBufQueues(engine.freeBufQueue_, engine.recBufQueue_);
    engine.recorder_->RegisterCallback(EngineService, (void*)&engine);
    return JNI_TRUE;
}

void deleteAudioDecoder(void)
{
    if(engine.decoder_) delete engine.decoder_;

    engine.decoder_ = nullptr;
}


JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_deleteAudioRecorder(JNIEnv *env, jclass type) {
    if(engine.recorder_)
        delete engine.recorder_;

    engine.recorder_ = nullptr;
}

jboolean createAudioDecoder(void) {

    SampleFormat sampleFormat;
    memset(&sampleFormat, 0, sizeof(sampleFormat));
    sampleFormat.pcmFormat_ = static_cast<uint16_t>(engine.bitsPerSample_);

    // SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
    sampleFormat.channels_ = engine.sampleChannels_;
    sampleFormat.sampleRate_ = engine.fastPathSampleRate_;
    sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_;
    engine.decoder_ = new AudioDecoder(&sampleFormat, engine.slEngineItf_, uriString.c_str());

    if (!engine.decoder_) {
        return JNI_FALSE;
    }

    engine.decoder_->SetBufQueues(engine.freeBufQueue_, engine.recBufQueue_);
    engine.decoder_->RegisterCallback(EngineService, (void *) &engine);

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_NativeFastPlayer_createAudioDecoder(JNIEnv* env, jclass type, jbyteArray uri) {

    //GetStringUTFChars doesn't work well on some devices - HTC M8 in particular
    // convert Java string to UTF-8
    //utf8uri = env->GetStringUTFChars(uri, JNI_FALSE);
    //assert(NULL != utf8uri);

    //http://stackoverflow.com/questions/20536296/jni-call-convert-jstring-to-char
    //http://banachowski.com/deprogramming/2012/02/working-around-jni-utf-8-strings/
    jbyte *text_input = env->GetByteArrayElements(uri, NULL);
    jsize size = env->GetArrayLength(uri);

    uriString.clear();
    uriString.append((char *)text_input, size);

    jboolean result = createAudioDecoder();

    env->ReleaseByteArrayElements(uri, text_input, NULL);

    // release the Java string and UTF-8
    //env->ReleaseStringUTFChars(uri, utf8uri);

    return result?JNI_TRUE:JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_deleteAudioDecoder(JNIEnv *env, jclass type) {
    deleteAudioDecoder();
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_startPlay(JNIEnv *env, jclass type) {

    engine.frameCount_  = 0;
    /*
     * start player: make it into waitForData state
     */
    if(SL_BOOLEAN_FALSE == engine.player_->Start()){
        LOGE("====%s failed", __FUNCTION__);
        return;
    }
    if(engine.recorder_) engine.recorder_->Start();
    if(engine.decoder_) engine.decoder_->Start();
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_stopPlay(JNIEnv *env, jclass type) {
    if(engine.recorder_) engine.recorder_->Stop();
    if(engine.decoder_) engine.decoder_->Stop();
    engine.player_ ->Stop();

    if(engine.recorder_) delete engine.recorder_;
    if(engine.decoder_) delete engine.decoder_;
    delete engine.player_;
    engine.recorder_ = NULL;
    engine.decoder_ = NULL;
    engine.player_ = NULL;
}

JNIEXPORT void JNICALL
Java_com_google_sample_echo_NativeFastPlayer_deleteSLEngine(JNIEnv *env, jclass type) {
    delete engine.recBufQueue_;
    delete engine.freeBufQueue_;
    releaseSampleBufs(engine.bufs_, engine.bufCount_);
    if (engine.slEngineObj_ != NULL) {
        (*engine.slEngineObj_)->Destroy(engine.slEngineObj_);
        engine.slEngineObj_ = NULL;
        engine.slEngineItf_ = NULL;
    }
}

uint32_t dbgEngineGetBufCount(void) {
    uint32_t count = engine.player_->dbgGetDevBufCount();
    if(engine.recorder_) count += engine.recorder_->dbgGetDevBufCount();
    if(engine.decoder_) count += engine.decoder_->dbgGetDevBufCount();
    count += engine.freeBufQueue_->size();
    count += engine.recBufQueue_->size();

    LOGE("Buf Disrtibutions: PlayerDev=%d, RecDev=%d, FreeQ=%d, "
                 "RecQ=%d",
         engine.player_->dbgGetDevBufCount(),
         engine.recorder_?engine.recorder_->dbgGetDevBufCount():engine.decoder_->dbgGetDevBufCount(),
         engine.freeBufQueue_->size(),
         engine.recBufQueue_->size());
    if(count != engine.bufCount_) {
        LOGE("====Lost Bufs among the queue(supposed = %d, found = %d)",
             BUF_COUNT, count);
    }
    return count;
}

void restartDecoder(void)
{
    dbgEngineGetBufCount();
    if(engine.decoder_)
    {
        engine.decoder_->Stop();//this also empties the devShadowQueue_
        delete engine.decoder_;
    }
    LOGI("restartRecorder, delete engine.recorder_");
    engine.decoder_ = nullptr;
    LOGI("restartRecorder, engine.recorder_ = nullptr");

    createAudioDecoder();
    //startPlay();
    engine.decoder_->SoftStart();
    dbgEngineGetBufCount();
}

/*
 * simple message passing for player/recorder to communicate with engine
 */
bool EngineService(void* ctx, uint32_t msg, void* data ) {
    assert(ctx == &engine);
    switch (msg) {
        case ENGINE_SERVICE_MSG_KICKSTART_PLAYER:
            engine.player_->PlayAudioBuffers(PLAY_KICKSTART_BUFFER_COUNT);
            // we only allow it to call once, so tell caller do not call
            // anymore
            return false;
        case ENGINE_SERVICE_MSG_RETRIEVE_DUMP_BUFS:
            *(static_cast<uint32_t*>(data)) = dbgEngineGetBufCount();
            break;

        /*Resume decoding again - typically when the buffer que is getting low on buffers*/
        case ENGINE_SERVICE_CONTINUE_DECODING:
            if(engine.decoder_) engine.decoder_->Resume();
            return true;

        /*Notify the player that the decoding finished*/
        case ENGINE_SERVICE_DECODING_FINISHED:
            if(engine.player_) engine.player_->DecodingFinished();
            return true;

        /*Recreate the decoder in order to play the file again*/
        case ENGINE_SERVICE_RESTART_DECODING:
            if(engine.decoder_) restartDecoder();
            return true;

        default:
            assert(false);
            return false;
    }

    return true;
}
