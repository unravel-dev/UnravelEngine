using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

/// <summary>
/// Loads and resolves assets by key or unique identifier.
/// </summary>
public class Assets
{
    /// <summary>
    /// Loads an asset of type <typeparamref name="T"/> by asset key.
    /// </summary>
    /// <typeparam name="T">The asset type to load.</typeparam>
    /// <param name="key">
    /// The asset key (protocol path or project key). The type-specific extension
    /// may be omitted (e.g. <c>app:/data/key</c> for a <c>.spfb</c> scene).
    /// </param>
    /// <returns>A new asset handle for the resolved asset.</returns>
    public static T GetAsset<T>(string key) where T : Asset<T>, new()
    {
        var asset_uid = internal_m2n_get_asset_by_key(key, typeof(T));
        T asset = new T();
        asset.uid = asset_uid;
        asset.OnResolved();
        return asset;
    }

    /// <summary>
    /// Loads an asset of type <typeparamref name="T"/> by unique identifier.
    /// </summary>
    /// <typeparam name="T">The asset type to load.</typeparam>
    /// <param name="uid">The asset unique identifier.</param>
    /// <returns>A new asset handle for the resolved asset.</returns>
    public static T GetAsset<T>(Guid uid) where T : Asset<T>, new()
    {
        var asset_uid = internal_m2n_get_asset_by_uuid(uid, typeof(T));
        T asset = new T();
        asset.uid = asset_uid;
        asset.OnResolved();
        return asset;
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_get_asset_by_uuid(Guid uid, Type obj);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_get_asset_by_key(string key, Type obj);
}

/// <summary>
/// Texture asset handle.
/// </summary>
public class Texture : Asset<Texture>
{
}

/// <summary>
/// A property of a material, as the typed getters and setters of <see cref="Material"/> and
/// <see cref="MaterialInstance"/> (<see cref="Material.SetColor"/>, <see cref="Material.SetFloat"/>,
/// <see cref="Material.SetVector"/>, <see cref="Material.SetTexture"/>),
/// <see cref="MaterialInstance.IsOverridden"/> and <see cref="MaterialInstance.Revert"/> name it. Each is a
/// parameter or a texture of the Standard material graph ("Base Color", "Color Map"); setting one a graph does not have
/// changes nothing and logs a warning once per material.
/// The alpha and cull modes are the graph's and only read.
/// </summary>
public enum MaterialProperty
{
    /// <summary>Base (albedo) color; its alpha fades a surface whose graph clips by it (a masked or translucent
    /// material, or one made with an alpha below 1).</summary>
    BaseColor,
    /// <summary>Emissive color.</summary>
    EmissiveColor,
    /// <summary>Subsurface color; alpha is the subsurface opacity.</summary>
    SubsurfaceColor,
    /// <summary>Multiplier of the emissive color.</summary>
    EmissiveIntensity,
    /// <summary>Surface roughness; on a specular-workflow material it reads and writes Glossiness as 1 - roughness.
    /// </summary>
    Roughness,
    /// <summary>How metallic the surface is, 0 to 1. A specular-workflow material has none: setting it there changes
    /// nothing and logs a warning.</summary>
    Metallic,
    /// <summary>Normal-map bump intensity.</summary>
    Bumpiness,
    /// <summary>Alpha below which a masked surface is cut out.</summary>
    AlphaCutoff,
    /// <summary>UV tiling scale.</summary>
    Tiling,
    /// <summary>Base color texture.</summary>
    ColorMap,
    /// <summary>Normal map.</summary>
    NormalMap,
    /// <summary>Roughness texture.</summary>
    RoughnessMap,
    /// <summary>Metallic texture.</summary>
    MetallicMap,
    /// <summary>Emissive texture.</summary>
    EmissiveMap,
    /// <summary>Ambient occlusion texture.</summary>
    AoMap,
    /// <summary>How the material treats alpha: the graph's (masked, translucent or opaque); setting it does nothing.</summary>
    AlphaMode,
    /// <summary>Which faces the material hides: the graph's Two Sided and Cull Front Faces settings; setting it does
    /// nothing.</summary>
    CullMode,
}

/// <summary>
/// The value type of each <see cref="MaterialProperty"/>: the typed getters and setters of <see cref="Material"/> and
/// <see cref="MaterialInstance"/> reject a property that holds another type.
/// </summary>
internal static class MaterialPropertyTypes
{
    internal static void RequireColor(MaterialProperty property)
    {
        bool isColor = property == MaterialProperty.BaseColor ||
                       property == MaterialProperty.EmissiveColor ||
                       property == MaterialProperty.SubsurfaceColor;
        Require(isColor, property, "BaseColor, EmissiveColor or SubsurfaceColor");
    }

