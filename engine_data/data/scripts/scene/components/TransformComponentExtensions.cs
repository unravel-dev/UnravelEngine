using System.Collections.Generic;

namespace Ace.Core
{
public static class TransformComponentExtensions
{
    /// <summary>
    /// Finds the Entity under this TransformComponent whose Transform has a Bone component
    /// and is closest in world‐space to `point`. Returns null if no such Entity is found.
    /// </summary>
    public static Entity FindClosestBone(this TransformComponent root, Vector3 point)
    {
        Entity closestEntity = root.owner;
        float  minSqrDistance   = float.MaxValue;

        // stack for iterative DFS through the hierarchy
        var stack = new Stack<Entity>();
        stack.Push(root.owner);

        while (stack.Count > 0)
        {
            var entity = stack.Pop();
            var tc     = entity.GetComponent<TransformComponent>();
            var bone   = entity.GetComponent<BoneComponent>();
            
            // if this entity has a Bone, check its distance
            if (bone != null)
            {
                Vector3 worldPos = tc.position;  
                float   d2       = (worldPos - point).sqrMagnitude;
                if (d2 < minSqrDistance)
                {
                    minSqrDistance = d2;
                    closestEntity  = entity;
                }
            }

            // push all children onto the stack
            foreach (var child in tc.children)
                stack.Push(child);
        }

        return closestEntity;
    }
}

}