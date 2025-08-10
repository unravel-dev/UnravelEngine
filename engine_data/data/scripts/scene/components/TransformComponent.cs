using System;
using System.Runtime.CompilerServices;

namespace Ace.Core
{
    /// <summary>
    /// Represents a component that defines the position, rotation, scale, and other transformations of an entity in 3D space.
    /// </summary>
    public class TransformComponent : Component
    {
        
        /// <summary>
        /// Gets or sets the parent entity of the current entity.
        /// </summary>
        /// <value>The <see cref="Entity"/> object representing the parent entity.</value>  
        public Entity parent
        {
            get
            {
                return internal_m2n_get_parent(owner);
            }
            set
            {
                internal_m2n_set_parent(owner, value, true);
            }
        }

        /// <summary>
        /// Gets the child entities of the current entity.
        /// </summary>
        /// <value>An array of <see cref="Entity"/> objects representing the child entities.</value>
        public Entity[] children
        {
            get
            {
                return internal_m2n_get_children(owner).ToStructArray<Entity>();
            }
        }

        /// <summary>
        /// Finds a child by name/path and returns it.
        /// If no child with name n can be found, null is returned.
        /// If n contains a '/' character it will access the Transform in the hierarchy like a path name
        /// Note: Find does not work properly if you have '/' in the name of an Entity.
        /// Note: Find can find transform of an inactive/disabled Entity
        /// </summary>
        /// <param name="path">The <see cref="string"/> name to find.</param>
        /// <param name="recursive">
        /// A boolean value indicating whether to perform a recursive descend down a Transform hierarchy.
        /// </param>
        public Entity FindChild(string path, bool recursive = false)
        {
            return internal_m2n_get_child(owner, path, recursive);
        }

