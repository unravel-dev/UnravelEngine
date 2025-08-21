/**
 * @file crash_handlers.hpp
 * @brief Modern C++20 cross-platform crash handler using cpptrace
 * 
 * This crash handler provides comprehensive crash detection and logging capabilities
 * using the cpptrace library for robust, cross-platform stack trace generation.
 * It intercepts all crash possibilities and logs detailed stack traces.
 * 
 * Features:
 * - Cross-platform crash handling using cpptrace
 * - Signal-safe stack trace capture
 * - Comprehensive crash logging with detailed context
 * - Minimal OS-specific code
 * - Thread-safe crash detection
 * - Automatic log flushing on crash
 * 
 * @author UnravelEngine Team
 * @version 3.0
 */

#pragma once

namespace unravel::crash {

/**
 * @brief Install comprehensive crash handlers
 * 
 * Sets up all crash detection mechanisms for the current platform.
 * This function is thread-safe and can be called multiple times.
 */
auto install_handlers() -> void;

} // namespace unravel::crash