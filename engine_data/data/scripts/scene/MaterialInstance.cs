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
    ///
    /// A material graph's parameters and texture nodes are set by name (<see cref="SetFloat(string, float)"/>,
    /// <see cref="SetTexture(string, Texture)"/>): a parameter among the graph's first four registers keeps the
    /// renderer in the shared draw; a texture node, or any parameter of a graph whose Opacity Mask clips, does not.
    /// </remarks>
    public sealed class MaterialInstance : IDisposable
    {
        private readonly Entity owner;
        /// <summary>What the native calls know the instance by: its model slot, or for an instance of no renderer its
        /// id (0 once released).</summary>
        private uint key;
        private readonly Material material;

        /// <summary>The slot a decal's instance passes to the native calls: a decal has one instance, of its material.
        /// Mirrors DECAL_MATERIAL_INSTANCE_SLOT in model_component_bindings.cpp.</summary>
        internal const uint DecalSlot = uint.MaxValue;

        /// <summary>Whether the instance is a post-process material entry's (PostProcessMaterialComponent.GetInstance).</summary>
        private bool isPostProcessEntry => slot >= PostProcessMaterialComponent.InstanceSlotBase && slot != DecalSlot;

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

        /// <summary>The model slot this instance belongs to; 0 for an instance of no renderer, the largest value for a
        /// decal's, and from 0xFF000000 on for a post-process material entry's.</summary>
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
                if (slot == DecalSlot)
                {
                    var decalMaterial = DecalComponent.internal_m2n_decal_get_material(owner);
                    return decalMaterial == Guid.Empty ? null : new Material { uid = decalMaterial };
                }
                if (isPostProcessEntry)
                {
                    var entryMaterial = PostProcessMaterialComponent.internal_m2n_post_materials_get_material(
                        owner, (int)(slot - PostProcessMaterialComponent.InstanceSlotBase));
                    return entryMaterial == Guid.Empty ? null : new Material { uid = entryMaterial };
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

        /// <summary>Base (albedo) color; its alpha fades a surface whose graph clips by it (a masked or translucent
        /// material, or one made with an alpha below 1). Per instance.</summary>
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

        /// <summary>Surface roughness; on a specular-workflow material it reads and writes Glossiness as 1 -
        /// roughness. Per instance.</summary>
        public float roughness
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.Roughness);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.Roughness, value);
        }

        /// <summary>How metallic the surface is, 0 to 1. A specular-workflow material has none: setting it there
        /// changes nothing and logs a warning. Per instance.</summary>
        public float metallic
        {
            get => internal_m2n_material_instance_get_float(owner, key, (int)MaterialProperty.Metallic);
            set => internal_m2n_material_instance_set_float(owner, key, (int)MaterialProperty.Metallic, value);
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

        /// <summary>Metallic texture, or null for the engine's default map. Per material.</summary>
        public Texture metallicMap
        {
            get => GetTexture(MaterialProperty.MetallicMap);
            set => SetTexture(MaterialProperty.MetallicMap, value);
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

        /// <summary>How the material treats alpha: the graph's; setting it does nothing (a warning is logged).</summary>
        public MaterialAlphaMode alphaMode
        {
            get => (MaterialAlphaMode)internal_m2n_material_instance_get_mode(owner, key, (int)MaterialProperty.AlphaMode);
            set => internal_m2n_material_instance_set_mode(owner, key, (int)MaterialProperty.AlphaMode, (int)value);
        }

        /// <summary>Which faces the material hides (None draws both sides): the graph's Two Sided and Cull Front
        /// Faces settings; setting it does nothing (a warning is logged).</summary>
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
        /// Gets the number this renderer draws with: EmissiveIntensity, Roughness, Metallic, Bumpiness or AlphaCutoff.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
        public float GetFloat(MaterialProperty property)
        {
            MaterialPropertyTypes.RequireFloat(property);
            return internal_m2n_material_instance_get_float(owner, key, (int)property);
        }

        /// <summary>
        /// Overrides a number for this renderer: EmissiveIntensity, Roughness, Metallic, Bumpiness or AlphaCutoff.
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
        /// Gets the texture this renderer draws with: ColorMap, NormalMap, RoughnessMap, MetallicMap, EmissiveMap or
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
        /// Overrides a texture for this renderer: ColorMap, NormalMap, RoughnessMap, MetallicMap, EmissiveMap or
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

        /// <summary>Whether the shared material is a material graph with a parameter named <paramref name="name"/>.</summary>
        public bool HasParameter(string name)
        {
            return internal_m2n_material_instance_has_parameter(owner, key, name);
        }

        /// <summary>Gets the number this renderer gives a material graph's Scalar Parameter.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public float GetFloat(string name)
        {
            return GetVector(name).x;
        }

        /// <summary>Overrides a material graph's Scalar Parameter for this renderer.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public void SetFloat(string name, float value)
        {
            SetVector(name, new Vector4(value, 0.0f, 0.0f, 0.0f));
        }

        /// <summary>Gets the vector this renderer gives a material graph's Vector or Color Parameter.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public Vector4 GetVector(string name)
        {
            RequireParameter(name);
            return internal_m2n_material_instance_get_parameter(owner, key, name);
        }

        /// <summary>Overrides a material graph's Vector or Color Parameter for this renderer.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public void SetVector(string name, Vector4 value)
        {
            if (!internal_m2n_material_instance_set_parameter(owner, key, name, value))
            {
                throw new ArgumentException($"The shared material is not a material graph with a parameter named '{name}'.", nameof(name));
            }
        }

        /// <summary>Gets the color this renderer gives a material graph's Color Parameter.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public Color GetColor(string name)
        {
            var value = GetVector(name);
            return new Color(value.x, value.y, value.z, value.w);
        }

        /// <summary>Overrides a material graph's Color Parameter for this renderer.</summary>
        /// <exception cref="ArgumentException">The shared material has no parameter named <paramref name="name"/>.</exception>
        public void SetColor(string name, Color value)
        {
            SetVector(name, new Vector4(value.r, value.g, value.b, value.a));
        }

        /// <summary>
        /// Gets the texture this renderer samples for a material graph's texture node, named by its id.
        /// </summary>
        /// <returns>The texture, or null when the node samples the engine's default texture.</returns>
        /// <exception cref="ArgumentException">The shared material has no texture node <paramref name="textureNode"/>.</exception>
        public Texture GetTexture(string textureNode)
        {
            if (!internal_m2n_material_instance_has_texture_node(owner, key, textureNode))
            {
                throw new ArgumentException($"The shared material is not a material graph with a texture node '{textureNode}'.", nameof(textureNode));
            }
            var uid = internal_m2n_material_instance_get_texture_by_name(owner, key, textureNode);
            return uid == Guid.Empty ? null : new Texture { uid = uid };
        }

        /// <summary>
        /// Overrides the texture of a material graph's texture node, named by its id, for this renderer. Per material.
        /// </summary>
        /// <param name="textureNode">The texture node's id.</param>
        /// <param name="texture">The texture, or null for the engine's default texture.</param>
        /// <exception cref="ArgumentException">The shared material has no texture node <paramref name="textureNode"/>.</exception>
        public void SetTexture(string textureNode, Texture texture)
        {
            if (!internal_m2n_material_instance_set_texture_by_name(owner, key, textureNode, texture?.uid ?? Guid.Empty))
            {
                throw new ArgumentException($"The shared material is not a material graph with a texture node '{textureNode}'.", nameof(textureNode));
            }
        }

        /// <summary>Whether this renderer overrides the material graph's parameter or texture node <paramref name="name"/>.</summary>
        public bool IsOverridden(string name)
        {
            return internal_m2n_material_instance_is_name_overridden(owner, key, name);
        }

        /// <summary>
        /// Drops the override of the material graph's parameter or texture node <paramref name="name"/>: it follows the
        /// shared material again.
        /// </summary>
        public void Revert(string name)
        {
            internal_m2n_material_instance_revert_name(owner, key, name);
        }

        private void RequireParameter(string name)
        {
            if (!HasParameter(name))
            {
                throw new ArgumentException($"The shared material is not a material graph with a parameter named '{name}'.", nameof(name));
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_has_parameter(Entity eid, uint slot, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector4 internal_m2n_material_instance_get_parameter(Entity eid, uint slot, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_set_parameter(Entity eid, uint slot, string name, Vector4 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_has_texture_node(Entity eid, uint slot, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_material_instance_get_texture_by_name(Entity eid, uint slot, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_set_texture_by_name(Entity eid, uint slot, string name, Guid texture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_material_instance_is_name_overridden(Entity eid, uint slot, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_material_instance_revert_name(Entity eid, uint slot, string name);

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
