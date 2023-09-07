LOCAL_PATH:= $(call my-dir)

AML_MP_MEDIAPLAYER_DEMO_INC :=

AML_MP_MEDIAPLAYER_DEMO_SRCS := \
	AmlMpMediaPlayerDemo.cpp \

AML_MP_MEDIAPLAYER_DEMO_CFLAGS := -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION) \
	 -Wsign-compare

AML_MP_MEDIAPLAYER_DEMO_VENDOR_SHARED_LIBS := \
	libaml_mp_sdk.vendor \
	libutils \
	libcutils \
	liblog \
	libnativewindow \
	libui

AML_MP_MEDIAPLAYER_DEMO_VENDOR_STATIC_LIBS := \
	libamlMpTestSupporter.vendor

###############################################################################
include $(CLEAR_VARS)
LOCAL_MODULE := amlMpMediaPlayerDemo.vendor
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-MIT legacy_by_exception_only legacy_notice
LOCAL_LICENSE_CONDITIONS := by_exception_only notice restricted
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/../../LICENSE
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(AML_MP_MEDIAPLAYER_DEMO_SRCS)
LOCAL_CFLAGS := $(AML_MP_MEDIAPLAYER_DEMO_CFLAGS)
LOCAL_CPPFLAGS += -fexceptions
LOCAL_C_INCLUDES := $(AML_MP_MEDIAPLAYER_DEMO_INC)
LOCAL_SHARED_LIBRARIES := $(AML_MP_MEDIAPLAYER_DEMO_VENDOR_SHARED_LIBS)
LOCAL_STATIC_LIBRARIES := $(AML_MP_MEDIAPLAYER_DEMO_VENDOR_STATIC_LIBS)
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
