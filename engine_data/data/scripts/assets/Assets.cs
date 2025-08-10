using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ace
{
namespace Core
{

public class Assets
{
   
    public static T GetAsset<T>(string key) where T : Asset<T>, new()
    {
        var asset_uid = internal_m2n_get_asset_by_key(key, typeof(T));
        T asset = new T();
        asset.uid = asset_uid;
        return asset;
    }

    public static T GetAsset<T>(Guid uid) where T : Asset<T>, new()
    {
        var asset_uid = internal_m2n_get_asset_by_uuid(uid, typeof(T));
        T asset = new T();
        asset.uid = asset_uid;
        return asset;
    }

    // Specialized overload for Material
    public static Material GetAsset(string key)
    {
        var asset_uid = internal_m2n_get_asset_by_key(key, typeof(Material));
        Material material = new Material();
        material.uid = asset_uid;
        material.SetProperties(internal_m2n_get_material_properties(material.uid));
        return material;
    }

    // Specialized overload for Material (by Guid)
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

public class Texture : Asset<Texture>
{
}

[StructLayout(LayoutKind.Sequential)]
public struct MaterialProperties
{
    public bool valid;

    public Color baseColor;

    public Color emissiveColor;

    public Vector2 tiling;

    public float roughness;

    public float metalness;

    public float bumpiness;
}


public class Material : Asset<Material>
{
    public Material()
    {

    }
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

    public Color color = Color.white;

    public Color emissiveColor = Color.black;

    public Vector2 tiling = Vector2.one;

    public float roughness = 0.0f;

    public float metalness = 0.0f;

    public float bumpiness = 1.0f;
}

public class Mesh : Asset<Mesh>
{
}

public class AnimationClip : Asset<AnimationClip>
{
}


public class PhysicsMaterial : Asset<PhysicsMaterial>
{
}


public class Font : Asset<Font>
{
}
public class AudioClip : Asset<AudioClip>
{
    //
    // Summary:
    //     The length of the audio clip in seconds. (Read Only)
    public float length
    {
        get
        {
            return internal_m2n_audio_clip_get_length(uid);
        }
    }

    //
    // Summary:
    //     The length of the audio clip in samples. (Read Only)
    // public extern int samples
    // {
    //     [MethodImpl(MethodImplOptions.InternalCall)]
    //     get;
    // }

    // //
    // // Summary:
    // //     The number of channels in the audio clip. (Read Only)
    // public extern int channels
    // {
    //     [MethodImpl(MethodImplOptions.InternalCall)]
    //     get;
    // }

    // //
    // // Summary:
    // //     The sample frequency of the clip in Hertz. (Read Only)
    // public extern int frequency
    // {
    //     [MethodImpl(MethodImplOptions.InternalCall)]
    //     get;
    // }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern float internal_m2n_audio_clip_get_length(Guid uid);
}

}

}


