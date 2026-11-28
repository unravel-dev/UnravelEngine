using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// What the components that draw geometry the engine builds every frame share (<see cref="LineComponent"/>,
    /// <see cref="TrailComponent"/>): a Surface material with this renderer's own values
    /// of it, whether it casts a shadow, and its place among the see-through renderers.
    /// </summary>
    public abstract class DynamicRendererComponent : Component
    {
        private MaterialInstance instance;

        /// <summary>What the native calls know the component by: the slot of its material instance
        /// (MaterialInstance.LineSlot or TrailSlot).</summary>
        internal abstract uint rendererSlot { get; }

        /// <summary>The Surface material the geometry draws with. An Opaque or Dithered one is lit as any surface, and
        /// a see-through one sorts with the particles and the see-through models. Null draws the engine's own for the
        /// component.</summary>
        public Material material
        {
            get
            {
                var uid = internal_m2n_dynamic_renderer_get_material(owner, rendererSlot);
                return uid == Guid.Empty ? null : new Material { uid = uid };
            }
            set => internal_m2n_dynamic_renderer_set_material(owner, rendererSlot, value?.uid ?? Guid.Empty);
        }

        /// <summary>This renderer's overrides of its material: set a parameter or a texture here and only this
        /// renderer changes.</summary>
        public MaterialInstance materialInstance => instance ??= new MaterialInstance(owner, rendererSlot);

        /// <summary>Where a see-through material draws the geometry among the particles and the see-through models:
        /// lower first, then far to near.</summary>
        public int sortPriority
        {
            get => internal_m2n_dynamic_renderer_get_sort_priority(owner, rendererSlot);
            set => internal_m2n_dynamic_renderer_set_sort_priority(owner, rendererSlot, value);
        }

        /// <summary>The geometry casts shadows when its material casts them: an Opaque material, or a see-through one
        /// whose Cast Shadow is Dithered.</summary>
        public bool castsShadow
        {
            get => internal_m2n_dynamic_renderer_get_casts_shadow(owner, rendererSlot);
            set => internal_m2n_dynamic_renderer_set_casts_shadow(owner, rendererSlot, value);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Guid internal_m2n_dynamic_renderer_get_material(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_dynamic_renderer_set_material(Entity eid, uint slot, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_dynamic_renderer_get_sort_priority(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_dynamic_renderer_set_sort_priority(Entity eid, uint slot, int priority);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_dynamic_renderer_get_casts_shadow(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_dynamic_renderer_set_casts_shadow(Entity eid, uint slot, bool value);
    }
}
