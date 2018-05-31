LOCAL_PATH := $(call my-dir)

###########################
#
# FFmpeg shared library
#
###########################
# FFmpeg library
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg
LOCAL_SRC_FILES := libffmpeg.so
include $(PREBUILT_SHARED_LIBRARY)

###########################
#
# novc shared library
#
###########################
# novc library
include $(CLEAR_VARS)
LOCAL_MODULE := nvoc
LOCAL_SRC_FILES := libnvoc.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := native_audio
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_SRC_FILES := native_audio.c opensl_io.c
LOCAL_SHARED_LIBRARIES := ffmpeg nvoc
LOCAL_LDLIBS := -llog -lOpenSLES
include $(BUILD_SHARED_LIBRARY)

#include $(CLEAR_VARS)
#LOCAL_MODULE := native_nvoc
#LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
#LOCAL_SRC_FILES := native_nvoc.c
#LOCAL_SHARED_LIBRARIES := nvoc
#LOCAL_LDLIBS := -llog
#include $(BUILD_SHARED_LIBRARY)