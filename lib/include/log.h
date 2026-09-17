#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

// Log levels — set LOG_LEVEL in platformio.ini build_flags.
// 0 = OFF, 1 = ERROR, 2 = WARN, 3 = INFO, 4 = DEBUG
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#define LOG_ERROR(msg) Serial.println(F(msg))
#define LOG_WARN(msg)  Serial.println(F(msg))
#define LOG_INFO(msg)  Serial.println(F(msg))
#define LOG_DEBUG(msg) Serial.println(F(msg))

// LOG_INFOF / LOG_DEBUGF for printf-style calls with runtime values
#define LOG_ERRORF(...) Serial.printf(__VA_ARGS__)
#define LOG_WARNF(...)  Serial.printf(__VA_ARGS__)
#define LOG_INFOF(...)  Serial.printf(__VA_ARGS__)
#define LOG_DEBUGF(...) Serial.printf(__VA_ARGS__)

// LOG_INFO_RAW for calls that don't fit the F() macro (e.g. passing an IPAddress or int)
#define LOG_INFO_RAW(val)   Serial.println(val)
#define LOG_DEBUG_RAW(val)  Serial.println(val)
#define LOG_WARN_RAW(val)   Serial.println(val)
#define LOG_INFO_PRINT(val)  Serial.print(val)
#define LOG_DEBUG_PRINT(val) Serial.print(val)
#define LOG_WARN_PRINT(val)  Serial.print(val)

#if LOG_LEVEL < 4
#undef LOG_DEBUG
#define LOG_DEBUG(msg)      do {} while (0)
#undef LOG_DEBUGF
#define LOG_DEBUGF(...)     do {} while (0)
#undef LOG_DEBUG_RAW
#define LOG_DEBUG_RAW(val)  do {} while (0)
#undef LOG_DEBUG_PRINT
#define LOG_DEBUG_PRINT(val) do {} while (0)
#endif

#if LOG_LEVEL < 3
#undef LOG_INFO
#define LOG_INFO(msg)       do {} while (0)
#undef LOG_INFOF
#define LOG_INFOF(...)      do {} while (0)
#undef LOG_INFO_RAW
#define LOG_INFO_RAW(val)   do {} while (0)
#undef LOG_INFO_PRINT
#define LOG_INFO_PRINT(val) do {} while (0)
#endif

#if LOG_LEVEL < 2
#undef LOG_WARN
#define LOG_WARN(msg)        do {} while (0)
#undef LOG_WARNF
#define LOG_WARNF(...)       do {} while (0)
#undef LOG_WARN_RAW
#define LOG_WARN_RAW(val)    do {} while (0)
#undef LOG_WARN_PRINT
#define LOG_WARN_PRINT(val)  do {} while (0)
#endif

#if LOG_LEVEL < 1
#undef LOG_ERROR
#define LOG_ERROR(msg)  do {} while (0)
#undef LOG_ERRORF
#define LOG_ERRORF(...) do {} while (0)
#endif

#endif
