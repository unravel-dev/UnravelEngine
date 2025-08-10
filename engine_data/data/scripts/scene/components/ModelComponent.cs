using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
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
        /// Sets the material at the specified index for the model.
        /// </summary>
        /// <param name="material">The <see cref="Material"/> to assign, or <c>null</c> to remove the material.</param>
        /// <param name="index">The index of the material to set.</param>
        public void SetSharedMaterial(Material material, uint index = 0)
        {
            internal_m2n_model_set_shared_material(owner, material?.uid ?? Guid.Empty, index);
        }

        /// <summary>
        /// Sets the material instance at the specified index for the model.
        /// </summary>
        /// <param name="material">The <see cref="Material"/> to assign, or <c>null</c> to remove the material.</param>
        /// <param name="index">The index of the material to set.</param>
        public void SetMaterial(Material material, uint index = 0)
        {
            if(material == null)
            {
                internal_m2n_model_set_material_instance(owner, new MaterialProperties(), index);
                return;
            }
            internal_m2n_model_set_material_instance(owner, material.GetProperties(), index);
        }

        public void ResetMaterials()
        {
            var count = GetSharedMaterialsCount();
			for(uint i = 0; i < count; ++i)
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
			for(uint i = 0; i < count; ++i)
			{
                var mat = GetMaterial(i);
                mat.color = color;
                SetMaterial(mat, i);
			}
        }

        /// <summary>
        /// Sets the material instance color at the specified index for the model.
        /// </summary>
        /// <param name="color">The <see cref="Color"/> to assign.</param>
        /// <param name="index">The index of the material to assign.</param>
        public void SetColor(Color color, uint index)
        {
            var mat = GetMaterial(index);
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
            var count = GetSharedMaterialsCount();
            if(count == 0)
            {
                return Color.white;
            }
            var mat = GetMaterial(0);
            return mat.color;
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
            return mat.color;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_model_get_enabled(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_model_set_enabled(Entity eid, bool enabled);

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
}
