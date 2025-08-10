using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
/// <summary>
/// Represents an audio source component that can play, pause, and control audio playback in a scene.
/// </summary>
public class AudioSourceComponent : Component
{
    private AudioClip clip_;

    /// <summary>
    /// Gets or sets the audio clip currently assigned to the audio source.
    /// </summary>
    /// <value>The currently assigned audio clip.</value>
    public AudioClip clip
    {
        get
        {
            var uid = internal_m2n_audio_source_get_audio_clip(owner);

            if (uid == Guid.Empty)
            {
                clip_ = null;
            }
            else if (clip_ == null || clip_.uid != uid)
            {
                clip_ = new AudioClip { uid = uid };
            }

            return clip_;
        }
        set
        {
            clip_ = value;
            internal_m2n_audio_source_set_audio_clip(owner, clip_?.uid ?? Guid.Empty);
        }
    }

    /// <summary>
    /// Gets or sets a value indicating whether the audio source loops playback.
    /// </summary>
    /// <value><c>true</c> if the audio source is set to loop; otherwise, <c>false</c>.</value>
    public bool loop
    {
        get
        {
            return internal_m2n_audio_source_get_loop(owner);
        }
        set
        {
            internal_m2n_audio_source_set_loop(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets the volume of the audio source.
    /// </summary>
    /// <value>A float value representing the audio source volume, where 1.0 is the default level.</value>
    public float volume
    {
        get
        {
            return internal_m2n_audio_source_get_volume(owner);
        }
        set
        {
            internal_m2n_audio_source_set_volume(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets the pitch of the audio source.
    /// </summary>
    /// <value>A float value representing the pitch, where 1.0 is normal pitch.</value>
    public float pitch
    {
        get
        {
            return internal_m2n_audio_source_get_pitch(owner);
        }
        set
        {
            internal_m2n_audio_source_set_pitch(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets the volume rolloff factor of the audio source.
    /// </summary>
    /// <value>A float value determining how the volume decreases with distance from the source.</value>
    public float volumeRolloff
    {
        get
        {
            return internal_m2n_audio_source_get_volume_rolloff(owner);
        }
        set
        {
            internal_m2n_audio_source_set_volume_rolloff(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets the minimum distance for the audio source.
    /// </summary>
    /// <value>The minimum distance within which the audio source plays at full volume.</value>
    public float minDistance
    {
        get
        {
            return internal_m2n_audio_source_get_min_distance(owner);
        }
        set
        {
            internal_m2n_audio_source_set_min_distance(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets the maximum distance for the audio source.
    /// </summary>
    /// <value>The maximum distance beyond which the audio source volume is effectively zero.</value>
    public float maxDistance
    {
        get
        {
            return internal_m2n_audio_source_get_max_distance(owner);
        }
        set
        {
            internal_m2n_audio_source_set_max_distance(owner, value);
        }
    }

    /// <summary>
    /// Gets or sets a value indicating whether the audio source is muted.
    /// </summary>
    /// <value><c>true</c> if the audio source is muted; otherwise, <c>false</c>.</value>
    public bool mute
    {
        get
        {
            return internal_m2n_audio_source_get_mute(owner);
        }
        set
        {
            internal_m2n_audio_source_set_mute(owner, value);
        }
    }

    /// <summary>
    /// Gets a value indicating whether the audio source is currently playing.
    /// </summary>
    /// <value><c>true</c> if the audio source is playing; otherwise, <c>false</c>.</value>
    public bool isPlaying
    {
        get
        {
            return internal_m2n_audio_source_is_playing(owner);
        }
    }

    /// <summary>
    /// Gets a value indicating whether the audio source is currently paused.
    /// </summary>
    /// <value><c>true</c> if the audio source is paused; otherwise, <c>false</c>.</value>
    public bool isPaused
    {
        get
        {
            return internal_m2n_audio_source_is_paused(owner);
        }
    }

    /// <summary>
    /// Gets or sets the current playback time of the audio source.
    /// </summary>
    /// <value>The current playback time in seconds.</value>
    public float time
    {
        get
        {
            return internal_m2n_audio_source_get_time(owner);
        }
        set
        {
            internal_m2n_audio_source_set_time(owner, value);
        }
    }

    /// <summary>
    /// Starts playing the audio source from the beginning.
    /// </summary>
    public void Play()
    {
        internal_m2n_audio_source_play(owner);
    }

    /// <summary>
    /// Stops playback of the audio source.
    /// </summary>
    public void Stop()
    {
        internal_m2n_audio_source_stop(owner);
    }

    /// <summary>
    /// Pauses playback of the audio source.
    /// </summary>
    public void Pause()
    {
        internal_m2n_audio_source_pause(owner);
    }

    /// <summary>
    /// Resumes playback of the audio source.
    /// </summary>
    public void Resume()
    {
        internal_m2n_audio_source_resume(owner);
    }

    /// <summary>
    /// Plays a one-shot audio clip at the specified position with a specified volume.
    /// </summary>
    /// <param name="clip">The audio clip to play.</param>
    /// <param name="position">The world position where the audio will be played.</param>
    /// <param name="volume">The volume at which the audio will be played. Default is 1.0.</param>
    public static Entity PlayClipAtPoint(AudioClip clip, Vector3 position, float volume = 1.0f)
    {
        var entity = Scene.CreateEntity("One shot audio");
        entity.transform.position = position;
        var audioSource = entity.AddComponent<AudioSourceComponent>();
        audioSource.clip = clip;
        audioSource.volume = volume;
        audioSource.Play();
        Scene.DestroyEntity(entity, clip.length * ((Time.timeScale < 0.01f) ? 0.01f : Time.timeScale));
        return entity;
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_audio_source_get_loop(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_loop(Entity eid, bool loop);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_volume(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_volume(Entity eid, float volume);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_pitch(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_pitch(Entity eid, float pitch);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_volume_rolloff(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_volume_rolloff(Entity eid, float rolloff);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_min_distance(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_min_distance(Entity eid, float distance);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_max_distance(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_max_distance(Entity eid, float distance);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_source_get_time(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_time(Entity eid, float seconds);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_audio_source_get_mute(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_mute(Entity eid, bool mute);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_audio_source_is_playing(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_audio_source_is_paused(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_play(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_stop(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_pause(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_resume(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_audio_source_get_audio_clip(Entity eid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_audio_source_set_audio_clip(Entity eid, Guid uid);
}
}
}
