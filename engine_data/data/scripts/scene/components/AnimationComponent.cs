using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Provides functionality to manage and blend animation clips on an entity.
    /// </summary>
    public class AnimationComponent : Component
    {
        /// <summary>
        /// The speed of the animation. 1.0 is normal speed, 2.0 is double speed, 0.5 is half speed.
        /// </summary>
        public float Speed
        {
            get => internal_m2n_animation_get_speed(owner);
            set => internal_m2n_animation_set_speed(owner, value);
        }

        /// <summary>
        /// Whether the animation autoplays when the entity starts.
        /// </summary>
        public bool autoplay
        {
            get => internal_m2n_animation_get_autoplay(owner);
            set => internal_m2n_animation_set_autoplay(owner, value);
        }

        /// <summary>
        /// Whether root motion from the clip is applied to the transform.
        /// </summary>
        public bool applyRootMotion
        {
            get => internal_m2n_animation_get_apply_root_motion(owner);
            set => internal_m2n_animation_set_apply_root_motion(owner, value);
        }

        /// <summary>
        /// Default animation clip assigned to this component.
        /// </summary>
        public AnimationClip animation
        {
            get
            {
                var uid = internal_m2n_animation_get_clip(owner);
                if (uid == Guid.Empty)
                {
                    return null;
                }
                return new AnimationClip { uid = uid };
            }
            set => internal_m2n_animation_set_clip(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Whether the animation player is currently playing.
        /// </summary>
        public bool isPlaying
        {
            get => internal_m2n_animation_is_playing(owner);
        }

        /// <summary>
        /// Whether the animation player is currently paused.
        /// </summary>
        public bool isPaused
        {
            get => internal_m2n_animation_is_paused(owner);
        }

        /// <summary>
        /// Blends the specified animation clip into the default layer.
        /// </summary>
        /// <param name="clip">The animation clip to blend in.</param>
        /// <param name="seconds">Blend transition duration in seconds.</param>
        /// <param name="loop">Whether the clip should loop after playing.</param>
        /// <param name="phaseSync">Whether to synchronize phase with the current animation.</param>
        public void Blend(AnimationClip clip, float seconds, bool loop, bool phaseSync)
        {
            BlendLayer(0, clip, seconds, loop, phaseSync);
        }

        /// <summary>
        /// Blends the specified animation clip into the given layer.
        /// </summary>
        /// <param name="layer">Animation layer index to blend into.</param>
        /// <param name="clip">The animation clip to blend in.</param>
        /// <param name="seconds">Blend transition duration in seconds.</param>
        /// <param name="loop">Whether the clip should loop after playing.</param>
        /// <param name="phaseSync">Whether to synchronize phase with the current animation.</param>
        public void BlendLayer(int layer, AnimationClip clip, float seconds, bool loop, bool phaseSync)
        {
            internal_m2n_animation_blend(owner, layer, clip.uid, seconds, loop, phaseSync);
        }

        /// <summary>
        /// Starts playing the currently blended animation on the entity.
        /// </summary>
        public void Play()
        {
            internal_m2n_animation_play(owner);
        }

        /// <summary>
        /// Pauses the currently playing animation on the entity.
        /// </summary>
        public void Pause()
        {
            internal_m2n_animation_pause(owner);
        }

        /// <summary>
        /// Resumes the currently paused animation on the entity.
        /// </summary>
        public void Resume()
        {
            internal_m2n_animation_resume(owner);
        }

        /// <summary>
        /// Stops the currently playing animation on the entity.
        /// </summary>
        public void Stop()
        {
            internal_m2n_animation_stop(owner);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_blend(Entity eid, int layer, Guid guid, float seconds, bool loop, bool phaseSync);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_play(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_pause(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_resume(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_stop(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_speed(Entity eid, float speed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_get_autoplay(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_autoplay(Entity eid, bool autoplay);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_get_apply_root_motion(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_apply_root_motion(Entity eid, bool apply);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_animation_get_clip(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_clip(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_is_playing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_is_paused(Entity eid);
    }
}
