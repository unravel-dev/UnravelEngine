using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Writes messages to the engine log with automatic call-site information.
    /// </summary>
    public static class Log
    {
        /// <summary>
        /// Writes a trace-level log message.
        /// </summary>
        /// <param name="message">The message to log.</param>
        /// <param name="func">Caller member name (filled automatically).</param>
        /// <param name="file">Caller file path (filled automatically).</param>
        /// <param name="line">Caller line number (filled automatically).</param>
        public static void Trace(string message,
                                    [CallerMemberName] string func = "",
                                    [CallerFilePath] string file = "",
                                    [CallerLineNumber] int line = 0)
        {
            internal_m2n_log_trace(message, func, file, line);
        }

        /// <summary>
        /// Writes an info-level log message.
        /// </summary>
        /// <param name="message">The message to log.</param>
        /// <param name="func">Caller member name (filled automatically).</param>
        /// <param name="file">Caller file path (filled automatically).</param>
        /// <param name="line">Caller line number (filled automatically).</param>
        public static void Info(string message,
                                   [CallerMemberName] string func = "",
                                   [CallerFilePath] string file = "",
                                   [CallerLineNumber] int line = 0)
        {
            internal_m2n_log_info(message, func, file, line);
        }

        /// <summary>
        /// Writes a warning-level log message.
        /// </summary>
        /// <param name="message">The message to log.</param>
        /// <param name="func">Caller member name (filled automatically).</param>
        /// <param name="file">Caller file path (filled automatically).</param>
        /// <param name="line">Caller line number (filled automatically).</param>
        public static void Warning(string message,
                                      [CallerMemberName] string func = "",
                                      [CallerFilePath] string file = "",
                                      [CallerLineNumber] int line = 0)
        {
            internal_m2n_log_warning(message, func, file, line);
        }

        /// <summary>
        /// Writes an error-level log message.
        /// </summary>
        /// <param name="message">The message to log.</param>
        /// <param name="func">Caller member name (filled automatically).</param>
        /// <param name="file">Caller file path (filled automatically).</param>
        /// <param name="line">Caller line number (filled automatically).</param>
        public static void Error(string message,
                                    [CallerMemberName] string func = "",
                                    [CallerFilePath] string file = "",
                                    [CallerLineNumber] int line = 0)
        {
            internal_m2n_log_error(message, func, file, line);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_log_trace(string message, string func, string file, int line);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_log_info(string message, string func, string file, int line);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_log_warning(string message, string func, string file, int line);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_log_error(string message, string func, string file, int line);
    }
}
