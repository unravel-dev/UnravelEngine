using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Overrides of a shared material: one renderer's, of one model slot (from
    /// <see cref="ModelComponent.GetMaterialInstance"/>), or of no renderer, created from a material
    /// (<see cref="MaterialInstance(Material)"/>) for instanced draws (<see cref="Graphics"/>).
    /// </summary>
    /// <remarks>
    /// Setting a property overrides it for this renderer only; a property the instance does not override follows the
    /// shared material, also when the shared material changes later. Reading a property returns the value the renderer
    /// draws with. Colors and numbers are per instance: renderers that override only those still draw together with
    /// the other renderers of the shared material (<see cref="sharesDraw"/>). Textures, tiling and modes are per
    /// material: overriding one gives the renderer a draw of its own.
    /// </remarks>
    public sealed class MaterialInstance : IDisposable
    {
        private readonly Entity owner;
        /// <summary>What the native calls know the instance by: its model slot, or for an instance of no renderer its
        /// id (0 once released).</summary>
        private uint key;
        private readonly Material material;

        internal MaterialInstance(Entity owner, uint slot)
        {
            this.owner = owner;
            this.slot = slot;
            key = slot;
        }

        /// <summary>
        /// Creates a material instance of <paramref name="sharedMaterial"/> that belongs to no renderer: set the values
        /// that should differ and pass it to the instanced draws that should draw them
        /// (<see cref="Graphics.DrawMeshInstanced(Mesh, Material, ReadOnlySpan{Matrix4x4}, ReadOnlySpan{Matrix4x4}, bool, int, MaterialInstance)"/>).
        /// What it does not override follows the shared material. Release it with <see cref="Dispose"/>; play mode
        /// ending releases the rest.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="sharedMaterial"/> is null.</exception>
        public MaterialInstance(Material sharedMaterial)
        {
            if (sharedMaterial == null)
            {
                throw new ArgumentNullException(nameof(sharedMaterial));
            }
            owner = Entity.Invalid;
            material = sharedMaterial;
            key = internal_m2n_material_instance_create(sharedMaterial.uid);
        }

        /// <summary>The model slot this instance belongs to; 0 for an instance of no renderer.</summary>
        public uint slot { get; }

        /// <summary>The shared material this instance overrides, or null when the slot has none.</summary>
        public Material sharedMaterial
        {
            get
            {
                if (isOfNoRenderer)
                {
                    return material;
                }
                var uid = ModelComponent.internal_m2n_model_get_shared_material(owner, slot);
                return uid == Guid.Empty ? null : new Material { uid = uid };
            }
        }

        /// <summary>The id instanced draws find an instance of no renderer by; 0 for a renderer's or a released one.</summary>
        internal int drawId => isOfNoRenderer ? (int)key : 0;

        private bool isOfNoRenderer => owner.Id == 0;

        /// <summary>
        /// Releases an instance of no renderer: draws can no longer use it, and it reads defaults and ignores sets.
        /// Does nothing for a renderer's instance.
        /// </summary>
        public void Dispose()
        {
            if (isOfNoRenderer && key != 0)
            {
                internal_m2n_material_instance_release(key);
                key = 0;
            }
        }

        /// <summary>Base (albedo) color; its alpha fades the surface. Per instance.</summary>
        public Color baseColor
        {
            get => internal_m2n_material_instance_get_color(owner, key, (int)MaterialProperty.BaseColor);
            set => internal_m2n_material_instance_set_color(owner, key, (int)MaterialProperty.BaseColor, value);
        }

        /// <summary>Emissive color. Per instance.</summary>
        public Color emissiveColor
        {
            get => internal_m2n_material_instance_get_color(owner, key, (int)MaterialProperty.EmissiveColor);
            set => internal_m2n_material_instance_set_color(owner, key, (int)MaterialProperty.EmissiveColor, value);
        }

        /// <summary>Subsurface color; alpha is the subsurface opacity. Per instance.</summary>
        public Color subsurfaceColor
        {
            get => internal_m2n_material_instance_get_color(owner, key, (int)MaterialProperty.SubsurfaceColor);
            set => internal_m2n_material_instance_set_color(owner, key, (int)MaterialProperty.SubsurfaceColor, value);
        }

        /// <summary>Multiplier of the emissive color: emitted light = emissiveColor x emissiveIntensity. Per instance.</summary>
        public float emissiveIntensity
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.EmissiveIntensity);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.EmissiveIntensity, value);
        }

        /// <summary>Surface roughness. Per instance.</summary>
        public float roughness
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.Roughness);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.Roughness, value);
        }

        /// <summary>Surface metalness. Per instance.</summary>
        public float metalness
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.Metalness);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.Metalness, value);
        }

        /// <summary>Normal-map bump intensity. Per instance.</summary>
        public float bumpiness
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.Bumpiness);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.Bumpiness, value);
        }

        /// <summary>Alpha below which a masked surface is cut out. Per material.</summary>
        public float alphaCutoff
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.AlphaCutoff);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.AlphaCutoff, value);
        }

        /// <summary>UV tiling scale. Per material.</summary>
        public Vector2 tiling
        {
            get => internal_m2n_material_instance_get_vector(owner, key, (int)MaterialProperty.Tiling);
            set => internal_m2n_material_instance_set_vector(owner, key, (int)MaterialProperty.Tiling, value);
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
            get => (MaterialAlphaMode)internal_m2n_material_instance_get_mode(owner, key, (int)MaterialProperty.AlphaMode);
            set => internal_m2n_material_instance_set_mode(owner, key, (int)MaterialProperty.AlphaMode, (int)value);
        }

        /// <summary>Which faces the material hides; None draws both sides. Per material.</summary>
        public MaterialCullMode cullMode
        {
            get => (MaterialCullMode)internal_m2n_material_instance_get_mode(owner, key, (int)MaterialProperty.CullMode);
            set => internal_m2n_material_instance_set_mode(owner, key, (int)MaterialProperty.CullMode, (int)value);
        }

        /// <summary>Whether this renderer overrides any property of the shared material.</summary>
        public bool hasOverrides => internal_m2n_material_instance_has_overrides(owner, key);

        /// <summary>
        /// Whether every override is per instance, so this renderer still draws together with the other renderers of
        /// the shared material.
        /// </summary>
        public bool sharesDraw => internal_m2n_material_instance_shares_draw(owner, key);

        /// <summary>Whether this renderer overrides <paramref name="property"/>.</summary>
        public bool IsOverridden(MaterialProperty property)
        {
            return internal_m2n_material_instance_is_overridden(owner, key, (int)property);
        }

        /// <summary>Drops the override of <paramref name="property"/>: it follows the shared material again.</summary>
        public void Revert(MaterialProperty property)
        {
            internal_m2n_material_instance_revert(owner, key, (int)property);
        }

        /// <summary>Drops every override: the renderer draws exactly like the shared material.</summary>
        public void RevertAll()
        {
            internal_m2n_material_instance_revert_all(owner, key);
        }

        /// <summary>
        /// Gets the color this renderer draws with: BaseColor, EmissiveColor or SubsurfaceColor.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
        public Color GetColor(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireColor(property);
            return internal_m2n_material_instance_get_color(owner, key, (int)property);
        }

        /// <summary>
        /// Overrides a color for this renderer: BaseColor, EmissiveColor or SubsurfaceColor. Per instance.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
        public void SetColor(MaterialProperty property, Color value)
        {
            MaterialPropertyTypes.RequireColor(property);
            internal_m2n_material_instance_set_color(owner, key, (int)property, value);
        }

        /// <summary>
        /// Gets the number this renderer draws with: EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
        public float GetFloat(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireFloat(property);
            return internal_m2n_material_instance_get_float(owner, key, (int)property);
        }

        /// <summary>
        /// Overrides a number for this renderer: EmissiveIntensity, Roughness, Metalness, Bumpiness or AlphaCutoff.
        /// Per instance, except AlphaCutoff, which is per material.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
        public void SetFloat(MaterialProperty property, float value)
        {
            MaterialPropertyTypes.RequireFloat(property);
            internal_m2n_material_instance_set_float(owner, key, (int)property, value);
        }

        /// <summary>Gets the vector this renderer draws with: Tiling.</summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
        public Vector2 GetVector(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireVector(property);
            return internal_m2n_material_instance_get_vector(owner, key, (int)property);
        }

        /// <summary>Overrides a vector for this renderer: Tiling. Per material.</summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
        public void SetVector(MaterialProperty property, Vector2 value)
        {
            MaterialPropertyTypes.RequireVector(property);
            internal_m2n_material_instance_set_vector(owner, key, (int)property, value);
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
            var uid = internal_m2n_material_instance_get_texture(owner, key, (int)property);
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
            internal_m2n_material_instance_set_texture(owner, key, (int)property, texture?.uid ?? Guid.Empty);
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_material_instance_create(Guid material);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_release(uint instance);
    }
}
