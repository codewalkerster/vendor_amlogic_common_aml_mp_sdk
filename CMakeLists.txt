cmake_minimum_required (VERSION 2.8)

project(aml_mp_sdk)

#Version
execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}; git rev-parse --verify --abbrev-ref HEAD"
    OUTPUT_VARIABLE BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}; git rev-parse --verify @{0}"
    OUTPUT_VARIABLE COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}; git log -1 --pretty=format:%cd"
    OUTPUT_VARIABLE COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "TZ='CST-8' date +'%a %b %d %H:%M:%S %Y %z'"
    OUTPUT_VARIABLE BUILD_TIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}; id -un"
    OUTPUT_VARIABLE BUILD_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}; git log -10 | grep 'Change-Id:' -m 1 | cut -d':' -f 2 | tr -d ' '"
    OUTPUT_VARIABLE CHANGE_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR};  git diff --numstat | wc -l"
    OUTPUT_VARIABLE MODIFIED_FILES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND bash -c "cd ${PROJECT_SOURCE_DIR};  git diff --cached --numstat | wc -l"
    OUTPUT_VARIABLE STAGED_FILES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

math(EXPR WORKDIR_STATUS "${MODIFIED_FILES} + ${STAGED_FILES}")
#Version

OPTION(HAVE_CTC "CTC player" OFF)
OPTION(HAVE_SUBTITLE "subtitle" ON)
OPTION(HAVE_VMXIPTV "verimatrix iptv" ON)
OPTION(HAVE_VMXWEB "verimatrix web" ON)
OPTION(HAVE_NAGRAWEB "nagra web" ON)
OPTION(HAVE_WVCAS "widevine cas" OFF)
OPTION(HAVE_CAS_HAL "CAS HAL" ON)


SET(AML_MP_PLAYER_SRC
    player/Aml_MP.cpp
    player/Aml_MP_Player.cpp
    player/Aml_MP_PlayerImpl.cpp
    player/AmlPlayerBase.cpp
    player/AmlTsPlayer.cpp
    player/AmlDummyTsPlayer.cpp
)

SET(AML_MP_CAS_SRC
    cas/Aml_MP_CAS.cpp
    cas/AmlCasBase.cpp
    cas/AmlDvbCasHal.cpp
    cas/AmCasLibWrapper.cpp
)

SET(AML_MP_DVR_SRC
    dvr/Aml_MP_DVR.cpp
    dvr/AmlDVRPlayer.cpp
    dvr/AmlDVRRecorder.cpp
)

SET(AML_MP_UTILS_SRC
    utils/AmlMpAtomizer.cpp
    utils/AmlMpBitReader.cpp
    utils/AmlMpBuffer.cpp
    utils/AmlMpConfig.cpp
    utils/AmlMpEventHandler.cpp
    utils/AmlMpEventLooper.cpp
    utils/AmlMpEventLooperRoster.cpp
    utils/AmlMpLooper.cpp
    utils/AmlMpMessage.cpp
    utils/AmlMpRefBase.cpp
    utils/AmlMpStrongPointer.cpp
    utils/AmlMpThread.cpp
    utils/AmlMpUtils.cpp
    utils/AmlMpChunkFifo.cpp
    utils/Amlsysfsutils.cpp
    utils/AmlMpPlayerRoster.cpp
    utils/json/lib_json/json_reader.cpp
    utils/json/lib_json/json_value.cpp
    utils/json/lib_json/json_writer.cpp
    utils/AmlMpCodecCapability.cpp
    utils/AmlMpSignalHandler.cpp
)

SET(AML_MP_DEMUX_SRC
    demux/AmlDemuxBase.cpp
    demux/AmlHwDemux.cpp
    demux/AmlSwDemux.cpp
    demux/AmlTsParser.cpp
    demux/AmlESQueue.cpp
)

#OPTION SETTING START
SET(AML_MP_DEFINITIONS JSON_USE_EXCEPTION=0 JSONCPP_NO_LOCALE_SUPPORT)

IF(HAVE_VMXIPTV)
    LIST(APPEND AML_MP_CAS_SRC cas/vmx_iptvcas/AmlVMXIptvCas_V2.cpp)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_VMXIPTV_CAS_V2)
