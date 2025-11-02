using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Provides application-level functionality and control.
    /// </summary>
    public static class Application
    {
        /// <summary>
        /// Quits the application by stopping playmode.
        /// This will exit the current play session and return to edit mode.
        /// </summary>
        public static void Quit()
        {
            internal_m2n_application_quit();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_application_quit();
    }

}
