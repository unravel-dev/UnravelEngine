using System;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// Implemented by a script on a pooled entity's root to reset itself when the pool hands the
    /// entity out and to clean up when it comes back.
    /// </summary>
    public interface IPoolable
    {
        /// <summary>Called after the entity is activated by <see cref="EntityPool.Acquire()"/>.</summary>
        void OnAcquire();

        /// <summary>Called before the entity is deactivated by <see cref="EntityPool.Release(Entity)"/>.</summary>
        void OnRelease();
    }

    /// <summary>
    /// Reuses prefab instances via SetActive instead of Instantiate/Destroy each time.
    /// Suitable for high-churn gameplay objects (enemies, projectiles, pickups, VFX).
    /// </summary>
    /// <remarks>
    /// <see cref="For"/> returns one shared pool per prefab. Scripts on an instance's root that
    /// implement <see cref="IPoolable"/> are told when the instance is acquired and released.
    /// </remarks>
    [AutoStaticsCleanup]
    public sealed class EntityPool
    {
        private static Dictionary<Guid, EntityPool> shared_ = new Dictionary<Guid, EntityPool>();
        private static HashSet<EntityPool> withDelayedReleases_ = new HashSet<EntityPool>();

        private static void OnStaticsCleanup()
        {
            shared_ = new Dictionary<Guid, EntityPool>();
            withDelayedReleases_ = new HashSet<EntityPool>();
        }

        private readonly Prefab prefab_;
        private readonly Stack<Entity> inactive_ = new Stack<Entity>();
        // Membership of inactive_, so a second Release of the same entity is a no-op instead of
        // pushing it twice (and handing it out twice).
        private readonly HashSet<Entity> inactiveSet_ = new HashSet<Entity>();
        private readonly HashSet<Entity> all_ = new HashSet<Entity>();
        // Release time (Time.time) of entities released with a delay.
        private readonly Dictionary<Entity, float> releaseAt_ = new Dictionary<Entity, float>();

        /// <summary>
        /// Creates a pool for the given prefab and optionally prewarms inactive instances.
        /// </summary>
        /// <param name="prefab">Prefab to instantiate when the pool is empty.</param>
        /// <param name="prewarm">Number of inactive instances to create up front.</param>
        public EntityPool(Prefab prefab, int prewarm = 0)
        {
            if (prefab == null)
            {
                throw new ArgumentNullException(nameof(prefab));
            }
            prefab_ = prefab;
            if (prewarm > 0)
            {
                Prewarm(prewarm);
            }
        }

        /// <summary>
        /// The pool shared by every caller for @p prefab, created on first use.
        /// </summary>
        public static EntityPool For(Prefab prefab)
        {
            if (prefab == null)
            {
                throw new ArgumentNullException(nameof(prefab));
            }
            if (!shared_.TryGetValue(prefab.uid, out EntityPool pool))
            {
                pool = new EntityPool(prefab);
                shared_.Add(prefab.uid, pool);
            }
            return pool;
        }

        /// <summary>
        /// Number of inactive entities currently available.
        /// </summary>
        public int InactiveCount => inactive_.Count;

        /// <summary>
        /// Total entities created by this pool (active + inactive).
        /// </summary>
        public int TotalCount => all_.Count;

        /// <summary>
        /// Instantiates inactive instances so later Acquire calls avoid spawn cost.
        /// </summary>
        public void Prewarm(int count)
        {
            for (int i = 0; i < count; ++i)
            {
                PushInactive(CreateInactiveInstance());
            }
        }

        /// <summary>
        /// Takes an entity from the pool (or instantiates one) and activates it.
        /// </summary>
        public Entity Acquire()
        {
            Entity entity = TakeOrCreate();
            Activate(entity);
            return entity;
        }

        /// <summary>
        /// Acquires an entity and places it at the given world pose.
        /// </summary>
        public Entity Acquire(Vector3 position, Quaternion rotation)
        {
            Entity entity = TakeOrCreate();
            entity.transform.position = position;
            entity.transform.rotation = rotation;
            Activate(entity);
            return entity;
        }

        /// <summary>
        /// Acquires an entity, parents it, then activates it.
        /// </summary>
        public Entity Acquire(Entity parent)
        {
            Entity entity = TakeOrCreate();
            entity.transform.SetParent(parent, false);
            Activate(entity);
            return entity;
        }

        /// <summary>
        /// Returns an entity to the pool. Stops audio on the root if present, then deactivates.
        /// Releasing an entity that is already in the pool does nothing; a pending delayed release
        /// of it is cancelled.
        /// </summary>
        public void Release(Entity entity)
        {
            CancelDelayedRelease(entity);
            if (!entity.IsValid() || !all_.Contains(entity) || inactiveSet_.Contains(entity))
            {
                return;
            }
            NotifyPoolable(entity, false);
            AudioSourceComponent audio = entity.GetComponent<AudioSourceComponent>();
            if (audio != null && audio.isPlaying)
            {
                audio.Stop();
            }
            entity.transform.SetParent(Entity.Invalid, true);
            entity.SetActive(false);
            PushInactive(entity);
        }

        /// <summary>
        /// Returns an entity to the pool after @p delay seconds of game time (scaled by
        /// Time.timeScale). A later Release call replaces the pending one.
        /// </summary>
        public void Release(Entity entity, float delay)
        {
            if (delay <= 0f)
            {
                Release(entity);
                return;
            }
            if (!all_.Contains(entity) || inactiveSet_.Contains(entity))
            {
                return;
            }
            releaseAt_[entity] = Time.time + delay;
            withDelayedReleases_.Add(this);
        }

        /// <summary>
        /// Destroys every entity owned by this pool and clears internal lists.
        /// </summary>
        public void Clear()
        {
            releaseAt_.Clear();
            withDelayedReleases_.Remove(this);
            foreach (Entity entity in all_)
            {
                if (entity.IsValid())
                {
                    Scene.DestroyEntityImmediate(entity);
                }
            }
            inactive_.Clear();
            inactiveSet_.Clear();
            all_.Clear();
        }

        /// <summary>
        /// Performs the delayed releases that are due. Runs once per frame after the scripts.
        /// </summary>
        internal static void ProcessDelayedReleases()
        {
            if (withDelayedReleases_.Count == 0)
            {
                return;
            }
            var due = new List<Entity>();
            foreach (EntityPool pool in new List<EntityPool>(withDelayedReleases_))
            {
                due.Clear();
                foreach (KeyValuePair<Entity, float> pending in pool.releaseAt_)
                {
                    if (Time.time >= pending.Value)
                    {
                        due.Add(pending.Key);
                    }
                }
                foreach (Entity entity in due)
                {
                    pool.Release(entity);
                }
            }
        }

        private void CancelDelayedRelease(Entity entity)
        {
            if (releaseAt_.Remove(entity) && releaseAt_.Count == 0)
            {
                withDelayedReleases_.Remove(this);
            }
        }

        private void Activate(Entity entity)
        {
            entity.SetActive(true);
            NotifyPoolable(entity, true);
        }

        /// <summary>
        /// Calls OnAcquire / OnRelease on the root scripts implementing IPoolable, each in its own
        /// script scope; a throwing script is logged and does not stop the others.
        /// </summary>
        private void NotifyPoolable(Entity entity, bool acquired)
        {
            foreach (ScriptComponent script in entity.GetComponents<ScriptComponent>())
            {
                if (!(script is IPoolable poolable))
                {
                    continue;
                }
                try
                {
                    using (ScriptComponent.Run(script))
                    {
                        if (acquired)
                        {
                            poolable.OnAcquire();
                        }
                        else
                        {
                            poolable.OnRelease();
                        }
                    }
                }
                catch (Exception ex)
                {
                    string call = acquired ? "OnAcquire" : "OnRelease";
                    Log.Error($"{call} of {script.GetType().FullName}: {ex}");
                }
            }
        }

        private void PushInactive(Entity entity)
        {
            inactive_.Push(entity);
            inactiveSet_.Add(entity);
        }

        private Entity TakeOrCreate()
        {
            while (inactive_.Count > 0)
            {
                Entity entity = inactive_.Pop();
                inactiveSet_.Remove(entity);
                if (entity.IsValid())
                {
                    return entity;
                }
                all_.Remove(entity);
            }
            return CreateInactiveInstance();
        }

        private Entity CreateInactiveInstance()
        {
            Entity entity = Scene.Instantiate(prefab_);
            entity.SetActive(false);
            all_.Add(entity);
            return entity;
        }
    }
}
