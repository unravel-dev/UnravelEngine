using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// How the game's window covers its display (<see cref="Screen.fullScreenMode"/>).
    /// </summary>
    public enum FullScreenMode
    {
        /// <summary>
        /// A window with a frame, at the size <see cref="Screen.SetResolution"/> gave it. The player can move and
        /// resize it.
        /// </summary>
        Windowed = 0,

        /// <summary>
        /// A window without a frame over the whole display, at the desktop's resolution. Switching to and from it is
        /// quick, and other windows can show over it.
        /// </summary>
        FullScreenWindow = 1,

        /// <summary>
        /// The display switches to the resolution and refresh rate <see cref="Screen.SetResolution"/> chose, and the
        /// game covers it.
        /// </summary>
        ExclusiveFullScreen = 2,
    }

    /// <summary>
    /// A display resolution: a size in pixels and a refresh rate.
    /// </summary>
    public struct Resolution
    {
        /// <summary>Width in pixels.</summary>
        public int width;

        /// <summary>Height in pixels.</summary>
        public int height;

        /// <summary>Refresh rate in Hz, rounded; 0 where the display reports none.</summary>
        public int refreshRate;

        /// <summary>The resolution as "1920 x 1080 @ 60Hz".</summary>
        public override string ToString()
        {
            return $"{width} x {height} @ {refreshRate}Hz";
        }
    }

    /// <summary>
    /// Size of the view the game is shown in: the window in the player, the Game panel's
    /// resolution in the editor. Same pixel space as <see cref="Input.mousePosition"/>.
    /// </summary>
    /// <remarks>
    /// Set before the scripts update, to the size the frame renders at; in the player also before
    /// play begins. In the editor a free-aspect resolution is known once the Game panel has been
    /// shown (zero before), and the size keeps its last value while the panel is hidden. A single
    /// camera's size is <see cref="CameraComponent.viewportSize"/>.
    ///
    /// The display options (<see cref="fullScreenMode"/>, <see cref="SetResolution"/>, <see cref="displayIndex"/>,
    /// <see cref="vSync"/>, <see cref="Application.targetFrameRate"/>) change the player's window and frame pacing
    /// and are saved in the user's folder, so the game starts the way the player left it; a window the player
    /// resizes or moves keeps its size and display too. A first start is a
    /// <see cref="FullScreenMode.FullScreenWindow"/> on the main display with vsync on and no frame cap. The command
    /// line changes them for one run: --window-mode windowed|borderless|exclusive, --resolution 1280x720 or
    /// 1920x1080@144, --display 1, --novsync, --max-fps 60. In the editor the Game panel is part of the editor's
    /// window: the options read <see cref="FullScreenMode.Windowed"/> and the editor's vsync and Max FPS, and
    /// changing them does nothing.
    /// </remarks>
    [AutoStaticsCleanup]
    public static class Screen
    {
        private struct ResizedHandler
        {
            public Action handler;
            public ScriptComponent script;
        }

        private static readonly List<ResizedHandler> resizedHandlers = new List<ResizedHandler>();
        private static Vector2 lastSize;
        private static bool hasLastSize;

        /// <summary>
        /// Width of the game view in pixels.
        /// </summary>
        public static int width
        {
            get => (int)internal_m2n_screen_get_size().x;
        }

        /// <summary>
        /// Height of the game view in pixels.
        /// </summary>
        public static int height
        {
            get => (int)internal_m2n_screen_get_size().y;
        }

        /// <summary>
        /// Width divided by height of the game view, or 0 while the size is unknown.
        /// </summary>
        public static float aspect
        {
            get
            {
                Vector2 size = internal_m2n_screen_get_size();
                return size.y > 0.0f ? size.x / size.y : 0.0f;
            }
        }

        /// <summary>
        /// Pixels per inch of the display showing the game, as the system reports it: 96 at 100 % display scaling
        /// on desktops (144 at 150 %), the screen's density on phones (Android's density dpi, 450 on many). In the
        /// editor a phone or a tablet resolution preset reports that device's density in the Game view's pixels, so
        /// <c>width / dpi</c> is the screen's size in inches everywhere.
        /// </summary>
        public static float dpi
        {
            get => internal_m2n_screen_get_dpi();
        }

        /// <summary>
        /// The part of the game view that display cutouts, rounded corners and system bars leave
        /// clear, in the same pixels as <see cref="width"/> and <see cref="height"/>, from the top-left
        /// corner. Keep buttons and text inside it on phones. The whole view where nothing covers it,
        /// as on desktops and in the editor.
        /// </summary>
        public static Rect safeArea
        {
            get => internal_m2n_screen_get_safe_area();
        }

        /// <summary>
        /// How the game's window covers its display. Setting it keeps the size and refresh rate
        /// <see cref="SetResolution"/> chose, and saves the choice.
        /// </summary>
        public static FullScreenMode fullScreenMode
        {
            get => (FullScreenMode)internal_m2n_screen_get_full_screen_mode();
            set
            {
                RequireMode(value, nameof(value));
                internal_m2n_screen_set_full_screen_mode((int)value);
            }
        }

        /// <summary>
        /// Whether the game covers its display. Setting it to true keeps
        /// <see cref="FullScreenMode.ExclusiveFullScreen"/> and otherwise picks
        /// <see cref="FullScreenMode.FullScreenWindow"/>; false picks <see cref="FullScreenMode.Windowed"/>.
        /// </summary>
        public static bool fullScreen
        {
            get => fullScreenMode != FullScreenMode.Windowed;
            set
            {
                if (value != fullScreen)
                {
                    fullScreenMode = value ? FullScreenMode.FullScreenWindow : FullScreenMode.Windowed;
                }
            }
        }

        /// <summary>
        /// The resolution of the display the game is on: the one the game switched it to in
        /// <see cref="FullScreenMode.ExclusiveFullScreen"/>, otherwise the desktop's.
        /// </summary>
        public static Resolution currentResolution
        {
            get => internal_m2n_screen_get_current_resolution();
        }

        /// <summary>
        /// The resolutions the display the game is on offers for <see cref="FullScreenMode.ExclusiveFullScreen"/>,
        /// each once, from the smallest and the lowest refresh rate.
        /// </summary>
        public static Resolution[] resolutions
        {
            get => GetResolutions(displayIndex);
        }

        /// <summary>
        /// The resolutions a display offers for <see cref="FullScreenMode.ExclusiveFullScreen"/>, each once, from the
        /// smallest and the lowest refresh rate; empty for a display that is not there.
        /// </summary>
        /// <param name="display">The display, from 0 to <see cref="displayCount"/> - 1.</param>
        public static Resolution[] GetResolutions(int display)
        {
            return internal_m2n_screen_get_resolutions(display).ToStructArray<Resolution>();
        }

        /// <summary>
        /// Changes the game's window and saves the choice. <see cref="FullScreenMode.Windowed"/> gives the window
        /// the size, at most the display's free area. <see cref="FullScreenMode.ExclusiveFullScreen"/> switches the
        /// display to its resolution and refresh rate closest to the ones asked for (see <see cref="resolutions"/>).
        /// <see cref="FullScreenMode.FullScreenWindow"/> covers the display at the desktop's resolution and keeps the
        /// size for a later switch. <see cref="width"/> and <see cref="height"/> follow at the next frame, with
        /// <see cref="resized"/>.
        /// </summary>
        /// <param name="width">Width in pixels. A width or a height of 0 picks the default: the desktop's
        /// resolution, or 1280 x 720 for a window.</param>
        /// <param name="height">Height in pixels.</param>
        /// <param name="mode">How the window covers the display.</param>
        /// <param name="refreshRate">The refresh rate in Hz for <see cref="FullScreenMode.ExclusiveFullScreen"/>;
        /// 0 for the desktop's.</param>
        public static void SetResolution(int width, int height, FullScreenMode mode, int refreshRate = 0)
        {
            if (width < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(width), width, "The width cannot be negative.");
            }
            if (height < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(height), height, "The height cannot be negative.");
            }
            if (refreshRate < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(refreshRate), refreshRate, "The refresh rate cannot be negative.");
            }
            RequireMode(mode, nameof(mode));
            internal_m2n_screen_set_resolution(width, height, (int)mode, refreshRate);
        }

        /// <summary>
        /// The number of displays connected.
        /// </summary>
        public static int displayCount
        {
            get => internal_m2n_screen_get_display_count();
        }

        /// <summary>
        /// The display the game is on, from 0 to <see cref="displayCount"/> - 1. Setting it moves the game there in
        /// its <see cref="fullScreenMode"/> and saves the choice.
        /// </summary>
        public static int displayIndex
        {
            get => internal_m2n_screen_get_display_index();
            set
            {
                if (value < 0 || value >= displayCount)
                {
                    throw new ArgumentOutOfRangeException(nameof(value), value, $"There are {displayCount} displays.");
                }
                internal_m2n_screen_set_display_index(value);
            }
        }

        /// <summary>
        /// A display's name as the system shows it, often the monitor's model; empty when unknown or for a display
        /// that is not there.
        /// </summary>
        /// <param name="display">The display, from 0 to <see cref="displayCount"/> - 1.</param>
        public static string GetDisplayName(int display)
        {
            return internal_m2n_screen_get_display_name(display);
        }

        /// <summary>
        /// Whether a frame waits for the display's refresh: no tearing, and no more frames than the display shows.
        /// Setting it saves the choice.
        /// </summary>
        public static bool vSync
        {
            get => internal_m2n_screen_get_vsync();
            set => internal_m2n_screen_set_vsync(value);
        }

        private static void RequireMode(FullScreenMode mode, string parameterName)
        {
            if (!Enum.IsDefined(typeof(FullScreenMode), mode))
            {
                throw new ArgumentOutOfRangeException(parameterName, mode, "Not a FullScreenMode.");
            }
        }

        /// <summary>
        /// Raised at the start of a frame, before the scripts update, when <see cref="width"/> or
        /// <see cref="height"/> changed since the previous frame. Ends with the subscribing script.
        /// </summary>
        public static event Action resized
        {
            add
            {
                if (value != null)
                {
                    resizedHandlers.Add(new ResizedHandler
                    {
                        handler = value,
                        script = value.Target as ScriptComponent ?? ScriptComponent.running
                    });
                }
            }
            remove
            {
                int index = resizedHandlers.FindIndex(h => h.handler.Equals(value));
                if (index >= 0)
                {
                    resizedHandlers.RemoveAt(index);
                }
            }
        }

        /// <summary>
        /// Raises <see cref="resized"/> when the size changed; the first call only records it.
        /// </summary>
        internal static void Poll()
        {
            Vector2 size = internal_m2n_screen_get_size();
            bool changed = hasLastSize && size != lastSize;
            lastSize = size;
            hasLastSize = true;
            if (!changed || resizedHandlers.Count == 0)
            {
                return;
            }
            foreach (ResizedHandler entry in resizedHandlers.ToArray())
            {
                using (ScriptComponent.Run(entry.script))
                {
                    try
                    {
                        entry.handler();
                    }
                    catch (Exception ex)
                    {
                        Log.Error($"Screen.resized handler: {ex}");
                    }
                }
            }
        }

        /// <summary>
        /// Drops the handlers @p script added; called when it is destroyed.
        /// </summary>
        internal static void ForgetScript(ScriptComponent script)
        {
            resizedHandlers.RemoveAll(h => ReferenceEquals(h.script, script));
        }

        private static void OnStaticsCleanup()
        {
            resizedHandlers.Clear();
            hasLastSize = false;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_screen_get_size();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_screen_get_dpi();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Rect internal_m2n_screen_get_safe_area();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_screen_get_full_screen_mode();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_screen_set_full_screen_mode(int mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_screen_set_resolution(int width, int height, int mode, int refreshRate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Resolution internal_m2n_screen_get_current_resolution();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_screen_get_resolutions(int display);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_screen_get_display_count();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_screen_get_display_name(int display);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_screen_get_display_index();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_screen_set_display_index(int display);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_screen_get_vsync();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_screen_set_vsync(bool vsync);
    }

}
