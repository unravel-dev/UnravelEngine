using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// The post-process materials a post process volume or a camera runs over the view: each entry is a Post Process
    /// material, how strongly it shows, and its own overrides of the material's parameters.
    /// </summary>
    /// <remarks>
    /// On a volume the entries fade with the volume's contribution; on a camera they always apply, after every
    /// volume's, which suits gameplay effects such as a damage flash: add the material once, then animate its weight.
    /// An entry's material runs at the Location its output node names (before transparent objects, before bloom, or
    /// after tonemapping). Entries are addressed by index, which changes when an earlier entry is removed.
    /// </remarks>
    public class PostProcessMaterialComponent : Component
    {
        /// <summary>The first slot an entry's <see cref="MaterialInstance"/> passes to the native calls; entry i
        /// passes this plus i. Mirrors POST_PROCESS_MATERIAL_INSTANCE_SLOT_BASE in model_component_bindings.cpp.</summary>
        internal const uint InstanceSlotBase = 0xFF000000u;

        /// <summary>How many entries there are.</summary>
        public int count => internal_m2n_post_materials_count(owner);

        /// <summary>
        /// Runs <paramref name="material"/> at <paramref name="weight"/>: adds an entry for it, or sets the weight of
        /// the entry it has.
        /// </summary>
        /// <param name="material">A Post Process material; one of another domain runs nothing.</param>
        /// <param name="weight">How strongly it shows, 0 to 1: it scales the material's Opacity.</param>
        /// <returns>The entry's overrides of the material, to set its parameters for this entry only.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="material"/> is null.</exception>
        public MaterialInstance Add(Material material, float weight = 1.0f)
        {
            if (material == null)
            {
                throw new ArgumentNullException(nameof(material));
            }
            var index = internal_m2n_post_materials_add(owner, material.uid, weight);
            return index < 0 ? null : GetInstance(index);
        }

        /// <summary>Stops running <paramref name="material"/>: removes its entry.</summary>
        /// <param name="material">The material whose entry to remove.</param>
        /// <returns>True when there was an entry of it.</returns>
        public bool Remove(Material material)
        {
            return material != null && internal_m2n_post_materials_remove(owner, material.uid);
        }

        /// <summary>The index of the entry of <paramref name="material"/>.</summary>
        /// <param name="material">The material to look for.</param>
        /// <returns>The entry's index, or -1 when there is none.</returns>
        public int IndexOf(Material material)
        {
            return material == null ? -1 : internal_m2n_post_materials_index_of(owner, material.uid);
        }

        /// <summary>The material of entry <paramref name="index"/>.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <returns>The material, or null for an index outside the entries.</returns>
        public Material GetMaterial(int index)
        {
            var uid = internal_m2n_post_materials_get_material(owner, index);
            return uid == Guid.Empty ? null : new Material { uid = uid };
        }

        /// <summary>How strongly entry <paramref name="index"/> shows, 0 to 1.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <returns>The weight, or 0 for an index outside the entries.</returns>
        public float GetWeight(int index)
        {
            return internal_m2n_post_materials_get_weight(owner, index);
        }

        /// <summary>Sets how strongly entry <paramref name="index"/> shows; animate it to fade the material.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <param name="weight">0 shows nothing, 1 the material as it is.</param>
        public void SetWeight(int index, float weight)
        {
            internal_m2n_post_materials_set_weight(owner, index, weight);
        }

        /// <summary>Whether entry <paramref name="index"/> runs.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <returns>False also for an index outside the entries.</returns>
        public bool IsEnabled(int index)
        {
            return internal_m2n_post_materials_get_enabled(owner, index);
        }

        /// <summary>Switches entry <paramref name="index"/> on or off.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <param name="enabled">Whether it runs.</param>
        public void SetEnabled(int index, bool enabled)
        {
            internal_m2n_post_materials_set_enabled(owner, index, enabled);
        }

        /// <summary>Entry <paramref name="index"/>'s overrides of its material: set a parameter here and only this
        /// entry changes.</summary>
        /// <param name="index">The entry, from 0.</param>
        /// <returns>The instance, which follows the entry at that index; null for an index outside the entries.</returns>
        public MaterialInstance GetInstance(int index)
        {
            if (index < 0 || index >= count)
            {
                return null;
            }
            return new MaterialInstance(owner, InstanceSlotBase + (uint)index);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_post_materials_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_post_materials_index_of(Entity eid, Guid material);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_post_materials_add(Entity eid, Guid material, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_post_materials_remove(Entity eid, Guid material);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Guid internal_m2n_post_materials_get_material(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_post_materials_get_weight(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_post_materials_set_weight(Entity eid, int index, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_post_materials_get_enabled(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_post_materials_set_enabled(Entity eid, int index, bool enabled);
    }
}
