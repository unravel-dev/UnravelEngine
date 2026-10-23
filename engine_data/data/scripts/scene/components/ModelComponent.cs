using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Represents a component that provides model rendering capabilities for an entity.
    /// </summary>
    public class ModelComponent : Component
    {
        /// <summary>
        /// Gets or sets a value indicating whether the model is enabled.
        /// </summary>
        /// <value>
        /// <c>true</c> if the model is enabled; otherwise, <c>false</c>.
        /// </value>
        public bool enabled
        {
            get
            {
                return internal_m2n_model_get_enabled(owner);
            }
            set
            {
                internal_m2n_model_set_enabled(owner, value);
            }
        }

        /// <summary>
        /// Whether the model casts shadows.
        /// </summary>
        public bool castsShadow
        {
            get => internal_m2n_model_get_casts_shadow(owner);
            set => internal_m2n_model_set_casts_shadow(owner, value);
        }

        /// <summary>
        /// Whether the model is treated as static for rendering.
        /// </summary>
        public bool isStatic
        {
            get => internal_m2n_model_get_static(owner);
            set => internal_m2n_model_set_static(owner, value);
        }

        /// <summary>
        /// LOD 0 mesh asset assigned to the model.
        /// </summary>
        public Mesh mesh
        {
            get
            {
                var uid = internal_m2n_model_get_mesh(owner);
                if (uid == Guid.Empty)
                {
                    return null;
                }
                return new Mesh { uid = uid };
            }
            set => internal_m2n_model_set_mesh(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// LOD selection bias applied when choosing mesh LOD levels.
        /// </summary>
        public float lodSelectionBias
        {
            get => internal_m2n_model_get_lod_selection_bias(owner);
            set => internal_m2n_model_set_lod_selection_bias(owner, value);
        }

        /// <summary>
        /// Gets the number of material slots of the model: each slot has a shared material and a material instance.
        /// </summary>
        public int GetSharedMaterialsCount()
        {
            return internal_m2n_model_get_shared_material_count(owner);
        }

        /// <summary>
        /// Gets the shared material of a slot: the material asset every renderer that uses it shares.
        /// </summary>
        /// <param name="slot">The material slot.</param>
        /// <returns>The shared material, or <c>null</c> when the slot has none.</returns>
        public Material GetSharedMaterial(uint slot = 0)
        {
            var uid = internal_m2n_model_get_shared_material(owner, slot);
            return uid == Guid.Empty ? null : new Material { uid = uid };
        }

        /// <summary>
        /// Sets the shared material of a slot. The slot's material instance keeps its overrides, which then apply on
        /// top of this material.
        /// </summary>
        /// <param name="material">The shared material, or <c>null</c> to clear the slot.</param>
        /// <param name="slot">The material slot.</param>
        public void SetSharedMaterial(Material material, uint slot = 0)
        {
            internal_m2n_model_set_shared_material(owner, material?.uid ?? Guid.Empty, slot);
        }

        /// <summary>
        /// Gets the material instance of a slot: this renderer's overrides of the slot's shared material. Setting one
        /// of its properties changes this renderer only.
        /// </summary>
        /// <remarks>Every call for a slot returns the same object, so keeping it costs nothing.</remarks>
        /// <param name="slot">The material slot.</param>
        public MaterialInstance GetMaterialInstance(uint slot = 0)
        {
            if (slot >= materialInstances.Length)
            {
                Array.Resize(ref materialInstances, (int)slot + 1);
            }
            return materialInstances[slot] ??= new MaterialInstance(owner, slot);
        }

        private MaterialInstance[] materialInstances = Array.Empty<MaterialInstance>();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_model_get_enabled(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_enabled(Entity eid, bool enabled);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_model_get_casts_shadow(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_casts_shadow(Entity eid, bool castsShadow);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_model_get_static(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_static(Entity eid, bool isStatic);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_model_get_mesh(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_mesh(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_model_get_lod_selection_bias(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_lod_selection_bias(Entity eid, float bias);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Guid internal_m2n_model_get_shared_material(Entity eid, uint index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_model_get_shared_material_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_shared_material(Entity eid, Guid guid, uint index);

    }
}

