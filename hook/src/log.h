#pragma once

// Plain-text log next to the game executable: and.log. Thread-safe, flushed per line.
void LogOpen();
void Log(const char* fmt, ...);
