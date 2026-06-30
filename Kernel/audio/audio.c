#include "audio.h"
#include "hda.h"
#include "debug.h"
#include "error.h"

void AudioInit(void) {
    if (HdaInitialized()) {
        return;
    }
    KdPrintf("[AUDIO] Initializing audio subsystem...\n");
    HdaInit();
    if (HdaInitialized()) {
        KdPrintf("[AUDIO] HD Audio ready\n");
    } else {
        KdPrintf("[AUDIO] HD Audio not available\n");
    }
}

i32 AudioInitialized(void) {
    return HdaInitialized();
}

ntstatus AudioPlay(const void* buffer, u32 size) {
    if (!HdaInitialized()) return STATUS_UNSUCCESSFUL;
    return HdaPlayBuffer(buffer, size);
}

void AudioStop(void) {
    HdaStop();
}