    internal static void RequireFloat(MaterialProperty property)
    {
        bool isFloat = property == MaterialProperty.EmissiveIntensity ||
                       property == MaterialProperty.Roughness ||
                       property == MaterialProperty.Metallic ||
                       property == MaterialProperty.Bumpiness ||
                       property == MaterialProperty.AlphaCutoff;
        Require(isFloat, property, "EmissiveIntensity, Roughness, Metallic, Bumpiness or AlphaCutoff");
    }

    internal static void RequireVector(MaterialProperty property)
    {
        Require(property == MaterialProperty.Tiling, property, "Tiling");
    }

    internal static void RequireTexture(MaterialProperty property)
    {
        bool isTexture = property == MaterialProperty.ColorMap ||
                         property == MaterialProperty.NormalMap ||
                         property == MaterialProperty.RoughnessMap ||
                         property == MaterialProperty.MetallicMap ||
                         property == MaterialProperty.EmissiveMap ||
                         property == MaterialProperty.AoMap;
        Require(isTexture, property, "ColorMap, NormalMap, RoughnessMap, MetallicMap, EmissiveMap or AoMap");
    }

    private static void Require(bool isOfType, MaterialProperty property, string expected)
    {
        if (!isOfType)
        {
            throw new ArgumentException($"{property} is not one of {expected}.", nameof(property));
        }
    }
}

/// <summary>
/// How a material treats alpha.
/// </summary>
public enum MaterialAlphaMode
{
    /// <summary>No alpha clip; casts solid shadows.</summary>
    Opaque,
    /// <summary>Hard cutoff at the alpha cutoff; casts cutout shadows.</summary>
    Mask,
    /// <summary>See-through (any <see cref="MaterialBlendMode"/> but Opaque); does not cast shadows.</summary>
    Blend,
}

/// <summary>
/// How a material's surface covers what is behind it: its graph's Blend Mode (C++ material_blend_mode).
/// </summary>
public enum MaterialBlendMode
{
    /// <summary>Covers what is behind it; an Opacity Mask cuts it out.</summary>
    Opaque,
    /// <summary>See-through by a dither of its Opacity, lit and shadowed like opaque surfaces.</summary>
    Dithered,
    /// <summary>Blended over the scene by its Opacity after the lighting: glass, water, fades.</summary>
    Translucent,
    /// <summary>Adds its colour as it is while its Opacity covers what is behind: light that also occludes.</summary>
    Premultiplied,
    /// <summary>Adds its light to the scene, scaled by its Opacity.</summary>
    Additive,
    /// <summary>Multiplies the scene behind it by its colour.</summary>
    Modulate,
}

/// <summary>
/// Which light a material's surface takes: its graph's Shading Model (C++ material_shading_model).
/// </summary>
public enum MaterialShadingModel
{
    /// <summary>Takes the scene's light.</summary>
    DefaultLit,
    /// <summary>Shows its Emissive alone.</summary>
    Unlit,
}

/// <summary>
/// Which triangle faces a material hides.
/// </summary>
public enum MaterialCullMode
{
    /// <summary>Both sides are drawn (double sided).</summary>
    None,
    /// <summary>Clockwise faces are hidden.</summary>
    Clockwise,
    /// <summary>Counter-clockwise faces are hidden (the default).</summary>
    CounterClockwise,
}

/// <summary>
/// A shared material: a material asset that renderers use.
/// </summary>
/// <remarks>
/// Setting a property changes every renderer that uses the material, except the values a renderer's
/// <see cref="MaterialInstance"/> overrides. To change one renderer only, use its material instance
/// (<see cref="ModelComponent.GetMaterialInstance"/>). In the editor, what a script changes on a shared material
/// lasts until play stops.
/// </remarks>
public class Material : Asset<Material>
{
    /// <summary>Base (albedo) color; its alpha fades a surface whose graph clips by it (a masked or translucent
    /// material, or one made with an alpha below 1).</summary>
    public Color baseColor
    {
        get => internal_m2n_material_get_color(uid, (int)MaterialProperty.BaseColor);
        set => internal_m2n_material_set_color(uid, (int)MaterialProperty.BaseColor, value);
    }

