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
        /// The most frames per second the game runs at; 0 when there is no cap, as on a first start.
        /// Setting 0 or less removes the cap. With <see cref="Screen.vSync"/> on, the display's refresh
        /// rate limits the frames too, so a cap above it changes nothing. Unless
        /// <see cref="runInBackground"/> is true, a minimized game runs at most 20 frames per second
        /// whatever the cap.
        /// </summary>
        /// <remarks>
        /// In the player setting it saves the choice with the display options (see <see cref="Screen"/>),
        /// and --max-fps 60 (0 for no cap) changes it for one run. In the editor it reads the editor's
        /// Max FPS, and setting it does nothing.
        /// </remarks>
        public static int targetFrameRate
        {
            get => internal_m2n_application_get_target_frame_rate();
            set => internal_m2n_application_set_target_frame_rate(value);
        }

        /// <summary>
        /// Whether the game keeps its frame rate while its window is minimized. When it is false, a
        /// minimized game runs at most 20 frames per second: it keeps going, but updates less often
        /// and spends little power on frames nobody sees. It starts as the project settings'
        /// Standalone > Run In Background.
        /// </summary>
        /// <remarks>
        /// In the player, setting it changes it for this run only; a game that offers it as an option
        /// saves the choice itself and sets it at start. In the editor it is always true, because the
        /// editor keeps its frame rate while minimized, and setting it does nothing.
        /// </remarks>
        public static bool runInBackground
        {
            get => internal_m2n_application_get_run_in_background();
            set => internal_m2n_application_set_run_in_background(value);
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_application_get_target_frame_rate();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_application_set_target_frame_rate(int frameRate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_application_get_run_in_background();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_application_set_run_in_background(bool runInBackground);
    }

}
