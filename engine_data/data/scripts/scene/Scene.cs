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
        /// Loads a scene next to the current one, at the same point <see cref="LoadScene(string)"/> would,
        /// so calls apply in the order they were made. Its entities stay until
        /// <see cref="UnloadScene(SceneInstance)"/>, <see cref="LoadScene(string)"/> or <see cref="ReloadScene"/>
        /// removes them. Entities its scripts create while it starts belong to it; entities created later
        /// belong to it only when they are parented under one of its entities.
        /// </summary>
        /// <param name="key">The key identifying the scene to load.</param>
        /// <returns>The loaded scene, for <see cref="UnloadScene(SceneInstance)"/>.</returns>
        public static SceneInstance LoadSceneAdditive(string key)
        {
            return new SceneInstance(internal_m2n_load_scene_additive(key));
        }

        /// <summary>
        /// Loads a Scene asset next to the current one. See <see cref="LoadSceneAdditive(string)"/>.
        /// </summary>
        /// <param name="scene">The Scene asset to load.</param>
        /// <returns>The loaded scene, for <see cref="UnloadScene(SceneInstance)"/>.</returns>
        public static SceneInstance LoadSceneAdditive(Scene scene)
        {
            return new SceneInstance(internal_m2n_load_scene_additive_uid(scene.uid));
        }

        /// <summary>
        /// Removes the entities of a scene loaded with <see cref="LoadSceneAdditive(string)"/>, with
        /// everything parented under them, at the same point <see cref="DestroyEntity"/> would.
        /// Entities made persistent with <see cref="DontDestroyOnLoad"/> stay.
        /// </summary>
        /// <param name="instance">The scene to unload.</param>
        public static void UnloadScene(SceneInstance instance)
        {
            internal_m2n_unload_scene(instance.Group);
        }

        /// <summary>
        /// Keeps a root entity, with everything parented under it, through <see cref="LoadScene(string)"/>,
        /// <see cref="ReloadScene"/> and <see cref="UnloadScene(SceneInstance)"/>.
        /// </summary>
        /// <param name="entity">The entity to keep. It must have no parent.</param>
        /// <returns><c>true</c> if the entity is kept; <c>false</c> if it has a parent.</returns>
        public static bool DontDestroyOnLoad(Entity entity)
        {
            return internal_m2n_dont_destroy_on_load(entity);
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


        /// <summary>
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
        private static extern uint internal_m2n_load_scene_additive(string key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_load_scene_additive_uid(Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_unload_scene(uint group);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_dont_destroy_on_load(Entity entity);

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

    /// <summary>
    /// A scene loaded next to the current one with <see cref="Scene.LoadSceneAdditive(string)"/>.
    /// </summary>
    public readonly struct SceneInstance
    {
        internal SceneInstance(uint group)
        {
            Group = group;
        }

        internal uint Group { get; }

        /// <summary>
        /// Whether this came from <see cref="Scene.LoadSceneAdditive(string)"/>. A default value is not valid.
        /// </summary>
        public bool IsValid => Group != 0;
    }
}