        /// <summary>
        /// Sets the parent entity of the current entity.
        /// </summary>
        /// <param name="parent">The <see cref="Entity"/> to set as the parent.</param>
        /// <param name="globalPositionStays">
        /// A boolean value indicating whether the entity should retain its global position when reparented.
        /// If <c>true</c>, the entity's global position will not change. If <c>false</c>, the entity's global position
        /// will be adjusted relative to the new parent.
        /// </param>
        public void SetParent(Entity parent, bool globalPositionStays)
        {
            internal_m2n_set_parent(owner, parent, globalPositionStays);
        }
        /// <summary>
        /// Gets or sets the world space position of the Transform.
        /// </summary>
        public Vector3 position
        {
            get
            {
                return internal_m2n_get_position_global(owner);
            }
            set
            {
                internal_m2n_set_position_global(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the position of the Transform relative to the parent transform.
        /// </summary>
        public Vector3 localPosition
        {
            get
            {
                return internal_m2n_get_position_local(owner);
            }
            set
            {
                internal_m2n_set_position_local(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the rotation of the Transform in world space, expressed in Euler angles.
        /// </summary>
        public Vector3 eulerAngles
        {
            get
            {
                return internal_m2n_get_rotation_euler_global(owner);
            }
            set
            {
                internal_m2n_set_rotation_euler_global(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the rotation of the Transform relative to the parent, expressed in local Euler angles.
        /// </summary>
        public Vector3 localEulerAngles
        {
            get
            {
                return internal_m2n_get_rotation_euler_local(owner);
            }
            set
            {
                internal_m2n_set_rotation_euler_local(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the rotation of the Transform in world space, expressed as a Quaternion.
        /// </summary>
        public Quaternion rotation
        {
            get
            {
                return internal_m2n_get_rotation_global(owner);
            }
            set
            {
                internal_m2n_set_rotation_global(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the rotation of the Transform relative to the parent, expressed as a Quaternion.
        /// </summary>
        public Quaternion localRotation
        {
            get
            {
                return internal_m2n_get_rotation_local(owner);
            }
            set
            {
                internal_m2n_set_rotation_local(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the global scale of the Transform.
        /// </summary>
        public Vector3 scale
        {
            get
            {
                return internal_m2n_get_scale_global(owner);
            }
            set
            {
                internal_m2n_set_scale_global(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the scale of the Transform relative to the parent.
        /// </summary>
        public Vector3 localScale
        {
            get
            {
                return internal_m2n_get_scale_local(owner);
            }
            set
            {
                internal_m2n_set_scale_local(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the global skew of the Transform.
        /// </summary>
        public Vector3 skew
        {
            get
            {
                return internal_m2n_get_skew_global(owner);
            }
            set
            {
                internal_m2n_set_skew_global(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the skew of the Transform relative to the parent.
        /// </summary>
        public Vector3 localSkew
        {
            get
            {
                return internal_m2n_get_skew_local(owner);
            }
            set
            {
                internal_m2n_set_skew_local(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the right vector of the Transform in world space.
        /// </summary>
        public Vector3 right
        {
            get
            {
                return rotation * Vector3.right;
            }
            set
            {
                rotation = Quaternion.FromToRotation(Vector3.right, value);
            }
        }

        /// <summary>
        /// Gets or sets the right vector of the Transform relative to the parent.
        /// </summary>
        public Vector3 localRight
        {
            get
            {
                return localRotation * Vector3.right;
            }
            set
            {
                localRotation = Quaternion.FromToRotation(Vector3.right, value);
            }
        }

        /// <summary>
        /// Gets or sets the up vector of the Transform in world space.
        /// </summary>
        public Vector3 up
        {
            get
            {
                return rotation * Vector3.up;
            }
            set
            {
                rotation = Quaternion.FromToRotation(Vector3.up, value);
            }
        }

        /// <summary>
        /// Gets or sets the up vector of the Transform relative to the parent.
        /// </summary>
        public Vector3 localUp
        {
            get
            {
                return localRotation * Vector3.up;
            }
            set
            {
                localRotation = Quaternion.FromToRotation(Vector3.up, value);
            }
        }

        /// <summary>
        /// Gets or sets the forward vector of the Transform in world space.
        /// </summary>
        public Vector3 forward
        {
            get
            {
                return rotation * Vector3.forward;
            }
            set
            {
                rotation = Quaternion.FromToRotation(Vector3.forward, value);
            }
        }

        /// <summary>
        /// Gets or sets the forward vector of the Transform relative to the parent.
        /// </summary>
        public Vector3 localForward
        {
            get
            {
                return localRotation * Vector3.forward;
            }
            set
            {
                localRotation = Quaternion.FromToRotation(Vector3.forward, value);
            }
        }

        /// <summary>
        /// Moves the Transform in world space by a specified amount.
        /// </summary>
        /// <param name="amount">The amount to move the Transform.</param>
        public void MoveBy(Vector3 amount)
        {
            internal_m2n_move_by_global(owner, amount);
        }

        /// <summary>
        /// Moves the Transform relative to the parent by a specified amount.
        /// </summary>
        /// <param name="amount">The amount to move the Transform.</param>
        public void MoveByLocal(Vector3 amount)
        {
            internal_m2n_move_by_local(owner, amount);
        }

        /// <summary>
        /// Scales the Transform in world space by a specified amount.
        /// </summary>
        /// <param name="amount">The scaling factors to apply.</param>
        public void ScaleBy(Vector3 amount)
        {
            internal_m2n_scale_by_global(owner, amount);
        }

        /// <summary>
        /// Scales the Transform relative to the parent by a specified amount.
        /// </summary>
        /// <param name="amount">The scaling factors to apply.</param>
        public void ScaleByLocal(Vector3 amount)
        {
            internal_m2n_scale_by_local(owner, amount);
        }

        /// <summary>
        /// Rotates the Transform in world space by a specified Quaternion.
        /// </summary>
        /// <param name="amount">The rotation to apply.</param>
        public void RotateBy(Quaternion amount)
        {
            internal_m2n_rotate_by_global(owner, amount);
        }

        /// <summary>
        /// Rotates the Transform relative to the parent by a specified Quaternion.
        /// </summary>
        /// <param name="amount">The rotation to apply.</param>
        public void RotateByLocal(Quaternion amount)
        {
            internal_m2n_rotate_by_local(owner, amount);
        }

        /// <summary>
        /// Rotates the Transform in world space by specified Euler angles.
        /// </summary>
        /// <param name="amount">The Euler angles to rotate by.</param>
        public void RotateByEuler(Vector3 amount)
        {
            internal_m2n_rotate_by_euler_global(owner, amount);
        }

        /// <summary>
        /// Rotates the Transform relative to the parent by specified Euler angles.
        /// </summary>
        /// <param name="amount">The Euler angles to rotate by.</param>
        public void RotateByEulerLocal(Vector3 amount)
        {
            internal_m2n_rotate_by_euler_local(owner, amount);
        }

        /// <summary>
        /// Rotates the Transform in world space around a specified axis by a specified angle.
        /// </summary>
        /// <param name="degrees">The angle in degrees to rotate.</param>
        /// <param name="axis">The axis to rotate around.</param>
        public void RotateAxis(float degrees, Vector3 axis)
        {
            internal_m2n_rotate_axis_global(owner, degrees, axis);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified point in world space.
        /// </summary>
        /// <param name="point">The point to look at.</param>
        public void LookAt(Vector3 point)
        {
            LookAt(point, Vector3.up);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified point in world space, using the specified up vector.
        /// </summary>
        /// <param name="point">The point to look at.</param>
        /// <param name="up">The vector that defines in which direction up is.</param>
        public void LookAt(Vector3 point, Vector3 up)
        {
            internal_m2n_look_at(owner, point, up);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified target entity in world space.
        /// </summary>
        /// <param name="target">The target entity to look at.</param>
        public void LookAt(Entity target)
        {
            LookAt(target, Vector3.up);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified target entity in world space, using the specified up vector.
        /// </summary>
        /// <param name="target">The target entity to look at.</param>
        /// <param name="up">The vector that defines in which direction up is.</param>
        public void LookAt(Entity target, Vector3 up)
        {
            LookAt(target.transform.position, up);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified target TransformComponent in world space.
        /// </summary>
        /// <param name="target">The target TransformComponent to look at.</param>
        public void LookAt(TransformComponent target)
        {
            LookAt(target.position, Vector3.up);
        }

        /// <summary>
        /// Rotates the Transform to look at the specified target TransformComponent in world space, using the specified up vector.
        /// </summary>
        /// <param name="target">The target TransformComponent to look at.</param>
        /// <param name="up">The vector that defines in which direction up is.</param>
        public void LookAt(TransformComponent target, Vector3 up)
        {
            LookAt(target.position, up);
        }



        /// <summary>
        /// Transforms vector from local space to world space.
        /// </summary>
        /// <param name="vector">The vector to transform.</param>
        public Vector3 TransformVector(Vector3 vector)
        {
            return internal_m2n_transform_vector_global(owner, vector);
        }

        /// <summary>
        /// Transforms vector from world space to local space.
        /// </summary>
        /// <param name="vector">The vector to transform.</param>
        public Vector3 InverseTransformVector(Vector3 vector)
        {
            return internal_m2n_inverse_transform_vector_global(owner, vector);
        }

        /// <summary>
        /// Transforms direction from local space to world space.
        /// </summary>
        /// <param name="direction">The direction to transform.</param>
        public Vector3 TransformDirection(Vector3 direction)
        {
            return internal_m2n_transform_direction_global(owner, direction);
        }

        /// <summary>
        /// Transforms direction from world space to local space.
        /// </summary>
        /// <param name="direction">The direction to transform.</param>
        public Vector3 InverseTransformDirection(Vector3 direction)
        {
            return internal_m2n_inverse_transform_direction_global(owner, direction);
        }

        /// <summary>
        /// Rotates the Transform around a specified point and axis by a specified angle.
        /// </summary>
        /// <param name="point">The point to rotate around.</param>
        /// <param name="axis">The axis to rotate around.</param>
        /// <param name="angle">The angle in degrees to rotate.</param>
        public void RotateAround(Vector3 point, Vector3 axis, float angle)
        {
            Vector3 vector = position;
            Quaternion quaternion = Quaternion.AngleAxis(angle, axis);
            Vector3 vector2 = vector - point;
            vector2 = quaternion * vector2;
            vector = point + vector2;
            position = vector;
            RotateAxis(angle, axis);
        }

        /// <summary>
        /// Rotates the Transform around a specified target entity and axis by a specified angle.
        /// </summary>
        /// <param name="target">The target entity to rotate around.</param>
        /// <param name="axis">The axis to rotate around.</param>
        /// <param name="angle">The angle in degrees to rotate.</param>
        public void RotateAround(Entity target, Vector3 axis, float angle)
        {
            RotateAround(target.transform.position, axis, angle);
        }

        /// <summary>
        /// Rotates the Transform around a specified target TransformComponent and axis by a specified angle.
        /// </summary>
        /// <param name="target">The target TransformComponent to rotate around.</param>
        /// <param name="axis">The axis to rotate around.</param>
        /// <param name="angle">The angle in degrees to rotate.</param>
        public void RotateAround(TransformComponent target, Vector3 axis, float angle)
        {
            RotateAround(target.position, axis, angle);
        }

        /// <summary>
        /// Moves the Transform towards the specified target entity by a maximum distance.
        /// </summary>
        /// <param name="target">The target entity to move towards.</param>
        /// <param name="maxDistanceDelta">The maximum distance to move.</param>
        public void MoveTowards(Entity target, float maxDistanceDelta)
        {
            position = Vector3.MoveTowards(position, target.transform.position, maxDistanceDelta);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_get_children(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_get_child(Entity eid, string path, bool recursive);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_get_parent(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_parent(Entity eid, Entity newParent, bool globalStays);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_position_global(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_position_global(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_move_by_global(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_position_local(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_position_local(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_move_by_local(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_rotation_euler_global(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_rotation_euler_global(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_rotate_by_euler_global(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_rotate_axis_global(Entity eid, float degrees, Vector3 axis);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at(Entity eid, Vector3 point, Vector3 up);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_transform_vector_global(Entity eid, Vector3 vector);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_inverse_transform_vector_global(Entity eid, Vector3 vector);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_transform_direction_global(Entity eid, Vector3 direction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_inverse_transform_direction_global(Entity eid, Vector3 direction);


        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_rotation_euler_local(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_rotation_euler_local(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_rotate_by_euler_local(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Quaternion internal_m2n_get_rotation_global(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_rotation_global(Entity eid, Quaternion value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_rotate_by_global(Entity eid, Quaternion amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Quaternion internal_m2n_get_rotation_local(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_rotation_local(Entity eid, Quaternion value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_rotate_by_local(Entity eid, Quaternion amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_scale_global(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_scale_global(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_scale_by_global(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_scale_local(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_scale_local(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_scale_by_local(Entity eid, Vector3 amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_skew_global(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_skew_global(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_get_skew_local(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_skew_local(Entity eid, Vector3 value);
    }
}
