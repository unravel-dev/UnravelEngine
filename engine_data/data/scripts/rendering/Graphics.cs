using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>The graphics API the game renders with (<see cref="Graphics.backend"/>).</summary>
    public enum GraphicsBackend
    {
        /// <summary>No graphics: a run without a window, such as tests.</summary>
        None = 0,
        Direct3D11 = 1,
        Direct3D12 = 2,
        Metal = 3,
        OpenGL = 4,
        OpenGLES = 5,
        Vulkan = 6,
        /// <summary>WebGPU, in the browser.</summary>
        WebGPU = 7,
        /// <summary>WebGL 2, in the browser: no compute shaders, so the features that need them are off.</summary>
        WebGL2 = 8,
    }

    /// <summary>Why the browser page started WebGL 2 instead of WebGPU (<see cref="Graphics.fallbackReason"/>).</summary>
    public enum GraphicsFallbackReason
    {
        /// <summary>WebGL 2 does not stand in: the game runs on WebGPU, on a build for WebGL 2 only, or outside the
        /// browser.</summary>
        None = 0,
        /// <summary>The page is not served over HTTPS, which WebGPU needs.</summary>
        InsecurePage = 1,
        /// <summary>The browser offers no WebGPU.</summary>
        ApiMissing = 2,
        /// <summary>The browser gave no WebGPU adapter: a blocked graphics driver, or hardware acceleration turned
        /// off.</summary>
        NoAdapter = 3,
        /// <summary>The WebGPU device could not be created.</summary>
        DeviceFailed = 4,
        /// <summary>WebGPU would draw without the graphics card, WebGL 2 draws with it.</summary>
        SoftwareAdapter = 5,
        /// <summary>The page's address asked for WebGL 2 (graphics=webgl2).</summary>
        Forced = 6,
    }

    /// <summary>A rendering feature whose GPU needs a device may not meet (<see cref="Graphics.IsFeatureAvailable"/>).</summary>
    public enum RenderFeature
    {
        GlobalIllumination = 0,
        GlobalIlluminationPageFeedback = 1,
        HierarchicalDepth = 2,
        ScreenSpaceReflections = 3,
        ScreenSpaceIndirectLighting = 4,
        GroundTruthAmbientOcclusion = 5,
        AdaptiveScreenSpaceAmbientOcclusion = 6,
        AutoExposure = 7,
        LocalExposure = 8,
        GpuPreExposure = 9,
        SkyIrradiance = 10,
        ReflectionProbeFiltering = 11,
        VolumetricFog = 12,
        CloudShadows = 13,
        VertexStorageBuffers = 14,
        ParticleStorageBuffers = 15,
        GpuParticleCulling = 16,
        ParticleReactiveMask = 17,
        SoftShadows = 18,
        UIMultisampling = 19,
        TemporalStabilityProbe = 20,
    }

    /// <summary>
    /// Draws meshes without entities. For systems that keep many objects in script arrays (crowds, projectiles,
    /// debris): one call per frame draws them all as instances, in the same passes as models (lighting, shadows,
    /// motion for anti-aliasing and motion blur), culled by ground tile against every camera and shadow. Also tells
    /// which graphics API the game renders with and which rendering features the device runs.
    /// </summary>
    public static class Graphics
    {
        /// <summary>
        /// The graphics API the game renders with. In the browser, <see cref="GraphicsBackend.WebGL2"/> runs where
        /// WebGPU is unavailable, when the build allows it; <see cref="fallbackReason"/> says why.
        /// </summary>
        public static GraphicsBackend backend => (GraphicsBackend)internal_m2n_graphics_get_backend();

        /// <summary>
        /// Why the browser page started WebGL 2 instead of WebGPU, for a game's own notice or its analytics;
        /// <see cref="GraphicsFallbackReason.None"/> everywhere else.
        /// </summary>
        public static GraphicsFallbackReason fallbackReason => (GraphicsFallbackReason)internal_m2n_graphics_get_fallback_reason();

        /// <summary>
        /// Whether the device runs <paramref name="feature"/>. Where it does not, the engine renders its fallback
        /// (the log names it), and a game can drop an effect that depends on it.
        /// </summary>
        /// <param name="feature">The feature.</param>
        public static bool IsFeatureAvailable(RenderFeature feature)
        {
            return internal_m2n_graphics_is_feature_available((int)feature);
        }

        /// <summary>
        /// Draws <paramref name="mesh"/> once at every matrix of <paramref name="transforms"/>, this frame only (while
        /// the game is paused, the last draws stay): call it every frame the instances should show, from the main
        /// thread (the matrices can be computed on worker threads before). The engine copies the matrices, so the
        /// arrays can be reused at once. The mesh's non-skinned submeshes draw.
        /// </summary>
        /// <param name="mesh">The mesh.</param>
        /// <param name="material">The material of every slot of the mesh; null uses the mesh's own materials.</param>
        /// <param name="transforms">The world matrix of every instance.</param>
        /// <param name="previousTransforms">
        /// Each instance's matrix of the last frame, for the motion of moving instances; empty, or as many as
        /// <paramref name="transforms"/>. Without them instances move with the camera only, like static models.
        /// </param>
        /// <param name="castShadows">The instances cast shadows.</param>
        /// <param name="layerMask">The layers the instances are on; cameras whose render mask shares one draw them.
        /// 1 is the Default layer.</param>
        /// <param name="materialInstance">
        /// A material instance created from a material (<see cref="MaterialInstance(Material)"/>) that every slot draws
        /// in place of <paramref name="material"/>, which is then null or its shared material. Null draws
        /// <paramref name="material"/>. Instances that differ in their material instance go in calls of their own.
        /// </param>
        public static void DrawMeshInstanced(Mesh mesh,
                                             Material material,
                                             ReadOnlySpan<Matrix4x4> transforms,
                                             ReadOnlySpan<Matrix4x4> previousTransforms = default,
                                             bool castShadows = true,
                                             int layerMask = 1,
                                             MaterialInstance materialInstance = null)
        {
            if (mesh == null)
            {
                throw new ArgumentNullException(nameof(mesh));
            }
            if (!previousTransforms.IsEmpty && previousTransforms.Length != transforms.Length)
            {
                throw new ArgumentException("previousTransforms must be empty or as long as transforms.", nameof(previousTransforms));
            }
            int instanceId = GetDrawInstanceId(material, materialInstance);
            if (transforms.IsEmpty)
            {
                return;
            }
            unsafe
            {
                fixed (Matrix4x4* current = transforms)
                fixed (Matrix4x4* previous = previousTransforms)
                {
                    internal_m2n_graphics_draw_mesh_instanced(mesh.uid,
                                                              material != null ? material.uid : Guid.Empty,
                                                              instanceId,
                                                              (IntPtr)current,
                                                              transforms.Length,
                                                              (IntPtr)previous,
                                                              castShadows,
                                                              layerMask);
                }
            }
        }

        /// <summary>
        /// <see cref="DrawMeshInstanced(Mesh, Material, ReadOnlySpan{Matrix4x4}, ReadOnlySpan{Matrix4x4}, bool, int, MaterialInstance)"/>
        /// for an animated mesh: draws <paramref name="animation"/>'s mesh at every matrix of
        /// <paramref name="transforms"/>, each instance in the pose of its state (clip and time), this frame only.
        /// Needs a device that reads vertex storage buffers (not OpenGL ES): elsewhere nothing is drawn.
        /// </summary>
        /// <param name="animation">The baked clips; its mesh is drawn.</param>
        /// <param name="material">The material of every slot of the mesh; null uses the mesh's own materials.</param>
        /// <param name="transforms">The world matrix of every instance.</param>
        /// <param name="states">The clip and time of every instance, as many as <paramref name="transforms"/>.</param>
        /// <param name="previousTransforms">Each instance's matrix of the last frame; empty, or as many as
        /// <paramref name="transforms"/>.</param>
        /// <param name="previousStates">Each instance's state of the last frame, for the motion of its animation; empty,
        /// or as many as <paramref name="transforms"/>.</param>
        /// <param name="castShadows">The instances cast shadows.</param>
        /// <param name="layerMask">The layers the instances are on; cameras whose render mask shares one draw them.
        /// 1 is the Default layer.</param>
        /// <param name="materialInstance">A material instance created from a material that every slot draws in place of
        /// <paramref name="material"/>, which is then null or its shared material; null draws
        /// <paramref name="material"/>.</param>
        public static void DrawMeshInstanced(BakedAnimation animation,
                                             Material material,
                                             ReadOnlySpan<Matrix4x4> transforms,
                                             ReadOnlySpan<BakedAnimationState> states,
                                             ReadOnlySpan<Matrix4x4> previousTransforms = default,
                                             ReadOnlySpan<BakedAnimationState> previousStates = default,
                                             bool castShadows = true,
                                             int layerMask = 1,
                                             MaterialInstance materialInstance = null)
        {
            if (animation == null)
            {
                throw new ArgumentNullException(nameof(animation));
            }
            if (states.Length != transforms.Length)
            {
                throw new ArgumentException("states must be as long as transforms.", nameof(states));
            }
            if (!previousTransforms.IsEmpty && previousTransforms.Length != transforms.Length)
            {
                throw new ArgumentException("previousTransforms must be empty or as long as transforms.", nameof(previousTransforms));
            }
            if (!previousStates.IsEmpty && previousStates.Length != transforms.Length)
            {
                throw new ArgumentException("previousStates must be empty or as long as transforms.", nameof(previousStates));
            }
            int instanceId = GetDrawInstanceId(material, materialInstance);
            if (transforms.IsEmpty || !animation.isValid)
            {
                return;
            }
            unsafe
            {
                fixed (Matrix4x4* current = transforms)
                fixed (BakedAnimationState* currentStates = states)
                fixed (Matrix4x4* previous = previousTransforms)
                fixed (BakedAnimationState* previousStatesPointer = previousStates)
                {
                    internal_m2n_graphics_draw_baked_instanced(animation.nativeId,
                                                               material != null ? material.uid : Guid.Empty,
                                                               instanceId,
                                                               (IntPtr)current,
                                                               (IntPtr)currentStates,
                                                               transforms.Length,
                                                               (IntPtr)previous,
                                                               (IntPtr)previousStatesPointer,
                                                               castShadows,
                                                               layerMask);
                }
            }
        }

        /// <summary>The id a draw finds <paramref name="materialInstance"/> by; 0 for none.</summary>
        private static int GetDrawInstanceId(Material material, MaterialInstance materialInstance)
        {
            if (materialInstance == null)
            {
                return 0;
            }
            int id = materialInstance.drawId;
            if (id == 0)
            {
                throw new ArgumentException("materialInstance must be created from a material and not released.", nameof(materialInstance));
            }
            if (material != null && material.uid != materialInstance.sharedMaterial.uid)
            {
                throw new ArgumentException("material must be null or the shared material of materialInstance.", nameof(material));
            }
            return id;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_graphics_get_backend();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_graphics_get_fallback_reason();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_graphics_is_feature_available(int feature);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_graphics_draw_mesh_instanced(Guid mesh, Guid material, int materialInstance, IntPtr transforms, int count, IntPtr previous, bool castShadows, int layerMask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_graphics_draw_baked_instanced(int animation, Guid material, int materialInstance, IntPtr transforms, IntPtr states, int count, IntPtr previous, IntPtr previousStates, bool castShadows, int layerMask);
    }
}
