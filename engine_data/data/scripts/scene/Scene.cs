using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a scene in the application, providing methods to manage entities and load or destroy scenes.
    /// </summary>
    public class Scene : Asset<Scene>
    {
        /// <summary>
        /// Loads a scene by its unique key.
        /// </summary>
        /// <param name="key">The key identifying the scene to load.</param>
        public static void LoadScene(string key)
        {
            internal_m2n_load_scene(key);
        }

        /// <summary>
        /// Loads a scene using a Scene asset.
        /// </summary>
        /// <param name="scene">The Scene asset to load.</param>
        public static void LoadScene(Scene scene)
        {
            internal_m2n_load_scene_uid(scene.uid);
        }

        /// <summary>
        /// Reloads the current scene from its source prefab asset.
        /// </summary>
        public static void ReloadScene()
        {
            internal_m2n_reload_scene();
        }

        /// <summary>
        /// Instantiates an entity from a specified prefab.
        /// </summary>
        /// <param name="prefab">The prefab to instantiate.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(Prefab prefab)
        {
            return internal_m2n_create_entity_from_prefab_uid(prefab.uid);
        }

        /// <summary>
        /// Instantiates an entity from a prefab identified by a key.
        /// </summary>
        /// <param name="key">The key identifying the prefab to instantiate.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(string key)
        {
            return internal_m2n_create_entity_from_prefab_key(key);
        }

        /// <summary>
        /// Instantiates an entity from a specified prefab with a parent entity.
        /// </summary>
        /// <param name="prefab">The prefab to instantiate.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(Prefab prefab, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_uid_with_parent(prefab.uid, parent);
        }

        /// <summary>
        /// Instantiates an entity from a prefab identified by a key with a parent entity.
        /// </summary>
        /// <param name="key">The key identifying the prefab to instantiate.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(string key, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_key_with_parent(key, parent);
        }

        /// <summary>
        /// Instantiates an entity from a specified prefab at a specific position.
        /// </summary>
        /// <param name="prefab">The prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(Prefab prefab, Vector3 position)
        {
            return internal_m2n_create_entity_from_prefab_uid_with_position(prefab.uid, position);
        }

        /// <summary>
        /// Instantiates an entity from a prefab identified by a key at a specific position.
        /// </summary>
        /// <param name="key">The key identifying the prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(string key, Vector3 position)
        {
            return internal_m2n_create_entity_from_prefab_key_with_position(key, position);
        }

        /// <summary>
        /// Instantiates an entity from a specified prefab at a specific position with a parent entity.
        /// </summary>
        /// <param name="prefab">The prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(Prefab prefab, Vector3 position, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_uid_with_position_parent(prefab.uid, position, parent);
        }

        /// <summary>
        /// Instantiates an entity from a prefab identified by a key at a specific position with a parent entity.
        /// </summary>
        /// <param name="key">The key identifying the prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(string key, Vector3 position, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_key_with_position_parent(key, position, parent);
        }

        /// <summary>
        /// Instantiates an entity from a specified prefab at a specific position and rotation with a parent entity.
        /// </summary>
        /// <param name="prefab">The prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <param name="rotation">The world rotation of the entity.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent(prefab.uid, position, rotation, parent);
        }

        /// <summary>
        /// Instantiates an entity from a prefab identified by a key at a specific position and rotation with a parent entity.
        /// </summary>
        /// <param name="key">The key identifying the prefab to instantiate.</param>
        /// <param name="position">The world position where the entity will be instantiated.</param>
        /// <param name="rotation">The world rotation of the entity.</param>
        /// <param name="parent">The parent entity to attach the instantiated entity to.</param>
        /// <returns>The instantiated entity.</returns>
        public static Entity Instantiate(string key, Vector3 position, Quaternion rotation, Entity parent)
        {
            return internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent(key, position, rotation, parent);
        }

        /// <summary>
        /// Clones an existing entity.
        /// </summary>
        /// <param name="e">The entity to clone.</param>
        /// <returns>A new entity that is a clone of the specified entity.</returns>
        public static Entity CloneEntity(Entity e)
        {
            return internal_m2n_clone_entity(e);
        }

        /// <summary>
        /// Creates a new entity with the specified name.
        /// </summary>
        /// <param name="name">The name to assign to the new entity. Defaults to "Unnamed".</param>
        /// <returns>The newly created entity.</returns>
        public static Entity CreateEntity(string name = "Unnamed")
        {
            return internal_m2n_create_entity(name);
        }

        /// <summary>
        /// Destroys the specified entity after a delay.
        /// </summary>
        /// <param name="entity">The entity to destroy.</param>
        /// <param name="seconds">The delay in seconds before destruction. Defaults to 0.</param>
        public static void DestroyEntity(Entity entity, float seconds = 0.0f)
        {
            unsafe
            {
                internal_m2n_destroy_entity(entity, seconds);
            }
        }

        /// <summary>
        /// Immediately destroys the specified entity.
        /// </summary>
        /// <param name="entity">The entity to destroy.</param>
        public static void DestroyEntityImmediate(Entity entity)
        {
            unsafe
            {
                internal_m2n_destroy_entity_immediate(entity);
            }
        }

        /// <summary>
        /// Determines whether the specified entity is valid within the current scene.
        /// </summary>
        /// <param name="entity">The entity to validate.</param>
        /// <returns><c>true</c> if the entity is valid; otherwise, <c>false</c>.</returns>
        public static bool IsEntityValid(Entity entity)
        {
            return internal_m2n_is_entity_valid(entity);
        }

        /// <summary>
        /// Finds the first entity with the specified tag.
        /// </summary>
        /// <param name="tag">The tag to search for.</param>
        /// <returns>The entity with the specified tag, or <c>invalid</c> if no such entity exists.</returns>
        public static Entity FindEntityByTag(string tag)
        {
            return internal_m2n_find_entity_by_tag(tag);
        }

        /// <summary>
        /// Finds all entities with the specified tag.
        /// </summary>
        /// <param name="tag">The tag to search for.</param>
        /// <returns>The entities with the specified tag, or <c>empty</c> if no entities match.</returns>
        public static Entity[] FindEntitiesByTag(string tag)
        {

            byte[] rawEntities = internal_m2n_find_entities_by_tag(tag);
            return rawEntities.ToStructArray<Entity>();
        }

        
        /// <summary>
        /// Finds the first entity with the specified name.
        /// </summary>
        /// <param name="name">The name to search for.</param>
        /// <returns>The entity with the specified name, or <c>invalid</c> if no such entity exists.</returns>
        public static Entity FindEntityByName(string name)
        {
            return internal_m2n_find_entity_by_name(name);
        }


         // <summary>
        /// Finds all entities with the specified name.
        /// </summary>
        /// <param name="name">The name to search for.</param>
        /// <returns>The entities with the specified name, or <c>empty</c> if no entities match.</returns>
        public static Entity[] FindEntitiesByName(string name)
        {

            byte[] rawEntities = internal_m2n_find_entities_by_name(name);
            return rawEntities.ToStructArray<Entity>();
        }

        /// <summary>
        /// Finds all entities that have the specified component type.
        /// </summary>
        /// <typeparam name="T">The component type to search for.</typeparam>
        /// <returns>The entities that have the specified component, or empty if no entities match.</returns>
        public static Entity[] FindEntitiesWithComponent<T>() where T : Component
        {
            byte[] rawEntities = internal_m2n_find_entities_with_component(typeof(T));
            return rawEntities.ToStructArray<Entity>();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_load_scene(string key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_load_scene_uid(Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_reload_scene();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void internal_m2n_create_scene();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void internal_m2n_destroy_scene();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid(Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key(string key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid_with_parent(Guid uid, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key_with_parent(string key, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid_with_position(Guid uid, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key_with_position(string key, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid_with_position_parent(Guid uid, Vector3 position, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key_with_position_parent(string key, Vector3 position, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent(Guid uid, Vector3 position, Quaternion rotation, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent(string key, Vector3 position, Quaternion rotation, Entity parent);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity(string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_clone_entity(Entity id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_destroy_entity(Entity id, float seconds);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_destroy_entity_immediate(Entity id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_is_entity_valid(Entity id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_find_entity_by_name(string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_find_entities_by_name(string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_find_entity_by_tag(string tag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_find_entities_by_tag(string tag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_find_entities_with_component(Type componentType);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_find_entities_with_components(Type[] componentTypes);
    }
}
