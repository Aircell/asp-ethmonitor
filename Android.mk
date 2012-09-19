ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
    ethmonitor.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    external/ethmonitor \
	external/ethtool

LOCAL_LDLIBS += -lpthread

LOCAL_MODULE := ethmonitor
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_SHARED_LIBRARIES := libnetutils

include $(BUILD_EXECUTABLE)

endif  # TARGET_SIMULATOR != true
