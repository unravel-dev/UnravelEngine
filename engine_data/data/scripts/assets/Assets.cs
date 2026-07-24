using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
        return asset;
    }

    /// <summary>
    /// Loads a <see cref="Material"/> by asset key and populates its properties.
    /// </summary>
    /// <param name="key">The material asset key (extension optional).</param>
    /// <returns>The loaded material.</returns>
    public static Material GetAsset(string key)
    {
        var asset_uid = internal_m2n_get_asset_by_key(key, typeof(Material));
        Material material = new Material();
        material.uid = asset_uid;
        material.SetProperties(internal_m2n_get_material_properties(material.uid));
        return material;
    }

    /// <summary>
    /// Loads a <see cref="Material"/> by unique identifier and populates its properties.
    /// </summary>
    /// <param name="uid">The material unique identifier.</param>
    /// <returns>The loaded material.</returns>
    public static Material GetAsset(Guid uid)
    {
        var asset_uid = internal_m2n_get_asset_by_uuid(uid, typeof(Material));
        Material material = new Material();
        material.uid = asset_uid;
        material.SetProperties(internal_m2n_get_material_properties(material.uid));
        return material;
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_get_asset_by_uuid(Guid uid, Type obj);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Guid internal_m2n_get_asset_by_key(string key, Type obj);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern MaterialProperties internal_m2n_get_material_properties(Guid uid);
}

/// <summary>
/// Texture asset handle.
/// </summary>
public class Texture : Asset<Texture>
{
}

/// <summary>
/// Blittable material property block exchanged with native code.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct MaterialProperties
{
    /// <summary>
    /// Whether this property block contains valid data.
    /// </summary>
    public bool valid;

    /// <summary>
    /// Base (albedo) color.
    /// </summary>
    public Color baseColor;

    /// <summary>
    /// Emissive color.
    /// </summary>
    public Color emissiveColor;

    /// <summary>
    /// UV tiling scale.
    /// </summary>
    public Vector2 tiling;

    /// <summary>
    /// Surface roughness.
    /// </summary>
    public float roughness;

    /// <summary>
    /// Surface metalness.
    /// </summary>
    public float metalness;

    /// <summary>
    /// Normal-map bump intensity.
    /// </summary>
    public float bumpiness;
}

/// <summary>
/// Material asset with editable shading properties.
/// </summary>
public class Material : Asset<Material>
{
    /// <summary>
    /// Creates an empty material handle.
    /// </summary>
    public Material()
    {
    }

    /// <summary>
    /// Creates a material copy with the same properties as <paramref name="rhs"/>.
    /// </summary>
    /// <param name="rhs">The material to copy properties from.</param>
    public Material(Material rhs)
    {
        SetProperties(rhs.GetProperties());
    }

    internal void SetProperties(MaterialProperties props)
    {
        if(!props.valid)
        {
            return;
        }
        this.color = props.baseColor;
        this.emissiveColor = props.emissiveColor;
        this.tiling = props.tiling;
        this.roughness = props.roughness;
        this.metalness = props.metalness;
        this.bumpiness = props.bumpiness;
    }

    internal MaterialProperties GetProperties()
    {
        MaterialProperties props;
        props.baseColor = this.color;
        props.emissiveColor = this.emissiveColor;
        props.tiling = this.tiling;
        props.roughness = this.roughness;
        props.metalness = this.metalness;
        props.bumpiness = this.bumpiness;
        props.valid = true;
        return props;
    }

    /// <summary>
    /// Base (albedo) color.
    /// </summary>
    public Color color = Color.white;

    /// <summary>
    /// Emissive color.
    /// </summary>
    public Color emissiveColor = Color.black;

    /// <summary>
    /// UV tiling scale.
    /// </summary>
    public Vector2 tiling = Vector2.one;

    /// <summary>
    /// Surface roughness.
    /// </summary>
    public float roughness = 0.0f;

    /// <summary>
    /// Surface metalness.
    /// </summary>
    public float metalness = 0.0f;

    /// <summary>
    /// Normal-map bump intensity.
    /// </summary>
    public float bumpiness = 1.0f;
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
