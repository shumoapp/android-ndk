# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := audio_main
LOCAL_SRC_FILES := audio_main.cpp \
audio_player.cpp \
audio_recorder.cpp \
audio_common.cpp

LOCAL_EXPORT_C_INCLUDES := ../app/jni
LOCAL_C_INCLUDES += ../app/jni

# for native audio
LOCAL_LDLIBS    += -lOpenSLES
# for logging
LOCAL_LDLIBS    += -llog
# for native asset manager
LOCAL_LDLIBS    += -landroid


LOCAL_CFLAGS :=-D__GXX_EXPERIMENTAL_CXX0X__
LOCAL_CPPFLAGS  := -std=c++11

include $(BUILD_SHARED_LIBRARY)
