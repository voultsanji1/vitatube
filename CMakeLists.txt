cmake_minimum_required(VERSION 3.5...3.27)
if(CMAKE_VERSION VERSION_LESS 3.5)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
project(vitatube C CXX)

set(VITA_APP_NAME "VitaTube")
set(VITA_TITLEID "VITATUBE")
set(VITA_VERSION "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -std=c11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

include_directories(
  source
)

add_executable(${PROJECT_NAME}
  source/main.c
  source/http.c
  source/log.c
  source/storage.c
  source/extractor.c
  source/youtube.c
  source/stream_select.c
  source/chapters.c
  source/subtitles.c
  source/sponsorblock.c
  source/ui.c
  source/player.c
  source/app.c
)

target_link_libraries(${PROJECT_NAME}
  vita2d_vgl
  vitagl
  c
  m
  stdc++
  pthread
  vitashark
  SceLibKernel_stub
  SceCtrl_stub
  SceGxm_stub
  SceDisplay_stub
  SceShaccCg_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  SceAvPlayer_stub
  SceSysmodule_stub
  ScePower_stub
  SceAudio_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  SceTouch_stub
  SceMotion_stub
  SceIofilemgr_stub
  SceProcessmgr_stub
  ScePgf_stub
  SceFont_stub
  SceImeDialog_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

execute_process(
  COMMAND ${VITASDK}/bin/vita-mksfoex -s TITLE_ID=${VITA_TITLEID} -d VERSION=${VITA_VERSION} "${VITA_APP_NAME}" sce_sys/param.sfo
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/param.sfo sce_sys/param.sfo
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
