using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A sound started by <see cref="Audio.PlayOneShot(AudioClip, float, float, AudioBus, int)"/>. Every
    /// call on a voice that finished, was stolen or never started does nothing.
    /// </summary>
    public readonly struct AudioVoice : IEquatable<AudioVoice>
    {
        internal readonly ulong id;

        internal AudioVoice(ulong id)
        {
            this.id = id;
        }

        /// <summary>
        /// Whether the sound started (a clip limit or a missing clip leaves it unstarted).
        /// </summary>
        public bool started => id != 0;

        /// <summary>
        /// Whether the sound is still playing (or paused with the game).
        /// </summary>
        public bool isPlaying => id != 0 && Audio.internal_m2n_audio_voice_is_playing(id);

        /// <summary>
        /// Stops the sound now.
        /// </summary>
        public void Stop()
        {
            Audio.internal_m2n_audio_voice_stop(id);
        }

        /// <summary>
        /// Sets the sound's volume (0..1, before the bus and master gains); ends a running fade.
        /// </summary>
        public void SetVolume(float volume)
        {
            Audio.internal_m2n_audio_voice_set_volume(id, volume);
        }

        /// <summary>
        /// Sets the playback rate (1 = original pitch, 0..5).
        /// </summary>
        public void SetPitch(float pitch)
        {
            Audio.internal_m2n_audio_voice_set_pitch(id, pitch);
        }

        /// <summary>
        /// Ramps the volume to @p volume over @p seconds of unscaled time, then stops the sound when
        /// @p stopAtEnd.
        /// </summary>
        public void FadeTo(float volume, float seconds, bool stopAtEnd = false)
        {
            Audio.internal_m2n_audio_voice_fade_to(id, volume, seconds, stopAtEnd);
        }

        /// <inheritdoc/>
        public bool Equals(AudioVoice other) => id == other.id;

        /// <inheritdoc/>
        public override bool Equals(object obj) => obj is AudioVoice other && Equals(other);

        /// <inheritdoc/>
        public override int GetHashCode() => id.GetHashCode();
    }

    /// <summary>
    /// Global audio mixer controls (master + per-bus gains), fire-and-forget sounds and voice-limit info.
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
        /// Sets the gain for a mixer bus; ends a running fade of that bus.
        /// </summary>
        public static void SetBusVolume(AudioBus bus, float volume)
        {
            internal_m2n_audio_set_bus_volume((byte)bus, volume);
        }

        /// <summary>
        /// Ramps a bus gain to @p target over @p seconds of unscaled time (it keeps going while
        /// Time.timeScale is 0).
        /// </summary>
        public static void FadeBusVolume(AudioBus bus, float target, float seconds)
        {
            internal_m2n_audio_fade_bus_volume((byte)bus, target, seconds);
        }

        /// <summary>
        /// Plays @p clip once in 2D, without an entity. The voice counts toward <see cref="maxVoices"/>
        /// and ends with the play session.
        /// </summary>
        /// <param name="clip">The clip to play.</param>
        /// <param name="volume">Gain before the bus and master gains, 0..1.</param>
        /// <param name="pitch">Playback rate (1 = original pitch).</param>
        /// <param name="bus">Mixer bus.</param>
        /// <param name="priority">Voice-steal priority: at the voice cap, higher values are kept. A one-shot
        /// goes before an audio source of the same priority.</param>
        public static AudioVoice PlayOneShot(AudioClip clip, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus.Sfx, int priority = 0)
        {
            if (clip == null)
            {
                return default;
            }
            return new AudioVoice(internal_m2n_audio_play_one_shot(clip.uid, volume, pitch, (byte)bus, priority, false, Vector3.zero));
        }

        /// <summary>
        /// Plays @p clip once at a world position (3D), without an entity.
        /// </summary>
        /// <param name="clip">The clip to play.</param>
        /// <param name="position">World position of the sound.</param>
        /// <param name="volume">Gain before the bus and master gains, 0..1.</param>
        /// <param name="pitch">Playback rate (1 = original pitch).</param>
        /// <param name="bus">Mixer bus.</param>
        /// <param name="priority">Voice-steal priority: at the voice cap, higher values are kept. A one-shot
        /// goes before an audio source of the same priority.</param>
        public static AudioVoice PlayOneShot(AudioClip clip, Vector3 position, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus.Sfx, int priority = 0)
        {
            if (clip == null)
            {
                return default;
            }
            return new AudioVoice(internal_m2n_audio_play_one_shot(clip.uid, volume, pitch, (byte)bus, priority, true, position));
        }

        /// <summary>
        /// Limits how often @p clip plays as a one-shot: at most @p maxVoices at once (the oldest
        /// stops for a new one; 0 = no limit), and none within @p minInterval seconds of the last
        /// one. Limits end with the play session.
        /// </summary>
        public static void SetClipLimit(AudioClip clip, int maxVoices, float minInterval = 0.0f)
        {
            if (clip == null)
            {
                return;
            }
            internal_m2n_audio_set_clip_limit(clip.uid, maxVoices, minInterval);
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_audio_fade_bus_volume(byte bus, float target, float seconds);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong internal_m2n_audio_play_one_shot(Guid clip, float volume, float pitch, byte bus, int priority, bool spatial, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_audio_set_clip_limit(Guid clip, int maxVoices, float minInterval);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_audio_voice_stop(ulong voice);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool internal_m2n_audio_voice_is_playing(ulong voice);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_audio_voice_set_volume(ulong voice, float volume);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_audio_voice_set_pitch(ulong voice, float pitch);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_audio_voice_fade_to(ulong voice, float volume, float seconds, bool stopAtEnd);
    }
}
