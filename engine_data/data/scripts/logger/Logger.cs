using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
public static class Log
{
    public static void Trace(string message,
                                [CallerMemberName] string func = "",
                                [CallerFilePath] string file = "",
                                [CallerLineNumber] int line = 0)
    {
        internal_m2n_log_trace(message, func, file, line);
    }
    public static void Info(string message,
                               [CallerMemberName] string func = "",
                               [CallerFilePath] string file = "",
                               [CallerLineNumber] int line = 0)
    {
        internal_m2n_log_info(message, func, file, line);
    }
    public static void Warning(string message,
                                  [CallerMemberName] string func = "",
                                  [CallerFilePath] string file = "",
                                  [CallerLineNumber] int line = 0)
    {
        internal_m2n_log_warning(message, func, file, line);
    }
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

}


