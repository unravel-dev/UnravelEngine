using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Identifies a bone node in a skinned model armature.
    /// </summary>
    public class BoneComponent : Component
    {
        /// <summary>
        /// Index of this bone in the skeleton.
        /// </summary>
        public uint boneIndex
        {
            get => internal_m2n_bone_get_index(owner);
            set => internal_m2n_bone_set_index(owner, value);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_bone_get_index(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_bone_set_index(Entity eid, uint index);
    }
}
