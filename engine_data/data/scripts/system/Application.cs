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
        /// The folder for the game's saves and settings, which it keeps between runs and updates.
        /// On desktop it is named after the project settings' Company and Product (the project name
        /// when Product is empty), in the user's application data. On Android it is a folder of the
        /// game's internal storage, the only one its backup keeps. Play mode in the editor uses the
        /// same folder as the deployed game. The folder exists; the path has forward slashes and no
        /// trailing separator.
        /// </summary>
        public static string persistentDataPath
        {
            get => internal_m2n_application_get_persistent_data_path();
        }

        /// <summary>
        /// The folder for files the game can rebuild, which the user or the system may delete at
        /// any time. On Android it is the game's cache folder; on desktop a "cache" folder inside
        /// <see cref="persistentDataPath"/>. The folder exists; the path has forward slashes and no
        /// trailing separator.
        /// </summary>
        public static string temporaryCachePath
        {
            get => internal_m2n_application_get_temporary_cache_path();
        }

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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_application_get_persistent_data_path();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_application_get_temporary_cache_path();
    }

}
