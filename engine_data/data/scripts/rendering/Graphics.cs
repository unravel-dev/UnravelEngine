using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Draws meshes without entities. For systems that keep many objects in script arrays (crowds, projectiles,
    /// debris): one call per frame draws them all as instances, in the same passes as models (lighting, shadows,
    /// motion for anti-aliasing and motion blur), culled by ground tile against every camera and shadow.
    /// </summary>
    public static class Graphics
    {
        /// <summary>
        /// Draws <paramref name="mesh"/> once at every matrix of <paramref name="transforms"/>, this frame only:
        /// call it every frame the instances should show, from the main thread (the matrices can be computed on
        /// worker threads before). The engine copies the matrices, so the arrays can be reused at once. The mesh's
        /// non-skinned submeshes draw.
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
        public static void DrawMeshInstanced(Mesh mesh,
                                             Material material,
                                             ReadOnlySpan<Matrix4x4> transforms,
                                             ReadOnlySpan<Matrix4x4> previousTransforms = default,
                                             bool castShadows = true,
                                             int layerMask = 1)
        {
            if (mesh == null)
            {
                throw new ArgumentNullException(nameof(mesh));
            }
            if (!previousTransforms.IsEmpty && previousTransforms.Length != transforms.Length)
            {
                throw new ArgumentException("previousTransforms must be empty or as long as transforms.", nameof(previousTransforms));
            }
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
                                                              (IntPtr)current,
                                                              transforms.Length,
                                                              (IntPtr)previous,
                                                              castShadows,
                                                              layerMask);
                }
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_graphics_draw_mesh_instanced(Guid mesh, Guid material, IntPtr transforms, int count, IntPtr previous, bool castShadows, int layerMask);
    }
}
