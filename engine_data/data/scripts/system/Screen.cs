using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Size of the view the game is shown in: the window in the player, the Game panel's
    /// resolution in the editor. Same pixel space as <see cref="Input.mousePosition"/>.
    /// </summary>
    /// <remarks>
    /// Set before the scripts update, to the size the frame renders at; in the player also before
    /// play begins. In the editor a free-aspect resolution is known once the Game panel has been
    /// shown (zero before), and the size keeps its last value while the panel is hidden. A single
    /// camera's size is <see cref="CameraComponent.viewportSize"/>.
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
        /// Dots per inch the system reports for the display showing the game: 96 at 100 % display
        /// scaling, 144 at 150 %. Scale UI or touch targets by <c>dpi / 96</c>.
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
    }

}