ENDIF()

IF(HAVE_VMXWEB)
    LIST(APPEND AML_MP_CAS_SRC cas/vmx_webcas/AmlVMXWebCas.cpp)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_VMXWEB_CAS)
ENDIF()

IF(HAVE_NAGRAWEB)
    LIST(APPEND AML_MP_CAS_SRC cas/nagra_webcas/AmlNagraWebCas.cpp)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_NAGRAWEB_CAS)
ENDIF()

IF(HAVE_WVCAS)
    LIST(APPEND AML_MP_CAS_SRC cas/wv_iptvcas/AmlWVIptvCas_V2.cpp)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_WVIPTV_CAS_V2)
ENDIF()

IF(HAVE_SUBTITLE)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_SUBTITLE)
    LIST(APPEND subtitle_LINK_LIBRARIES subtitleclient)
ENDIF()

IF(HAVE_CTC)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_CTC)
ENDIF()

IF(HAVE_CAS_HAL)
    LIST(APPEND AML_MP_DEFINITIONS HAVE_CAS_HAL)
    FIND_PATH(libamcas_INCLUDE_DIRS am_cas.h PATH_SUFFIXES libamcas REQUIRED)
ENDIF()
#OPTION SETTING DONE

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    LIST(APPEND AML_MP_DEFINITIONS HAVE_VERSION_INFO)
    LIST(APPEND AML_MP_DEFINITIONS MODULE_BRANCH_NAME="${BRANCH}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_GIT_VERSION="${COMMIT}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_CHANGE_ID="${CHANGE_ID}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_COMMIT_DATE="${COMMIT_DATE}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_BUILD_TIME="${BUILD_TIME}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_BUILD_NAME="${BUILD_NAME}")
    LIST(APPEND AML_MP_DEFINITIONS MODULE_GIT_UNCOMMIT_FILE_NUM=${WORKDIR_STATUS})
ENDIF()

SET(AML_MP_SRCS
    ${AML_MP_PLAYER_SRC}
    ${AML_MP_DVR_SRC}
    ${AML_MP_UTILS_SRC}
    ${AML_MP_DEMUX_SRC}
    ${AML_MP_CAS_SRC}
)

SET(AML_MP_PUBLIC_HEADERS
    include/Aml_MP/Aml_MP.h
    include/Aml_MP/Cas.h
    include/Aml_MP/Common.h
    include/Aml_MP/Dvr.h
    include/Aml_MP/Aml_MediaPlayer.h
)

FIND_PATH(libdvr_INCLUDE_DIRS dvr_wrapper.h PATH_SUFFIXES libdvr REQUIRED)

SET(TARGET aml_mp_sdk)

ADD_LIBRARY(${TARGET} SHARED ${AML_MP_SRCS})

TARGET_INCLUDE_DIRECTORIES(${TARGET} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}
    ${libdvr_INCLUDE_DIRS}
    ${libamcas_INCLUDE_DIRS}
)

TARGET_COMPILE_OPTIONS(${TARGET} PUBLIC -std=c++14 -fpermissive -Werror -Wsign-compare
    -Wno-unused-result -Wno-multichar -Wno-deprecated-declarations -Wno-psabi)

TARGET_COMPILE_DEFINITIONS(${TARGET} PUBLIC __unused= )
TARGET_COMPILE_DEFINITIONS(${TARGET} PUBLIC ${AML_MP_DEFINITIONS})

TARGET_LINK_LIBRARIES(${TARGET} PUBLIC
    ${subtitle_LINK_LIBRARIES}
    mediahal_tsplayer
    amdvr
    amcas
    log
    cutils
    pthread
    dl
)

INSTALL(
    TARGETS ${TARGET}
)

INSTALL(
    FILES ${AML_MP_PUBLIC_HEADERS} DESTINATION include/Aml_MP
)

ADD_SUBDIRECTORY(tests/amlMpTestSupporter)
ADD_SUBDIRECTORY(tests/amlMpPlayerDemo)
ADD_SUBDIRECTORY(tests/amlMpMediaPlayerDemo)
ADD_SUBDIRECTORY(tests/unitTest)
ADD_SUBDIRECTORY(mediaplayer)









