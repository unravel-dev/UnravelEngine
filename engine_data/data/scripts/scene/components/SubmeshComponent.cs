using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Per-node submesh render settings authored on armature node entities.
    /// </summary>
    public class SubmeshComponent : Component
    {
        /// <summary>
        /// Number of submesh entries on this node.
        /// </summary>
        public int entryCount
        {
            get => internal_m2n_submesh_get_entry_count(owner);
        }

        /// <summary>
        /// Gets the runtime submesh index for the entry at <paramref name="index"/>.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <returns>Runtime submesh index into the mesh asset.</returns>
        public uint GetSubmeshIndex(int index)
        {
            return internal_m2n_submesh_get_submesh_index(owner, index);
        }

        /// <summary>
        /// Gets the import-stable submesh id for the entry at <paramref name="index"/>.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <returns>Stable id, or 0 when unknown.</returns>
        public uint GetStableId(int index)
        {
            return internal_m2n_submesh_get_stable_id(owner, index);
        }

        /// <summary>
        /// Gets whether the entry at <paramref name="index"/> is rendered.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <returns><c>true</c> if the submesh is rendered; otherwise <c>false</c>.</returns>
        public bool GetEnabled(int index)
        {
            return internal_m2n_submesh_get_enabled(owner, index);
        }

        /// <summary>
        /// Sets whether the entry at <paramref name="index"/> is rendered.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <param name="enabled"><c>true</c> to render the submesh; otherwise <c>false</c>.</param>
        public void SetEnabled(int index, bool enabled)
        {
            internal_m2n_submesh_set_enabled(owner, index, enabled);
        }

        /// <summary>
        /// Gets whether the entry at <paramref name="index"/> casts shadows.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <returns><c>true</c> if the submesh casts shadows; otherwise <c>false</c>.</returns>
        public bool GetCastsShadow(int index)
        {
            return internal_m2n_submesh_get_casts_shadow(owner, index);
        }

        /// <summary>
        /// Sets whether the entry at <paramref name="index"/> casts shadows.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <param name="castsShadow"><c>true</c> to cast shadows; otherwise <c>false</c>.</param>
        public void SetCastsShadow(int index, bool castsShadow)
        {
            internal_m2n_submesh_set_casts_shadow(owner, index, castsShadow);
        }

        /// <summary>
        /// Gets the material override uid for the entry at <paramref name="index"/>, or <see cref="Guid.Empty"/>.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <returns>Material asset uid, or empty when using the model material.</returns>
        public Guid GetMaterialOverrideUid(int index)
        {
            return internal_m2n_submesh_get_material_override(owner, index);
        }

        /// <summary>
        /// Sets the material override for the entry at <paramref name="index"/>. Pass null to clear.
        /// </summary>
        /// <param name="index">Zero-based entry index.</param>
        /// <param name="material">Material override, or <c>null</c> to use the model material.</param>
        public void SetMaterialOverride(int index, Material material)
        {
            internal_m2n_submesh_set_material_override(owner, index, material?.uid ?? Guid.Empty);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_submesh_get_entry_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_submesh_get_submesh_index(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_submesh_get_stable_id(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_submesh_get_enabled(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_submesh_set_enabled(Entity eid, int index, bool enabled);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_submesh_get_casts_shadow(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_submesh_set_casts_shadow(Entity eid, int index, bool castsShadow);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_submesh_get_material_override(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_submesh_set_material_override(Entity eid, int index, Guid uid);
    }
}
