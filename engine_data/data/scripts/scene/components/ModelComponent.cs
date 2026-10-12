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
        /// Gets the number of shared (asset-backed) materials on the model.
        /// </summary>
        public int GetSharedMaterialsCount()
        {
            return internal_m2n_model_get_shared_material_count(owner);
        }

        /// <summary>
        /// Retrieves the material assigned to a specific index in the model.
        /// </summary>
        /// <param name="index">The index of the material to retrieve.</param>
        /// <returns>
        /// The <see cref="Material"/> assigned to the specified index, or <c>null</c> if no material is assigned.
        /// </returns>
        public Material GetSharedMaterial(uint index = 0)
        {
            var uid = internal_m2n_model_get_shared_material(owner, index);

            if (uid == Guid.Empty)
            {
                return null;
            }

            var props = Assets.internal_m2n_get_material_properties(uid);

            if (!props.valid)
            {
                return null;
            }

            Material material = new Material();
            material.uid = uid;
            material.SetProperties(props);

            return material;
        }

        /// <summary>
        /// Gets the number of material instances on the model.
        /// </summary>
        public int GetMaterialsCount()
        {
            return internal_m2n_model_get_material_instance_count(owner);
        }

        /// <summary>
        /// Retrieves the material instance assigned to a specific index in the model.
        /// </summary>
        /// <param name="index">The index of the material instance to retrieve.</param>
        /// <returns>
        /// The <see cref="Material"/> assigned to the specified index, or <c>null</c> if no material is assigned.
        /// </returns>
        public Material GetMaterial(uint index = 0)
        {
            var props = internal_m2n_model_get_material_instance(owner, index);

            if (!props.valid)
            {
                return null;
            }

            Material material = new Material();
            material.SetProperties(props);

            return material;
        }

        /// <summary>
        /// Sets the shared material asset at the specified index for the model.
        /// </summary>
        /// <remarks>
        /// Only asset-backed materials can occupy a shared slot. A material with an empty
        /// <see cref="Asset{T}.uid"/>, such as one returned by <see cref="GetMaterial"/> or built
        /// with <see cref="Material(Material)"/>, is applied as this renderer's material instance
        /// instead of clearing the slot.
        /// </remarks>
        /// <param name="material">The <see cref="Material"/> to assign, or <c>null</c> to remove the material.</param>
        /// <param name="index">The index of the material to set.</param>
        public void SetSharedMaterial(Material material, uint index = 0)
        {
            if (material != null && material.uid == Guid.Empty)
            {
                SetMaterial(material, index);
                return;
            }
            internal_m2n_model_set_shared_material(owner, material?.uid ?? Guid.Empty, index);
        }

        /// <summary>
        /// Sets the material instance at the specified index for the model.
        /// </summary>
        /// <param name="material">The <see cref="Material"/> to assign, or <c>null</c> to remove the material.</param>
        /// <param name="index">The index of the material to set.</param>
        public void SetMaterial(Material material, uint index = 0)
        {
            if (material == null)
            {
                internal_m2n_model_set_material_instance(owner, new MaterialProperties(), index);
                return;
            }
            internal_m2n_model_set_material_instance(owner, material.GetProperties(), index);
        }

        /// <summary>
        /// Clears the material instance of one slot so it uses its shared material again.
        /// </summary>
        /// <param name="slot">The material slot.</param>
        public void ResetMaterial(uint slot)
        {
            SetMaterial(null, slot);
        }

        /// <summary>
        /// Sets one float field of the slot's material. The first write to a slot gives this
        /// renderer its own copy of the shared material; later writes change only that copy.
        /// </summary>
        /// <param name="property">EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.</param>
        /// <param name="value">The new value.</param>
        /// <param name="slot">The material slot.</param>
        public void SetFloat(MaterialProperty property, float value, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Float);
            internal_m2n_model_set_material_float(owner, (int)property, value, slot);
        }

        /// <summary>
        /// Gets one float field of the material the slot renders with.
        /// </summary>
        /// <param name="property">EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.</param>
        /// <param name="slot">The material slot.</param>
        public float GetFloat(MaterialProperty property, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Float);
            return internal_m2n_model_get_material_float(owner, (int)property, slot);
        }

        /// <summary>
        /// Sets one color field of the slot's material (see <see cref="SetFloat"/> for the copy).
        /// </summary>
        /// <param name="property">BaseColor, EmissiveColor or SubsurfaceColor.</param>
        /// <param name="value">The new color.</param>
        /// <param name="slot">The material slot.</param>
        public void SetColor(MaterialProperty property, Color value, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Color);
            internal_m2n_model_set_material_color(owner, (int)property, value, slot);
        }

        /// <summary>
        /// Gets one color field of the material the slot renders with.
        /// </summary>
        /// <param name="property">BaseColor, EmissiveColor or SubsurfaceColor.</param>
        /// <param name="slot">The material slot.</param>
        public Color GetColor(MaterialProperty property, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Color);
            return internal_m2n_model_get_material_color(owner, (int)property, slot);
        }

        /// <summary>
        /// Sets a vector field of the slot's material (see <see cref="SetFloat"/> for the copy).
        /// </summary>
        /// <param name="property">Tiling.</param>
        /// <param name="value">The new value.</param>
        /// <param name="slot">The material slot.</param>
        public void SetVector(MaterialProperty property, Vector2 value, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Vector);
            internal_m2n_model_set_material_vector(owner, (int)property, value, slot);
        }

        /// <summary>
        /// Gets a vector field of the material the slot renders with.
        /// </summary>
        /// <param name="property">Tiling.</param>
        /// <param name="slot">The material slot.</param>
        public Vector2 GetVector(MaterialProperty property, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Vector);
            return internal_m2n_model_get_material_vector(owner, (int)property, slot);
        }

        /// <summary>
        /// Sets a texture of the slot's material (see <see cref="SetFloat"/> for the copy).
        /// </summary>
        /// <param name="property">ColorMap, NormalMap, RoughnessMap, MetalnessMap, EmissiveMap or AoMap.</param>
        /// <param name="texture">The texture, or null for the engine's default map.</param>
        /// <param name="slot">The material slot.</param>
        public void SetTexture(MaterialProperty property, Texture texture, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Texture);
            internal_m2n_model_set_material_texture(owner, (int)property, texture?.uid ?? Guid.Empty, slot);
        }

        /// <summary>
        /// Gets a texture of the material the slot renders with, or null when it has none.
        /// </summary>
        /// <param name="property">ColorMap, NormalMap, RoughnessMap, MetalnessMap, EmissiveMap or AoMap.</param>
        /// <param name="slot">The material slot.</param>
        public Texture GetTexture(MaterialProperty property, uint slot = 0)
        {
            RequireKind(property, PropertyKind.Texture);
            var uid = internal_m2n_model_get_material_texture(owner, (int)property, slot);
            return uid == Guid.Empty ? null : new Texture { uid = uid };
        }

        /// <summary>
        /// Sets how the slot's material treats alpha (see <see cref="SetFloat"/> for the copy).
        /// </summary>
        public void SetAlphaMode(MaterialAlphaMode mode, uint slot = 0)
        {
            internal_m2n_model_set_material_mode(owner, AlphaModeField, (int)mode, slot);
        }

        /// <summary>
        /// Gets how the material the slot renders with treats alpha.
        /// </summary>
        public MaterialAlphaMode GetAlphaMode(uint slot = 0)
        {
            return (MaterialAlphaMode)internal_m2n_model_get_material_mode(owner, AlphaModeField, slot);
        }

        /// <summary>
        /// Sets which faces the slot's material hides; None draws both sides (see
        /// <see cref="SetFloat"/> for the copy).
        /// </summary>
        public void SetCullMode(MaterialCullMode mode, uint slot = 0)
        {
            internal_m2n_model_set_material_mode(owner, CullModeField, (int)mode, slot);
        }

        /// <summary>
        /// Gets which faces the material the slot renders with hides.
        /// </summary>
        public MaterialCullMode GetCullMode(uint slot = 0)
        {
            return (MaterialCullMode)internal_m2n_model_get_material_mode(owner, CullModeField, slot);
        }

        /// <summary>
        /// Clears all material instances so the model uses its shared materials again.
        /// </summary>
        public void ResetMaterials()
        {
            var count = GetSharedMaterialsCount();
            for (uint i = 0; i < count; ++i)
            {
                SetMaterial(null, i);
            }
        }

        /// <summary>
        /// Sets the first material instance color for the model.
        /// </summary>
        /// <param name="color">The <see cref="Color"/> to assign.</param>
        public void SetColor(Color color)
        {
            var count = GetSharedMaterialsCount();
            for (uint i = 0; i < count; ++i)
            {
                SetColor(color, i);
            }
        }

        /// <summary>
        /// Sets the material instance color at the specified index for the model.
        /// </summary>
        /// <remarks>Does nothing for a slot without a PBR material.</remarks>
        /// <param name="color">The <see cref="Color"/> to assign.</param>
        /// <param name="index">The index of the material to assign.</param>
        public void SetColor(Color color, uint index)
        {
            var mat = GetMaterial(index);
            if (mat == null)
            {
                return;
            }
            mat.color = color;
            SetMaterial(mat, index);
        }


        /// <summary>
        /// Gets the first material instance color for the model.
        /// </summary>
        /// <returns>
        /// The <see cref="Color"/> assigned to the specified index, or <c>white</c> if no material is assigned.
        /// </returns>       
        public Color GetColor()
        {
            return GetColor(0);
        }

        /// <summary>
        /// Gets the material instance color for the model at the specified index.
        /// </summary>
        /// <returns>
        /// The <see cref="Color"/> assigned to the specified index, or <c>white</c> if no material is assigned.
        /// </returns>   
        public Color GetColor(uint index)
        {
            var mat = GetMaterial(index);
            return mat != null ? mat.color : Color.white;
        }

        private enum PropertyKind
        {
            Color,
            Float,
            Vector,
            Texture,
        }

        // Native material field ids of the two modes; they follow the MaterialProperty values.
        private const int AlphaModeField = (int)MaterialProperty.AoMap + 1;
        private const int CullModeField = (int)MaterialProperty.AoMap + 2;

        private static PropertyKind KindOf(MaterialProperty property)
        {
            switch (property)
            {
                case MaterialProperty.BaseColor:
                case MaterialProperty.EmissiveColor:
                case MaterialProperty.SubsurfaceColor:
                    return PropertyKind.Color;
                case MaterialProperty.Tiling:
                    return PropertyKind.Vector;
                case MaterialProperty.ColorMap:
                case MaterialProperty.NormalMap:
                case MaterialProperty.RoughnessMap:
                case MaterialProperty.MetalnessMap:
                case MaterialProperty.EmissiveMap:
                case MaterialProperty.AoMap:
                    return PropertyKind.Texture;
                default:
                    return PropertyKind.Float;
            }
        }

        private static void RequireKind(MaterialProperty property, PropertyKind kind)
        {
            if (!Enum.IsDefined(typeof(MaterialProperty), property) || KindOf(property) != kind)
            {
                throw new ArgumentException($"MaterialProperty.{property} is not a {kind.ToString().ToLowerInvariant()} property", nameof(property));
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_float(Entity eid, int property, float value, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_model_get_material_float(Entity eid, int property, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_color(Entity eid, int property, Color value, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_model_get_material_color(Entity eid, int property, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_vector(Entity eid, int property, Vector2 value, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_model_get_material_vector(Entity eid, int property, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_texture(Entity eid, int property, Guid uid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_model_get_material_texture(Entity eid, int property, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_mode(Entity eid, int property, int value, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_model_get_material_mode(Entity eid, int property, uint slot);

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
        private static extern Guid internal_m2n_model_get_shared_material(Entity eid, uint index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_model_get_shared_material_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_shared_material(Entity eid, Guid guid, uint index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_material_instance(Entity eid, MaterialProperties props, uint index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern MaterialProperties internal_m2n_model_get_material_instance(Entity eid, uint index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_model_get_material_instance_count(Entity eid);

    }
}

