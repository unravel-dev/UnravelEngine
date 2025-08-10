using System;
using System.Runtime.CompilerServices;

namespace Ace.Core
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_load_scene(string key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void internal_m2n_create_scene();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void internal_m2n_destroy_scene();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_uid(Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_create_entity_from_prefab_key(string key);

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
    }
}
