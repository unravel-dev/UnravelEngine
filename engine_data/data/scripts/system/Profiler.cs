using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Performance profiler for measuring and recording execution times.
    /// </summary>
    public static class Profiler
    {
        /// <summary>
        /// Add a performance record with a custom name and time measurement.
        /// </summary>
        /// <param name="name">Name of the performance record</param>
        /// <param name="timeMs">Time in milliseconds</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_profiler_add_record(string name, float timeMs);

        /// <summary>
        /// Add a performance record with a custom name and time measurement.
        /// </summary>
        /// <param name="name">Name of the performance record</param>
        /// <param name="timeMs">Time in milliseconds</param>
        public static void AddRecord(string name, float timeMs)
        {
            internal_m2n_profiler_add_record(name, timeMs);
        }

        /// <summary>
        /// Create a scoped profiler that automatically measures and records time when disposed.
        /// Use with 'using' statement for automatic scope-based profiling.
        /// </summary>
        /// <param name="name">Name of the performance record</param>
        /// <returns>ProfilerScope that measures time until disposed</returns>
        /// <example>
        /// using (Profiler.Scope("MyFunction"))
        /// {
        ///     // Code to profile
        /// }
        /// </example>
        public static ProfilerScope Scope(string name)
        {
            return new ProfilerScope(name);
        }
    }

    /// <summary>
    /// Scoped profiler that measures execution time and automatically records it when disposed.
    /// Use with 'using' statement for automatic scope-based profiling.
    /// </summary>
    /// <example>
    /// using (var scope = new ProfilerScope("MyFunction"))
    /// {
    ///     // Code to profile
    /// }
    /// // Time is automatically recorded here
    /// 
    /// // Or use the static helper:
    /// using (Profiler.Scope("MyFunction"))
    /// {
    ///     // Code to profile
    /// }
    /// </example>
    public struct ProfilerScope : IDisposable
    {
        private readonly string _name;
        private readonly Stopwatch _stopwatch;
        private bool _disposed;

        /// <summary>
        /// Creates a new ProfilerScope and starts timing.
        /// </summary>
        /// <param name="name">Name of the performance record</param>
        public ProfilerScope(string name)
        {
            _name = name;
            _stopwatch = Stopwatch.StartNew();
            _disposed = false;
        }

        /// <summary>
        /// Stops timing and records the elapsed time to the profiler.
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
                return;

            _disposed = true;
            _stopwatch.Stop();
            
            float timeMs = (float)_stopwatch.Elapsed.TotalMilliseconds;
            Profiler.AddRecord(_name, timeMs);
        }
    }
}

