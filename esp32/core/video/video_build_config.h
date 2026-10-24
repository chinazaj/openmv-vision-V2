#pragma once

// Video core build mode.
//
// DEVELOPMENT keeps protocol/transaction diagnostics enabled. RELEASE removes
// the diagnostic counters and verbose logs from the video core.
//
// For the current investigation keep this at DEVELOPMENT. Switch this single
// value to VIDEO_BUILD_MODE_RELEASE before a production build.
#define VIDEO_BUILD_MODE_DEVELOPMENT 1
#define VIDEO_BUILD_MODE_RELEASE     0

#ifndef OPENMV_VIDEO_BUILD_MODE
#define OPENMV_VIDEO_BUILD_MODE VIDEO_BUILD_MODE_DEVELOPMENT
#endif

#if OPENMV_VIDEO_BUILD_MODE != VIDEO_BUILD_MODE_DEVELOPMENT && \
    OPENMV_VIDEO_BUILD_MODE != VIDEO_BUILD_MODE_RELEASE
#error "OPENMV_VIDEO_BUILD_MODE must be VIDEO_BUILD_MODE_DEVELOPMENT or VIDEO_BUILD_MODE_RELEASE"
#endif

#if OPENMV_VIDEO_BUILD_MODE == VIDEO_BUILD_MODE_DEVELOPMENT
#define VIDEO_CORE_DEVELOPMENT 1
#else
#define VIDEO_CORE_DEVELOPMENT 0
#endif
