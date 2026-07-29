using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Global audio mixer controls (master + per-bus gains) and voice-limit info.
    /// </summary>
    public static class Audio
    {
        /// <summary>
        /// Soft cap on concurrent OpenAL voices before stealing begins.
        /// </summary>
        public static int maxVoices => internal_m2n_audio_get_max_voices();

        /// <summary>
        /// Gets or sets the master mixer gain applied to every source.
        /// </summary>
        public static float masterVolume
        {
            get => internal_m2n_audio_get_master_volume();
            set => internal_m2n_audio_set_master_volume(value);
        }

        /// <summary>
        /// Gets the gain for a mixer bus.
        /// </summary>
        public static float GetBusVolume(AudioBus bus)
        {
            return internal_m2n_audio_get_bus_volume((byte)bus);
        }

        /// <summary>
        /// Sets the gain for a mixer bus.
        /// </summary>
        public static void SetBusVolume(AudioBus bus, float volume)
        {
            internal_m2n_audio_set_bus_volume((byte)bus, volume);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_audio_get_master_volume();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_audio_set_master_volume(float volume);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_audio_get_bus_volume(byte bus);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_audio_set_bus_volume(byte bus, float volume);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_audio_get_max_voices();
    }
}
