using System;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// Reuses prefab instances via SetActive instead of Instantiate/Destroy each time.
    /// Suitable for high-churn gameplay objects (enemies, projectiles, pickups, VFX).
    /// </summary>
    public sealed class EntityPool
    {
        private readonly Prefab prefab_;
        private readonly Stack<Entity> inactive_ = new Stack<Entity>();
        private readonly HashSet<Entity> all_ = new HashSet<Entity>();

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
                Entity entity = CreateInactiveInstance();
                inactive_.Push(entity);
            }
        }

        /// <summary>
        /// Takes an entity from the pool (or instantiates one) and activates it.
        /// </summary>
        public Entity Acquire()
        {
            Entity entity = TakeOrCreate();
            entity.SetActive(true);
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
            entity.SetActive(true);
            return entity;
        }

        /// <summary>
        /// Acquires an entity, parents it, then activates it.
        /// </summary>
        public Entity Acquire(Entity parent)
        {
            Entity entity = TakeOrCreate();
            entity.transform.SetParent(parent, false);
            entity.SetActive(true);
            return entity;
        }

        /// <summary>
        /// Returns an entity to the pool. Stops audio on the root if present, then deactivates.
        /// </summary>
        public void Release(Entity entity)
        {
            if (!entity.IsValid() || !all_.Contains(entity))
            {
                return;
            }
            AudioSourceComponent audio = entity.GetComponent<AudioSourceComponent>();
            if (audio != null && audio.isPlaying)
            {
                audio.Stop();
            }
            entity.transform.SetParent(Entity.Invalid, true);
            entity.SetActive(false);
            inactive_.Push(entity);
        }

        /// <summary>
        /// Destroys every entity owned by this pool and clears internal lists.
        /// </summary>
        public void Clear()
        {
            foreach (Entity entity in all_)
            {
                if (entity.IsValid())
                {
                    Scene.DestroyEntityImmediate(entity);
                }
            }
            inactive_.Clear();
            all_.Clear();
        }

        private Entity TakeOrCreate()
        {
            while (inactive_.Count > 0)
            {
                Entity entity = inactive_.Pop();
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
