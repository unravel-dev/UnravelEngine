using System;
using System.Runtime.InteropServices;

namespace Unravel.Web
{
    /// <summary>
    /// The browser player's managed entry. The page boots the .NET runtime and runs Main once the game's files are in
    /// place: Main hands the engine's script bridge (clrpp, runtime_host::browser) the Bootstrap it binds to, where a
    /// desktop player starts a runtime of its own, then starts the engine with the page's arguments. The engine runs
    /// its frames from the browser's animation frames after Main returns.
    /// </summary>
    public static unsafe class Program
    {
        /// <summary>The program the engine is started as: the application's folder is the file system root.</summary>
        private const string ProgramPath = "/app";

        [DllImport("libclrpp")]
        private static extern void clrpp_set_browser_bootstrap(IntPtr bootstrap);

        [DllImport("libospp")]
        private static extern int os_main_from_host(int argc, IntPtr* argv);

        public static int Main(string[] args)
        {
            delegate* unmanaged<IntPtr*, int, int> bootstrap = &Clrpp.Bridge.Bootstrap;
            clrpp_set_browser_bootstrap((IntPtr)bootstrap);
            return StartEngine(args);
        }

        /// <summary>
        /// Starts the engine as a C program is started: UTF-8 arguments after the program, ending in null. The engine
        /// copies them while it starts, so they are freed once it returns.
        /// </summary>
        private static int StartEngine(string[] args)
        {
            int argc = args.Length + 1;
            IntPtr* argv = stackalloc IntPtr[argc + 1];
            argv[0] = Marshal.StringToCoTaskMemUTF8(ProgramPath);
            for (int i = 0; i < args.Length; i++)
            {
                argv[i + 1] = Marshal.StringToCoTaskMemUTF8(args[i]);
            }
            argv[argc] = IntPtr.Zero;
            try
            {
                return os_main_from_host(argc, argv);
            }
            finally
            {
                for (int i = 0; i < argc; i++)
                {
                    Marshal.FreeCoTaskMem(argv[i]);
                }
            }
        }
    }
}
