using System;
using System.Runtime.CompilerServices;

namespace Ace.Core
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
        /// Blends the specified animation clip into the default layer.
        /// </summary>
        /// <param name="clip">The animation clip to blend in.</param>
        /// <param name="seconds">The duration of the blend transition, in seconds.</param>
        /// <param name="loop">Whether the animation should loop after playing.</param>
        /// <param name="phaseSync">Whether to synchronize the phase of the new clip with the current animation.</param>
        public void Blend(AnimationClip clip, float seconds, bool loop, bool phaseSync)
        {
            BlendLayer(0, clip, seconds, loop, phaseSync);
        }

        /// <summary>
        /// Blends the specified animation clip into the given layer.
        /// </summary>
        /// <param name="layer">The animation layer index to blend into.</param>
        /// <param name="clip">The animation clip to blend in.</param>
        /// <param name="seconds">The duration of the blend transition, in seconds.</param>
        /// <param name="loop">Whether the animation should loop after playing.</param>
        /// <param name="phaseSync">Whether to synchronize the phase of the new clip with the current animation.</param>
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
    }
}
