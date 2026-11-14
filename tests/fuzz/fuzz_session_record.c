// H3 harness: one framed authority-recording record, as a replay tool would read it.
#include "fuzz/fuzz_driver.h"
#include "room/boat_session_record.h"

#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    BoatSessionRecord record;
    if (!boat_session_record_decode(data, size, &record))
        return 0;
    static BoatSessionPlayback playback;
    memset(&playback, 0, sizeof(playback));
    (void)boat_session_record_apply(&playback, &record, false);
    boat_session_record_close(&playback);
    return 0;
}

void fuzz_seeds(FuzzAdd add)
{
    // Records are checksummed, so mutations rarely pass the frame; the empty and header-sized
    // inputs still exercise the length checks. A libFuzzer corpus can use real recordings.
    static const uint8_t zeros[64];
    add(zeros, 0);
    add(zeros, 16);
    add(zeros, sizeof(zeros));
}
