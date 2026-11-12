using System;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace Unravel.Core
{

    /// <summary>
    /// Asks the player for the permissions a system grants while the game runs. On Android those are
    /// the camera, the microphone, the location and, from Android 13, notifications. The profile's
    /// Permissions must declare every permission the game asks for: Android refuses any other
    /// without showing its dialog. On other platforms every request is granted at once, because
    /// they have no such permissions or ask by themselves when a device is first used.
    /// </summary>
    public static class Permissions
    {
        /// <summary>The cameras (the profile's Camera permission).</summary>
        public const string Camera = "android.permission.CAMERA";
        /// <summary>The microphone (the profile's Microphone permission).</summary>
        public const string Microphone = "android.permission.RECORD_AUDIO";
        /// <summary>The approximate location (the profile's Location permission).</summary>
        public const string CoarseLocation = "android.permission.ACCESS_COARSE_LOCATION";
        /// <summary>
        /// The precise location (the profile's Location permission). Android grants it only to a
        /// game that also holds <see cref="CoarseLocation"/>, so ask for that one first.
        /// </summary>
        public const string FineLocation = "android.permission.ACCESS_FINE_LOCATION";
        /// <summary>Notifications (the profile's Notifications permission).</summary>
        public const string Notifications = "android.permission.POST_NOTIFICATIONS";

        // The states of a request, as the engine numbers them.
        private const int STATE_PENDING = 0;
        private const int STATE_GRANTED = 1;

        /// <summary>
        /// Asks the player for a permission and returns whether the game holds it. The system shows
        /// its dialog unless the player answered that permission before; the game is paused while
        /// the dialog is up, and the answer arrives in a later frame.
        /// </summary>
        /// <param name="permission">
        /// One of this class's names, or any other as the system names it. A name without a dot gets
        /// "android.permission." in front, as in the profile's list of other permissions.
        /// </param>
        /// <param name="cancellationToken">
        /// Ends the wait cancelled. Without one the wait ends when play stops.
        /// </param>
        /// <returns>True when the permission is granted.</returns>
        public static async Task<bool> RequestAsync(string permission, CancellationToken cancellationToken = default)
        {
            if(string.IsNullOrEmpty(permission))
            {
                throw new ArgumentException("A permission needs a name.", nameof(permission));
            }
            CancellationToken token = cancellationToken.CanBeCanceled ? cancellationToken : Application.exitCancellationToken;
            uint request = internal_m2n_permissions_request(permission);
            try
            {
                int state = internal_m2n_permissions_get_state(request);
                while(state == STATE_PENDING)
                {
                    await Awaitable.NextFrameAsync(token);
                    state = internal_m2n_permissions_get_state(request);
                }
                return state == STATE_GRANTED;
            }
            finally
            {
                internal_m2n_permissions_forget(request);
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_permissions_request(string permission);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_permissions_get_state(uint request);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_permissions_forget(uint request);
    }

}