    /// <summary>Emissive color.</summary>
    public Color emissiveColor
    {
        get => internal_m2n_material_get_color(uid, (int)MaterialProperty.EmissiveColor);
        set => internal_m2n_material_set_color(uid, (int)MaterialProperty.EmissiveColor, value);
    }

    /// <summary>Subsurface color; alpha is the subsurface opacity.</summary>
    public Color subsurfaceColor
    {
        get => internal_m2n_material_get_color(uid, (int)MaterialProperty.SubsurfaceColor);
        set => internal_m2n_material_set_color(uid, (int)MaterialProperty.SubsurfaceColor, value);
    }

    /// <summary>Multiplier of the emissive color: emitted light = emissiveColor x emissiveIntensity.</summary>
    public float emissiveIntensity
    {
        get => internal_m2n_material_get_float(uid, (int)MaterialProperty.EmissiveIntensity);
        set => internal_m2n_material_set_float(uid, (int)MaterialProperty.EmissiveIntensity, value);
    }

    /// <summary>Surface roughness; on a specular-workflow material it reads and writes Glossiness as 1 - roughness.
    /// </summary>
    public float roughness
    {
        get => internal_m2n_material_get_float(uid, (int)MaterialProperty.Roughness);
        set => internal_m2n_material_set_float(uid, (int)MaterialProperty.Roughness, value);
    }

    /// <summary>How metallic the surface is, 0 to 1. A specular-workflow material has none: setting it there changes
    /// nothing and logs a warning.</summary>
    public float metallic
    {
        get => internal_m2n_material_get_float(uid, (int)MaterialProperty.Metallic);
        set => internal_m2n_material_set_float(uid, (int)MaterialProperty.Metallic, value);
    }

    /// <summary>Normal-map bump intensity.</summary>
    public float bumpiness
    {
        get => internal_m2n_material_get_float(uid, (int)MaterialProperty.Bumpiness);
        set => internal_m2n_material_set_float(uid, (int)MaterialProperty.Bumpiness, value);
    }

    /// <summary>Alpha below which a masked surface is cut out.</summary>
    public float alphaCutoff
    {
        get => internal_m2n_material_get_float(uid, (int)MaterialProperty.AlphaCutoff);
        set => internal_m2n_material_set_float(uid, (int)MaterialProperty.AlphaCutoff, value);
    }

    /// <summary>UV tiling scale.</summary>
    public Vector2 tiling
    {
        get => internal_m2n_material_get_vector(uid, (int)MaterialProperty.Tiling);
        set => internal_m2n_material_set_vector(uid, (int)MaterialProperty.Tiling, value);
    }

    /// <summary>Base color texture, or null for the engine's default map.</summary>
    public Texture colorMap
    {
        get => GetTexture(MaterialProperty.ColorMap);
        set => SetTexture(MaterialProperty.ColorMap, value);
    }

    /// <summary>Normal map, or null for the engine's default map.</summary>
    public Texture normalMap
    {
        get => GetTexture(MaterialProperty.NormalMap);
        set => SetTexture(MaterialProperty.NormalMap, value);
    }

