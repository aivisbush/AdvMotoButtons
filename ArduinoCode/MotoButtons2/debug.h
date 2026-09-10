/* Serial logging helpers. Each call site is one line and the compiler
 * removes the whole call when DEBUG is false, because the condition is a
 * compile-time constant.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

template <typename... Args>
inline void debugPrintf(const char *format, Args... args)
{
  if (DEBUG)
    Serial.printf(format, args...);
}

inline void debugPrintln(const char *text)
{
  if (DEBUG)
    Serial.println(text);
}
