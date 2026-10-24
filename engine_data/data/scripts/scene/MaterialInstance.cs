using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// One renderer's overrides of the shared material of one model slot, from
    /// <see cref="ModelComponent.GetMaterialInstance"/>.
    /// </summary>
    /// <remarks>
    /// Setting a property overrides it for this renderer only; a property the instance does not override follows the
    /// shared material, also when the shared material changes later. Reading a property returns the value the renderer
    /// draws with. Colors and numbers are per instance: renderers that override only those still draw together with
    /// the other renderers of the shared material (<see cref="sharesDraw"/>). Textures, tiling and modes are per
    /// material: overriding one gives the renderer a draw of its own.
    /// </remarks>
    public sealed class MaterialInstance
    {
        private readonly Entity owner;

        internal MaterialInstance(Entity owner, uint slot)
        {
            this.owner = owner;
            this.slot = slot;
        }

        /// <summary>The model slot this instance belongs to.</summary>
        public uint slot { get; }

        /// <summary>The shared material this instance overrides, or null when the slot has none.</summary>
        public Material sharedMaterial
        {
            get
            {
                var uid = ModelComponent.internal_m2n_model_get_shared_material(owner, slot);
                return uid == Guid.Empty ? null : new Material { uid = uid };
            }
        }

        /// <summary>Base (albedo) color; its alpha fades the surface. Per instance.</summary>
        public Color baseColor
        {
            get => internal_m2n_material_instance_get_color(owner, slot, (int)MaterialProperty.BaseColor);
            set => internal_m2n_material_instance_set_color(owner, slot, (int)MaterialProperty.BaseColor, value);
        }

        /// <summary>Emissive color. Per instance.</summary>
        public Color emissiveColor
        {
            get => internal_m2n_material_instance_get_color(owner, slot, (int)MaterialProperty.EmissiveColor);
            set => internal_m2n_material_instance_set_color(owner, slot, (int)MaterialProperty.EmissiveColor, value);
        }

        /// <summary>Subsurface color; alpha is the subsurface opacity. Per instance.</summary>
        public Color subsurfaceColor
        {
            get => internal_m2n_material_instance_get_color(owner, slot, (int)MaterialProperty.SubsurfaceColor);
            set => internal_m2n_material_instance_set_color(owner, slot, (int)MaterialProperty.SubsurfaceColor, value);
        }

        /// <summary>Multiplier of the emissive color: emitted light = emissiveColor x emissiveIntensity. Per instance.</summary>
        public float emissiveIntensity
        {
            get => internal_m2n_material_instance_get_float(owner, slot, (int)MaterialProperty.EmissiveIntensity);
            set => internal_m2n_material_instance_set_float(owner, slot, (int)MaterialProperty.EmissiveIntensity, value);
        }

        /// <summary>Surface roughness. Per instance.</summary>
        public float roughness
        {
            get => internal_m2n_material_instance_get_float(owner, slot, (int)MaterialProperty.Roughness);
            set => internal_m2n_material_instance_set_float(owner, slot, (int)MaterialProperty.Roughness, value);
        }

        /// <summary>Surface metalness. Per instance.</summary>
        public float metalness
        {
            get => internal_m2n_material_instance_get_float(owner, slot, (int)MaterialProperty.Metalness);
            set => internal_m2n_material_instance_set_float(owner, slot, (int)MaterialProperty.Metalness, value);
        }

        /// <summary>Normal-map bump intensity. Per instance.</summary>
        public float bumpiness
        {
            get => internal_m2n_material_instance_get_float(owner, slot, (int)MaterialProperty.Bumpiness);
            set => internal_m2n_material_instance_set_float(owner, slot, (int)MaterialProperty.Bumpiness, value);
        }

        /// <summary>Alpha below which a masked surface is cut out. Per material.</summary>
        public float alphaCutoff
        {
            get => internal_m2n_material_instance_get_float(owner, slot, (int)MaterialProperty.AlphaCutoff);
            set => internal_m2n_material_instance_set_float(owner, slot, (int)MaterialProperty.AlphaCutoff, value);
        }

        /// <summary>UV tiling scale. Per material.</summary>
        public Vector2 tiling
        {
            get => internal_m2n_material_instance_get_vector(owner, slot, (int)MaterialProperty.Tiling);
            set => internal_m2n_material_instance_set_vector(owner, slot, (int)MaterialProperty.Tiling, value);
        }

        /// <summary>Base color texture, or null for the engine's default map. Per material.</summary>
        public Texture colorMap
        {
            get => GetTexture(MaterialProperty.ColorMap);
            set => SetTexture(MaterialProperty.ColorMap, value);
        }

        /// <summary>Normal map, or null for the engine's default map. Per material.</summary>
        public Texture normalMap
        {
            get => GetTexture(MaterialProperty.NormalMap);
            set => SetTexture(MaterialProperty.NormalMap, value);
        }

        /// <summary>Roughness texture, or null for the engine's default map. Per material.</summary>
        public Texture roughnessMap
        {
            get => GetTexture(MaterialProperty.RoughnessMap);
            set => SetTexture(MaterialProperty.RoughnessMap, value);
        }

        /// <summary>Metalness texture, or null for the engine's default map. Per material.</summary>
        public Texture metalnessMap
        {
            get => GetTexture(MaterialProperty.MetalnessMap);
            set => SetTexture(MaterialProperty.MetalnessMap, value);
        }

        /// <summary>Emissive texture, or null for the engine's default map. Per material.</summary>
        public Texture emissiveMap
        {
            get => GetTexture(MaterialProperty.EmissiveMap);
            set => SetTexture(MaterialProperty.EmissiveMap, value);
        }

        /// <summary>Ambient occlusion texture, or null for the engine's default map. Per material.</summary>
        public Texture aoMap
        {
            get => GetTexture(MaterialProperty.AoMap);
            set => SetTexture(MaterialProperty.AoMap, value);
        }

        /// <summary>How the material treats alpha. Per material.</summary>
        public MaterialAlphaMode alphaMode
        {
            get => (MaterialAlphaMode)internal_m2n_material_instance_get_mode(owner, slot, (int)MaterialProperty.AlphaMode);
            set => internal_m2n_material_instance_set_mode(owner, slot, (int)MaterialProperty.AlphaMode, (int)value);
        }

        /// <summary>Which faces the material hides; None draws both sides. Per material.</summary>
        public MaterialCullMode cullMode
        {
            get => (MaterialCullMode)internal_m2n_material_instance_get_mode(owner, slot, (int)MaterialProperty.CullMode);
            set => internal_m2n_material_instance_set_mode(owner, slot, (int)MaterialProperty.CullMode, (int)value);
        }

        /// <summary>Whether this renderer overrides any property of the shared material.</summary>
        public bool hasOverrides => internal_m2n_material_instance_has_overrides(owner, slot);

        /// <summary>
        /// Whether every override is per instance, so this renderer still draws together with the other renderers of
        /// the shared material.
        /// </summary>
        public bool sharesDraw => internal_m2n_material_instance_shares_draw(owner, slot);

        /// <summary>Whether this renderer overrides <paramref name="property"/>.</summary>
        public bool IsOverridden(MaterialProperty property)
        {
            return internal_m2n_material_instance_is_overridden(owner, slot, (int)property);
        }

        /// <summary>Drops the override of <paramref name="property"/>: it follows the shared material again.</summary>
        public void Revert(MaterialProperty property)
        {
            internal_m2n_material_instance_revert(owner, slot, (int)property);
        }

        /// <summary>Drops every override: the renderer draws exactly like the shared material.</summary>
        public void RevertAll()
        {
            internal_m2n_material_instance_revert_all(owner, slot);
        }

        /// <summary>
        /// Gets the color this renderer draws with: BaseColor, EmissiveColor or SubsurfaceColor.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
        public Color GetColor(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireColor(property);
            return internal_m2n_material_instance_get_color(owner, slot, (int)property);
        }

        /// <summary>
        /// Overrides a color for this renderer: BaseColor, EmissiveColor or SubsurfaceColor. Per instance.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
        public void SetColor(MaterialProperty property, Color value)
        {
            MaterialPropertyTypes.RequireColor(property);
            internal_m2n_material_instance_set_color(owner, slot, (int)property, value);
        }

        /// <summary>
        /// Gets the number this renderer draws with: EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
        public float GetFloat(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireFloat(property);
            return internal_m2n_material_instance_get_float(owner, slot, (int)property);
        }

        /// <summary>
        /// Overrides a number for this renderer: EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.
        /// Per instance, except AlphaCutoff, which is per material.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
        public void SetFloat(MaterialProperty property, float value)
        {
            MaterialPropertyTypes.RequireFloat(property);
            internal_m2n_material_instance_set_float(owner, slot, (int)property, value);
        }

        /// <summary>Gets the vector this renderer draws with: Tiling.</summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
        public Vector2 GetVector(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireVector(property);
            return internal_m2n_material_instance_get_vector(owner, slot, (int)property);
        }

        /// <summary>Overrides a vector for this renderer: Tiling. Per material.</summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
        public void SetVector(MaterialProperty property, Vector2 value)
        {
            MaterialPropertyTypes.RequireVector(property);
            internal_m2n_material_instance_set_vector(owner, slot, (int)property, value);
        }

        /// <summary>
        /// Gets the texture this renderer draws with: ColorMap, NormalMap, RoughnessMap, MetalnessMap, EmissiveMap or
        /// AoMap.
        /// </summary>
        /// <returns>The texture, or null when the renderer uses the engine's default map.</returns>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a texture property.</exception>
        public Texture GetTexture(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireTexture(property);
            var uid = internal_m2n_material_instance_get_texture(owner, slot, (int)property);
            return uid == Guid.Empty ? null : new Texture { uid = uid };
        }

        /// <summary>
        /// Overrides a texture for this renderer: ColorMap, NormalMap, RoughnessMap, MetalnessMap, EmissiveMap or
        /// AoMap. Per material.
        /// </summary>
        /// <param name="property">The texture property.</param>
        /// <param name="texture">The texture, or null for the engine's default map.</param>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a texture property.</exception>
        public void SetTexture(MaterialProperty property, Texture texture)
        {
            MaterialPropertyTypes.RequireTexture(property);
            internal_m2n_material_instance_set_texture(owner, slot, (int)property, texture?.uid ?? Guid.Empty);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_set_color(Entity eid, uint slot, int property, Color value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_material_instance_get_color(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_set_float(Entity eid, uint slot, int property, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_material_instance_get_float(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_set_vector(Entity eid, uint slot, int property, Vector2 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_material_instance_get_vector(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_set_texture(Entity eid, uint slot, int property, Guid texture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_material_instance_get_texture(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_set_mode(Entity eid, uint slot, int property, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_material_instance_get_mode(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_is_overridden(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_revert(Entity eid, uint slot, int property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_revert_all(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_has_overrides(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_shares_draw(Entity eid, uint slot);
    }
}