    /// <summary>Roughness texture, or null for the engine's default map.</summary>
    public Texture roughnessMap
    {
        get => GetTexture(MaterialProperty.RoughnessMap);
        set => SetTexture(MaterialProperty.RoughnessMap, value);
    }

    /// <summary>Metallic texture, or null for the engine's default map.</summary>
    public Texture metallicMap
    {
        get => GetTexture(MaterialProperty.MetallicMap);
        set => SetTexture(MaterialProperty.MetallicMap, value);
    }

    /// <summary>Emissive texture, or null for the engine's default map.</summary>
    public Texture emissiveMap
    {
        get => GetTexture(MaterialProperty.EmissiveMap);
        set => SetTexture(MaterialProperty.EmissiveMap, value);
    }

    /// <summary>Ambient occlusion texture, or null for the engine's default map.</summary>
    public Texture aoMap
    {
        get => GetTexture(MaterialProperty.AoMap);
        set => SetTexture(MaterialProperty.AoMap, value);
    }

    /// <summary>How the material treats alpha: the graph's; setting it does nothing (a warning is logged).</summary>
    public MaterialAlphaMode alphaMode
    {
        get => (MaterialAlphaMode)internal_m2n_material_get_mode(uid, (int)MaterialProperty.AlphaMode);
        set => internal_m2n_material_set_mode(uid, (int)MaterialProperty.AlphaMode, (int)value);
    }

    /// <summary>How the material's surface covers what is behind it: its graph's Blend Mode. Read only: the Material
    /// Graph sets it.</summary>
    public MaterialBlendMode blendMode => (MaterialBlendMode)internal_m2n_material_get_blend_mode(uid);

    /// <summary>Which light the material's surface takes: its graph's Shading Model. Read only: the Material Graph sets
    /// it.</summary>
    public MaterialShadingModel shadingModel => (MaterialShadingModel)internal_m2n_material_get_shading_model(uid);

    /// <summary>Which faces the material hides (None draws both sides): the graph's Two Sided and Cull Front Faces
    /// settings; setting it does nothing (a warning is logged).</summary>
    public MaterialCullMode cullMode
    {
        get => (MaterialCullMode)internal_m2n_material_get_mode(uid, (int)MaterialProperty.CullMode);
        set => internal_m2n_material_set_mode(uid, (int)MaterialProperty.CullMode, (int)value);
    }

    /// <summary>Gets a color property: BaseColor, EmissiveColor or SubsurfaceColor.</summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
    public Color GetColor(MaterialProperty property)
    {
        MaterialPropertyTypes.RequireColor(property);
        return internal_m2n_material_get_color(uid, (int)property);
    }

    /// <summary>
    /// Sets a color property for every renderer that uses the material: BaseColor, EmissiveColor or SubsurfaceColor.
    /// </summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a color property.</exception>
    public void SetColor(MaterialProperty property, Color value)
    {
        MaterialPropertyTypes.RequireColor(property);
        internal_m2n_material_set_color(uid, (int)property, value);
    }

    /// <summary>Gets a number property: EmissiveIntensity, Roughness, Metallic, Bumpiness or AlphaCutoff.</summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
    public float GetFloat(MaterialProperty property)
    {
        MaterialPropertyTypes.RequireFloat(property);
        return internal_m2n_material_get_float(uid, (int)property);
    }

    /// <summary>
    /// Sets a number property for every renderer that uses the material: EmissiveIntensity, Roughness, Metallic,
    /// Bumpiness or AlphaCutoff.
    /// </summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a number property.</exception>
    public void SetFloat(MaterialProperty property, float value)
    {
        MaterialPropertyTypes.RequireFloat(property);
        internal_m2n_material_set_float(uid, (int)property, value);
    }

    /// <summary>Gets a vector property: Tiling.</summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
    public Vector2 GetVector(MaterialProperty property)
    {
        MaterialPropertyTypes.RequireVector(property);
        return internal_m2n_material_get_vector(uid, (int)property);
    }

