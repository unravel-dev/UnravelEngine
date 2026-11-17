using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A decal: a box that lays a Decal material onto the surfaces inside it, projecting along the entity's forward
    /// axis. Lighting, shadows, reflections and ambient occlusion treat the decal as the surface it lies on.
    /// </summary>
    /// <remarks>
    /// Per-decal values (a tint, an atlas frame) go through <see cref="materialInstance"/>; decals of one material
    /// still draw together while their overrides stay among its first parameters. A renderer whose
    /// <see cref="ModelComponent.receiveDecals"/> is off keeps none. <see cref="Decals.Spawn"/> places a decal on a
    /// surface in one call.
    /// </remarks>
    public class DecalComponent : Component
    {
        private MaterialInstance instance;

        /// <summary>The Decal material the box projects. A material of another domain draws nothing.</summary>
        public Material material
        {
            get
            {
                var uid = internal_m2n_decal_get_material(owner);
                return uid == Guid.Empty ? null : new Material { uid = uid };
            }
            set => internal_m2n_decal_set_material(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>This decal's overrides of its material: set a parameter or texture here and only this decal
        /// changes.</summary>
        public MaterialInstance materialInstance => instance ??= new MaterialInstance(owner, MaterialInstance.DecalSlot);

        /// <summary>Width and height of the projected face, in metres before the entity's scale.</summary>
        public Vector2 size
        {
            get => internal_m2n_decal_get_size(owner);
            set => internal_m2n_decal_set_size(owner, value);
        }

        /// <summary>How deep the box reaches along the projection, in metres before the entity's scale.</summary>
        public float depth
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.Depth);
            set => internal_m2n_decal_set_float(owner, (int)Field.Depth, value);
        }

        /// <summary>Scales the material's Opacity: 0 hides the decal. Animate it to fade a decal.</summary>
        public float opacity
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.Opacity);
            set => internal_m2n_decal_set_float(owner, (int)Field.Opacity, value);
        }

        /// <summary>Where decals overlap, a higher order draws over a lower one.</summary>
        public int sortOrder
        {
            get => internal_m2n_decal_get_sort_order(owner);
            set => internal_m2n_decal_set_sort_order(owner, value);
        }

        /// <summary>The layers of the models the decal lays onto: a model whose entity is on none of them keeps none of
        /// it.</summary>
        public LayerMask receiverLayers
        {
            get => internal_m2n_decal_get_receiver_layers(owner);
            set => internal_m2n_decal_set_receiver_layers(owner, value);
        }

        /// <summary>Degrees between a surface and the direction back toward the projector where the decal starts to
        /// fade (x) and is gone (y).</summary>
        public Vector2 angleFade
        {
            get => internal_m2n_decal_get_angle_fade(owner);
            set => internal_m2n_decal_set_angle_fade(owner, value);
        }

        /// <summary>Below this share of the view's height the decal fades out and is not drawn.</summary>
        public float fadeScreenSize
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.FadeScreenSize);
            set => internal_m2n_decal_set_float(owner, (int)Field.FadeScreenSize, value);
        }

        /// <summary>Beyond this distance from the camera, in metres, the decal is not drawn; 0 sets no limit.</summary>
        public float maxDistance
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.MaxDistance);
            set => internal_m2n_decal_set_float(owner, (int)Field.MaxDistance, value);
        }

        /// <summary>Seconds the decal lasts after it has faded in, before it fades out; 0 keeps it.</summary>
        public float lifetime
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.Lifetime);
            set => internal_m2n_decal_set_float(owner, (int)Field.Lifetime, value);
        }

        /// <summary>Seconds the decal takes to appear.</summary>
        public float fadeIn
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.FadeIn);
            set => internal_m2n_decal_set_float(owner, (int)Field.FadeIn, value);
        }

        /// <summary>Seconds the decal takes to disappear once its <see cref="lifetime"/> is over.</summary>
        public float fadeOut
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.FadeOut);
            set => internal_m2n_decal_set_float(owner, (int)Field.FadeOut, value);
        }

        /// <summary>Seconds since the decal appeared, which its fades run on; set 0 to start them again.</summary>
        public float age
        {
            get => internal_m2n_decal_get_float(owner, (int)Field.Age);
            set => internal_m2n_decal_set_float(owner, (int)Field.Age, value);
        }

        /// <summary>Whether the entity is destroyed once the decal has faded out.</summary>
        public bool removeAfterFade
        {
            get => internal_m2n_decal_get_remove_after_fade(owner);
            set => internal_m2n_decal_set_remove_after_fade(owner, value);
        }

        /// <summary>The float fields the native calls read and write by index (decal_float_field).</summary>
        private enum Field
        {
            Depth = 0,
            Opacity = 1,
            FadeScreenSize = 2,
            MaxDistance = 3,
            Lifetime = 4,
            FadeIn = 5,
            FadeOut = 6,
            Age = 7,
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Guid internal_m2n_decal_get_material(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_material(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_decal_get_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_size(Entity eid, Vector2 size);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_decal_get_angle_fade(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_angle_fade(Entity eid, Vector2 angles);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_decal_get_float(Entity eid, int field);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_float(Entity eid, int field, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_decal_get_sort_order(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_sort_order(Entity eid, int order);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_decal_get_receiver_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_receiver_layers(Entity eid, LayerMask layers);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_decal_get_remove_after_fade(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decal_set_remove_after_fade(Entity eid, bool remove);
    }

    /// <summary>
    /// Places decals from scripts: bullet holes, footprints, scorch marks.
    /// </summary>
    public static class Decals
    {
        /// <summary>
        /// The most decals made by <see cref="Spawn"/> at once; past it the oldest fade out quickly and go. 256 by
        /// default.
        /// </summary>
        public static int maxTransientDecals
        {
            get => internal_m2n_decals_get_max_transient();
            set => internal_m2n_decals_set_max_transient(value);
        }

        /// <summary>
        /// Places a decal of <paramref name="material"/> on the surface at <paramref name="point"/>, facing into it
        /// against <paramref name="normal"/> (a raycast's hit point and normal). It fades out over
        /// <paramref name="fadeOut"/> seconds once <paramref name="lifetime"/> is over and is then removed; a lifetime of
        /// 0 keeps it. With <paramref name="attachTo"/> it follows that entity's moves.
        /// </summary>
        /// <param name="material">A Decal material; null takes the engine's Default Decal.</param>
        /// <param name="point">Where on the surface, in world space.</param>
        /// <param name="normal">The surface's normal there, in world space.</param>
        /// <param name="size">Width and height of the decal, in metres.</param>
        /// <param name="rotation">Degrees the decal turns about the normal.</param>
        /// <param name="lifetime">Seconds it stays before fading out; 0 keeps it.</param>
        /// <param name="fadeOut">Seconds it takes to fade out.</param>
        /// <param name="attachTo">An entity it moves with, such as the object it was shot onto.</param>
        /// <returns>The decal, to change it further.</returns>
        public static DecalComponent Spawn(Material material,
                                           Vector3 point,
                                           Vector3 normal,
                                           Vector2 size,
                                           float rotation = 0f,
                                           float lifetime = 0f,
                                           float fadeOut = 1f,
                                           Entity attachTo = default)
        {
            var entity = internal_m2n_decals_spawn(material?.uid ?? Guid.Empty, point, normal, size, rotation, lifetime, fadeOut, attachTo);
            return entity.IsValid() ? entity.GetComponent<DecalComponent>() : null;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_decals_spawn(Guid material,
                                                               Vector3 point,
                                                               Vector3 normal,
                                                               Vector2 size,
                                                               float rotation,
                                                               float lifetime,
                                                               float fadeOut,
                                                               Entity attachTo);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_decals_get_max_transient();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_decals_set_max_transient(int count);
    }
}
