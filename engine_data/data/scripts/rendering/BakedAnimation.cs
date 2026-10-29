using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// How <see cref="BakedAnimation.Bake"/> carries clips onto the mesh's skeleton.
    /// </summary>
    public enum BakedAnimationRetarget
    {
        /// <summary>Bones take the clip's local transforms as authored: clips exported with the mesh's own skeleton.</summary>
        Copy,
        /// <summary>
        /// Bones turn as the clip's bones turned from their rest pose: clips from other exports or of other proportions,
        /// as long as both skeletons rest in the same pose.
        /// </summary>
        RestAligned,
    }

    /// <summary>
    /// What one instance of a baked animation draw shows: a clip of the bake (its index in the clips baked) and a time in
    /// it, in seconds. Times outside the clip show its first or last frame; wrap the time of a clip that loops with
    /// <see cref="BakedAnimation.GetLength"/>.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BakedAnimationState
    {
        /// <summary>The clip's index in the clips baked.</summary>
        public int clip;
        /// <summary>The time in the clip, in seconds.</summary>
        public float time;

        /// <summary>Creates a state.</summary>
        /// <param name="clip">The clip's index in the clips baked.</param>
        /// <param name="time">The time in the clip, in seconds.</param>
        public BakedAnimationState(int clip, float time)
        {
            this.clip = clip;
            this.time = time;
        }
    }

    /// <summary>
    /// A skinned mesh's clips sampled once into bone matrices on the GPU, to draw many animated instances without bones
    /// or entities (<see cref="Graphics.DrawMeshInstanced(BakedAnimation, Material, ReadOnlySpan{Matrix4x4}, ReadOnlySpan{BakedAnimationState}, ReadOnlySpan{Matrix4x4}, ReadOnlySpan{BakedAnimationState}, bool, int, MaterialInstance)"/>).
    /// Every instance picks its own clip and time. Clips play in place: their root motion is dropped, the instance's
    /// matrix moves it. Rigid parts on animated bones (a sword in a hand) follow the animation too.
    /// A bake lasts until <see cref="Dispose"/> or until play mode ends. Main thread only.
    /// </summary>
    public sealed class BakedAnimation : IDisposable
    {
        private int id;
        private readonly float[] lengths;

        private BakedAnimation(int id, Mesh mesh, float framesPerSecond, float[] lengths)
        {
            this.id = id;
            this.mesh = mesh;
            this.framesPerSecond = framesPerSecond;
            this.lengths = lengths;
        }

        /// <summary>
        /// Samples <paramref name="clips"/> on <paramref name="mesh"/>'s skeleton, loading them first if needed. Takes
        /// a moment: bake when a level loads, not every frame.
        /// </summary>
        /// <param name="mesh">A mesh with bones.</param>
        /// <param name="clips">The clips; an instance names one by its index here.</param>
        /// <param name="framesPerSecond">Samples per second: more plays smoother and takes more memory.</param>
        /// <param name="retarget">How the clips are carried onto the mesh's skeleton.</param>
        /// <returns>The bake.</returns>
        /// <exception cref="ArgumentException">The mesh has no bones, or a clip is missing.</exception>
        public static BakedAnimation Bake(Mesh mesh,
                                          ReadOnlySpan<AnimationClip> clips,
                                          float framesPerSecond = 30.0f,
                                          BakedAnimationRetarget retarget = BakedAnimationRetarget.Copy)
        {
            if (mesh == null)
            {
                throw new ArgumentNullException(nameof(mesh));
            }
            int id = internal_m2n_baked_animation_create(mesh.uid, framesPerSecond, (int)retarget);
            if (id == 0)
            {
                throw new ArgumentException("The mesh has no bones to bake an animation for.", nameof(mesh));
            }
            var lengths = new float[clips.Length];
            for (int i = 0; i < clips.Length; i++)
            {
                int index = clips[i] != null ? internal_m2n_baked_animation_add_clip(id, clips[i].uid) : -1;
                if (index < 0)
                {
                    internal_m2n_baked_animation_release(id);
                    throw new ArgumentException($"Clip {i} is missing or could not be loaded.", nameof(clips));
                }
                lengths[index] = internal_m2n_baked_animation_get_clip_length(id, index);
            }
            return new BakedAnimation(id, mesh, framesPerSecond, lengths);
        }

        /// <summary>The mesh the clips were baked for.</summary>
        public Mesh mesh { get; }

        /// <summary>Samples per second the clips were baked at.</summary>
        public float framesPerSecond { get; }

        /// <summary>How many clips the bake holds.</summary>
        public int clipCount => lengths.Length;

        /// <summary>False once disposed or after play mode ended.</summary>
        public bool isValid => id != 0 && internal_m2n_baked_animation_is_valid(id);

        /// <summary>Length of a clip in seconds, 0 for an index the bake does not have.</summary>
        /// <param name="clip">The clip's index in the clips baked.</param>
        public float GetLength(int clip)
        {
            return (uint)clip < (uint)lengths.Length ? lengths[clip] : 0.0f;
        }

        /// <summary>Releases the bake's GPU memory. Draws with it afterwards draw nothing.</summary>
        public void Dispose()
        {
            if (id != 0)
            {
                internal_m2n_baked_animation_release(id);
                id = 0;
            }
        }

        internal int nativeId => id;

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_baked_animation_create(Guid mesh, float framesPerSecond, int retarget);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_baked_animation_add_clip(int id, Guid clip);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_baked_animation_get_clip_length(int id, int clip);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_baked_animation_release(int id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_baked_animation_is_valid(int id);
    }
}