    /// <summary>Sets a vector property for every renderer that uses the material: Tiling.</summary>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a vector property.</exception>
    public void SetVector(MaterialProperty property, Vector2 value)
    {
        MaterialPropertyTypes.RequireVector(property);
        internal_m2n_material_set_vector(uid, (int)property, value);
    }

    /// <summary>
    /// Gets a texture property: ColorMap, NormalMap, RoughnessMap, MetallicMap, EmissiveMap or AoMap.
    /// </summary>
    /// <returns>The texture, or null when the material uses the engine's default map.</returns>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a texture property.</exception>
    public Texture GetTexture(MaterialProperty property)
    {
        MaterialPropertyTypes.RequireTexture(property);
        var texture = internal_m2n_material_get_texture(uid, (int)property);
        return texture == Guid.Empty ? null : new Texture { uid = texture };
    }

    /// <summary>
    /// Sets a texture property for every renderer that uses the material: ColorMap, NormalMap, RoughnessMap,
    /// MetallicMap, EmissiveMap or AoMap.
    /// </summary>
    /// <param name="property">The texture property.</param>
    /// <param name="texture">The texture, or null for the engine's default map.</param>
    /// <exception cref="ArgumentException"><paramref name="property"/> is not a texture property.</exception>
    public void SetTexture(MaterialProperty property, Texture texture)
    {
        MaterialPropertyTypes.RequireTexture(property);
        internal_m2n_material_set_texture(uid, (int)property, texture?.uid ?? Guid.Empty);
    }

    /// <summary>Whether the material is a material graph with a parameter named <paramref name="name"/>.</summary>
    public bool HasParameter(string name)
    {
        return internal_m2n_material_has_parameter(uid, name);
    }

    /// <summary>Gets the number a material graph's Scalar Parameter takes.</summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public float GetFloat(string name)
    {
        return GetVector(name).x;
    }

    /// <summary>
    /// Sets a material graph's Scalar Parameter for every renderer that uses the material; restored when play stops.
    /// </summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public void SetFloat(string name, float value)
    {
        SetVector(name, new Vector4(value, 0.0f, 0.0f, 0.0f));
    }

    /// <summary>Gets the vector a material graph's Vector or Color Parameter takes.</summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public Vector4 GetVector(string name)
    {
        if (!HasParameter(name))
        {
            throw new ArgumentException($"The material is not a material graph with a parameter named '{name}'.", nameof(name));
        }
        return internal_m2n_material_get_parameter(uid, name);
    }

    /// <summary>
    /// Sets a material graph's Vector or Color Parameter for every renderer that uses the material; restored when play
    /// stops.
    /// </summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public void SetVector(string name, Vector4 value)
    {
        if (!internal_m2n_material_set_parameter(uid, name, value))
        {
            throw new ArgumentException($"The material is not a material graph with a parameter named '{name}'.", nameof(name));
        }
    }

    /// <summary>Gets the color a material graph's Color Parameter takes.</summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public Color GetColor(string name)
    {
        var value = GetVector(name);
        return new Color(value.x, value.y, value.z, value.w);
    }

    /// <summary>
    /// Sets a material graph's Color Parameter for every renderer that uses the material; restored when play stops.
    /// </summary>
    /// <exception cref="ArgumentException">The material has no parameter named <paramref name="name"/>.</exception>
    public void SetColor(string name, Color value)
    {
        SetVector(name, new Vector4(value.r, value.g, value.b, value.a));
    }

    /// <summary>Gets the texture a material graph's texture node, named by its id, samples.</summary>
    /// <returns>The texture, or null when the node samples the engine's default texture.</returns>
    /// <exception cref="ArgumentException">The material has no texture node <paramref name="textureNode"/>.</exception>
    public Texture GetTexture(string textureNode)
    {
        if (!internal_m2n_material_has_texture_node(uid, textureNode))
        {
            throw new ArgumentException($"The material is not a material graph with a texture node '{textureNode}'.", nameof(textureNode));
        }
        var texture = internal_m2n_material_get_texture_by_name(uid, textureNode);
        return texture == Guid.Empty ? null : new Texture { uid = texture };
    }

