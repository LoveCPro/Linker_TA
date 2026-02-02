#pragma once
#include <QDebug>

// ===== 日志开关 =====
#define LOG_SERIAL   1
#define LOG_FRAME    1
#define LOG_ERROR    1

// ===== 日志宏 =====
#if LOG_SERIAL
#define LOG_SERIAL_D(...) qDebug().noquote() << __VA_ARGS__
#else
#define LOG_SERIAL_D(...)
#endif

#if LOG_FRAME
#define LOG_FRAME_D(...) qDebug().noquote() << __VA_ARGS__
#else
#define LOG_FRAME_D(...)
#endif

#if LOG_ERROR
#define LOG_E(...) qWarning().noquote() << __VA_ARGS__
#else
#define LOG_E(...)
#endif
