#include <cassert>
#include <cstdint>
#include <string>

#include "lab_logic.h"

int main() {
    PomodoroTimer timer(1000);
    const auto work = timer.advance(1000);
    assert(work.working);
    assert(work.remaining_seconds == 1500);

    const auto break_phase = timer.advance(1000 + 1500U * 1000U);
    assert(!break_phase.working);
    assert(break_phase.remaining_seconds == 300);

    PomodoroTimer rollover_timer(0);
    rollover_timer.advance(UINT32_MAX - 999U);
    const auto wrapped = rollover_timer.advance(0);
    assert(wrapped.working);
    assert(wrapped.remaining_seconds == 1333);

    assert(encodeMorse("SOS") == "... --- ...");
    assert(encodeMorse("HELLO WORLD").find("/") != std::string::npos);
    assert(std::string(morsePattern('!')).empty());

    assert(aliasCount() >= 6);
    assert(aliasAt(0) != nullptr);
    assert(aliasAt(aliasCount()) == nullptr);

    assert(boundedChoice(0, 6) == 0);
    assert(boundedChoice(13, 6) == 1);
    assert(boundedChoice(99, 0) == 0);

    assert(isSafeFilename("notes.txt"));
    assert(isSafeFilename("photo-01.bin"));
    assert(!isSafeFilename("../secrets.txt"));
    assert(!isSafeFilename("folder/file.txt"));
    assert(!isSafeFilename(".hidden"));
    assert(!isSafeFilename(std::string(49, 'a')));

    assert(boundedText("hello", 3) == "hel");
    assert(boundedText("hello", 10) == "hello");
    assert(boundedText("hello", 0).empty());

    assert(shouldFormatFilesystem(true, false, false));
    assert(!shouldFormatFilesystem(false, false, false));
    assert(!shouldFormatFilesystem(true, true, false));
    assert(!shouldFormatFilesystem(true, false, true));

    assert(filesystemInitializationIsDurable(true, true, false));
    assert(filesystemInitializationIsDurable(true, false, true));
    assert(!filesystemInitializationIsDurable(true, false, false));
    assert(!filesystemInitializationIsDurable(false, false, true));
    return 0;
}