    /// <summary>
    /// Sets the texture of a material graph's texture node, named by its id, for every renderer that uses the material;
    /// restored when play stops.
    /// </summary>
    /// <param name="textureNode">The texture node's id.</param>
    /// <param name="texture">The texture, or null for the engine's default texture.</param>
    /// <exception cref="ArgumentException">The material has no texture node <paramref name="textureNode"/>.</exception>
    public void SetTexture(string textureNode, Texture texture)
    {
        if (!internal_m2n_material_set_texture_by_name(uid, textureNode, texture?.uid ?? Guid.Empty))
        {
            throw new ArgumentException($"The material is not a material graph with a texture node '{textureNode}'.", nameof(textureNode));
        }
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_material_has_parameter(Guid uid, string name);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Vector4 internal_m2n_material_get_parameter(Guid uid, string name);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_material_set_parameter(Guid uid, string name, Vector4 value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_material_has_texture_node(Guid uid, string name);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_material_get_texture_by_name(Guid uid, string name);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern bool internal_m2n_material_set_texture_by_name(Guid uid, string name, Guid texture);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_material_set_color(Guid uid, int property, Color value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Color internal_m2n_material_get_color(Guid uid, int property);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_material_set_float(Guid uid, int property, float value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_material_get_float(Guid uid, int property);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_material_set_vector(Guid uid, int property, Vector2 value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Vector2 internal_m2n_material_get_vector(Guid uid, int property);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_material_set_texture(Guid uid, int property, Guid texture);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_material_get_texture(Guid uid, int property);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_material_set_mode(Guid uid, int property, int value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int internal_m2n_material_get_mode(Guid uid, int property);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int internal_m2n_material_get_blend_mode(Guid uid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int internal_m2n_material_get_shading_model(Guid uid);
}

/// <summary>
/// Mesh asset handle.
/// </summary>
public class Mesh : Asset<Mesh>
{
}

/// <summary>
/// Animation clip asset handle.
/// </summary>
public class AnimationClip : Asset<AnimationClip>
{
    /// <summary>
    /// Gets the length of the animation clip in seconds.
    /// </summary>
    public float length
    {
        get
        {
            return internal_m2n_animation_clip_get_length(uid);
        }
    }

    /// <summary>
    /// Gets the name of the animation clip.
    /// </summary>
    public string name
    {
        get
        {
            return internal_m2n_animation_clip_get_name(uid);
        }
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_animation_clip_get_length(Guid uid);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern string internal_m2n_animation_clip_get_name(Guid uid);
}

/// <summary>
/// Animation controller asset handle: a state machine of animation states, transitions and
/// parameters. Assign it to <see cref="AnimationComponent.controller"/> and drive it with
/// <see cref="AnimationComponent.SetFloat"/>, <see cref="AnimationComponent.SetTrigger"/> and friends.
/// </summary>
public class AnimationController : Asset<AnimationController>
{
}

/// <summary>
/// Physics material asset handle.
/// </summary>
public class PhysicsMaterial : Asset<PhysicsMaterial>
{
}

/// <summary>
/// UI visual-tree (RML/layout) asset handle.
/// </summary>
public class VisualTree : Asset<VisualTree>
{
}

/// <summary>
/// UI stylesheet asset handle.
/// </summary>
public class StyleSheet : Asset<StyleSheet>
{
}

/// <summary>
/// Font asset handle.
/// </summary>
public class Font : Asset<Font>
{
}

/// <summary>
/// Audio clip asset handle.
/// </summary>
public class AudioClip : Asset<AudioClip>
{
    /// <summary>
    /// Gets the length of the audio clip in seconds.
    /// </summary>
    public float length
    {
        get
        {
            return internal_m2n_audio_clip_get_length(uid);
        }
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_clip_get_length(Guid uid);
}

}
