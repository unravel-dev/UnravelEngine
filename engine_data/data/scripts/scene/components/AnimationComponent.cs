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
        /// State machine played by this component. While one is assigned it drives the animation
        /// (the single <see cref="animation"/> clip is ignored); steer it with parameters
        /// (<see cref="SetFloat"/>, <see cref="SetBool"/>, <see cref="SetTrigger"/>...).
        /// </summary>
        public AnimationController controller
        {
            get
            {
                var uid = internal_m2n_animation_get_controller(owner);
                if (uid == Guid.Empty)
                {
                    return null;
                }
                return new AnimationController { uid = uid };
            }
            set => internal_m2n_animation_set_controller(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Sets a float parameter of the controller. Int parameters take the rounded value.
        /// </summary>
        /// <returns>False when the controller has no such parameter.</returns>
        public bool SetFloat(string name, float value) => internal_m2n_animation_set_float(owner, name, value);

        /// <summary>Value of a float (or int) parameter of the controller.</summary>
        public float GetFloat(string name) => internal_m2n_animation_get_float(owner, name);

        /// <summary>Sets an int parameter of the controller.</summary>
        /// <returns>False when the controller has no such parameter.</returns>
        public bool SetInt(string name, int value) => internal_m2n_animation_set_int(owner, name, value);

        /// <summary>Value of an int (or float, rounded) parameter of the controller.</summary>
        public int GetInt(string name) => internal_m2n_animation_get_int(owner, name);

        /// <summary>Sets a bool parameter of the controller.</summary>
        /// <returns>False when the controller has no such parameter.</returns>
        public bool SetBool(string name, bool value) => internal_m2n_animation_set_bool(owner, name, value);

        /// <summary>Value of a bool (or trigger) parameter of the controller.</summary>
        public bool GetBool(string name) => internal_m2n_animation_get_bool(owner, name);

        /// <summary>
        /// Sets a trigger: it stays set until a transition that tests it fires (which clears it) or
        /// <see cref="ResetTrigger"/> is called.
        /// </summary>
        /// <returns>False when the controller has no such parameter.</returns>
        public bool SetTrigger(string name) => internal_m2n_animation_set_trigger(owner, name);

        /// <summary>Clears a trigger that no transition consumed.</summary>
        public bool ResetTrigger(string name) => internal_m2n_animation_reset_trigger(owner, name);

        /// <summary>
        /// Name of the state a controller layer plays - during a transition, the state being left.
        /// </summary>
        public string GetCurrentStateName(int layer = 0) => internal_m2n_animation_get_state_name(owner, layer, false);

        /// <summary>Destination of the layer's running transition, or an empty string.</summary>
        public string GetNextStateName(int layer = 0) => internal_m2n_animation_get_state_name(owner, layer, true);

        /// <summary>Whether a controller layer plays the named state (the source, during a transition).</summary>
        public bool IsInState(string state, int layer = 0) => internal_m2n_animation_is_in_state(owner, layer, state);

        /// <summary>Whether the state a controller layer plays carries the tag.</summary>
        public bool IsInTag(string tag, int layer = 0) => internal_m2n_animation_is_in_tag(owner, layer, tag);

        /// <summary>Whether a controller layer is crossfading between two states.</summary>
        public bool IsInTransition(int layer = 0) => internal_m2n_animation_is_in_transition(owner, layer);

        /// <summary>
        /// Normalized time of the state a controller layer plays: the fraction is the position in
        /// the cycle, the integer part counts completed loops.
        /// </summary>
        public float GetStateTime(int layer = 0) => internal_m2n_animation_get_state_time(owner, layer, false);

        /// <summary>Normalized time of the destination of the layer's running transition.</summary>
        public float GetNextStateTime(int layer = 0) => internal_m2n_animation_get_state_time(owner, layer, true);

        /// <summary>Progress (0..1) of the layer's running transition.</summary>
        public float GetTransitionProgress(int layer = 0) => internal_m2n_animation_get_transition_progress(owner, layer);

        /// <summary>
        /// Crossfades a controller layer to a state now, whatever its transitions say.
        /// </summary>
        /// <param name="state">State name.</param>
        /// <param name="seconds">Crossfade duration.</param>
        /// <param name="layer">Controller layer index.</param>
        /// <param name="normalizedTime">Where in its cycle the state starts, 0..1.</param>
        /// <returns>False when the layer has no such state.</returns>
        public bool CrossFade(string state, float seconds, int layer = 0, float normalizedTime = 0.0f)
        {
            return internal_m2n_animation_cross_fade(owner, layer, state, seconds, normalizedTime);
        }

        /// <summary>
        /// Jumps a controller layer to a state now, at a normalized time.
        /// </summary>
        /// <returns>False when the layer has no such state.</returns>
        public bool PlayState(string state, int layer = 0, float normalizedTime = 0.0f)
        {
            return internal_m2n_animation_cross_fade(owner, layer, state, 0.0f, normalizedTime);
        }

        /// <summary>Index of a controller layer by name, or -1.</summary>
        public int GetLayerIndex(string name) => internal_m2n_animation_get_layer_index(owner, name);

        /// <summary>Number of layers of the assigned controller (0 without one).</summary>
        public int layerCount => internal_m2n_animation_get_layer_count(owner);

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
            internal_m2n_animation_blend(owner, layer, clip?.uid ?? Guid.Empty, seconds, loop, phaseSync);
        }

        /// <summary>
        /// Fades a layer above the base layer out over <paramref name="seconds"/>, then clears it.
        /// The base layer stops at once. A layer the controller drives stays stopped until
        /// CrossFade or PlayState starts a state on it.
        /// </summary>
        /// <param name="layer">Animation layer index.</param>
        /// <param name="seconds">Fade duration in seconds.</param>
        public void StopLayer(int layer, float seconds)
        {
            internal_m2n_animation_blend(owner, layer, Guid.Empty, seconds, false, false);
        }

        /// <summary>
        /// Blended value of a curve authored on the playing clips (clip inspector, Curves tab), or
        /// 0 when none of them has it. Curves blend with the pose: a curve fades in and out across
        /// crossfades and layer weights.
        /// </summary>
        /// <param name="name">Curve name.</param>
        public float GetCurve(string name)
        {
            return internal_m2n_animation_get_curve(owner, name);
        }

        /// <summary>
        /// Whether any playing clip has the named curve.
        /// </summary>
        /// <param name="name">Curve name.</param>
        public bool HasCurve(string name)
        {
            return internal_m2n_animation_has_curve(owner, name);
        }

        /// <summary>
        /// Sets the weight (0..1) of a layer above the base layer: how much of the layer's pose
        /// replaces (or, for an additive layer, adds to) the layers below it.
        /// </summary>
        /// <param name="layer">Animation layer index.</param>
        /// <param name="weight">Layer weight, 0..1.</param>
        public void SetLayerWeight(int layer, float weight)
        {
            internal_m2n_animation_set_layer_weight(owner, layer, weight);
        }

        /// <summary>
        /// Weight of a layer (the base layer is always 1).
        /// </summary>
        /// <param name="layer">Animation layer index.</param>
        public float GetLayerWeight(int layer)
        {
            return internal_m2n_animation_get_layer_weight(owner, layer);
        }

        /// <summary>
        /// Makes a layer above the base layer additive (its change from its clips' first frame is
        /// added on top) or override (its pose replaces the pose below, the default).
        /// </summary>
        /// <param name="layer">Animation layer index, 1 or higher.</param>
        /// <param name="additive">True for additive, false for override.</param>
        public void SetLayerAdditive(int layer, bool additive)
        {
            internal_m2n_animation_set_layer_additive(owner, layer, additive);
        }

        /// <summary>
        /// Whether a layer is additive.
        /// </summary>
        /// <param name="layer">Animation layer index.</param>
        public bool IsLayerAdditive(int layer)
        {
            return internal_m2n_animation_get_layer_additive(owner, layer);
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_curve(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_animation_get_controller(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_controller(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_set_float(Entity eid, string name, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_float(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_set_int(Entity eid, string name, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_animation_get_int(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_set_bool(Entity eid, string name, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_get_bool(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_set_trigger(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_reset_trigger(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_animation_get_state_name(Entity eid, int layer, bool next);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_is_in_state(Entity eid, int layer, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_is_in_tag(Entity eid, int layer, string tag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_is_in_transition(Entity eid, int layer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_state_time(Entity eid, int layer, bool next);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_transition_progress(Entity eid, int layer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_cross_fade(Entity eid, int layer, string state, float seconds, float normalizedTime);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_animation_get_layer_index(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_animation_get_layer_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_has_curve(Entity eid, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_layer_weight(Entity eid, int layer, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_animation_get_layer_weight(Entity eid, int layer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_animation_set_layer_additive(Entity eid, int layer, bool additive);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_animation_get_layer_additive(Entity eid, int layer);
    }
}
