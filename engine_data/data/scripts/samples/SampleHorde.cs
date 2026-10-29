using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Unravel.Core;

/// <summary>
/// A horde of enemies written entirely in script, as a starting point for games with crowds. Members are rows
/// of arrays, not entities: each frame they walk toward <see cref="target"/>, brake at its edge, push each other
/// apart through a <see cref="SpatialHashGrid"/> and turn toward where they walk, stepped on worker threads with
/// Parallel.For; then they draw with one Graphics.DrawMeshInstanced call. With <see cref="walkClip"/> set the mesh
/// is baked once (<see cref="BakedAnimation"/>) and every member plays its own walk, or its attack while it touches
/// the target. A hurt member flashes <see cref="hurtFlashColor"/>: a material instance of <see cref="material"/>
/// draws the flashing members in a call of their own. Other scripts
/// spawn, find, damage and remove members through its methods; none of them allocates.
/// Main thread only, except the step it runs on worker threads itself.
/// </summary>
public class SampleHorde : ScriptComponent
{
    /// <summary>A member: its row and the generation the row had when it spawned.</summary>
    public struct Member : IEquatable<Member>
    {
        /// <summary>The member's row.</summary>
        public int index;
        /// <summary>The row's generation when the member spawned; a later member of the row has another.</summary>
        public uint generation;

        /// <summary>An id that matches no member.</summary>
        public static Member Invalid => new Member { index = -1 };

        /// <summary>False for <see cref="Invalid"/>, as a failed spawn returns it.</summary>
        public bool isValid => index >= 0;

        /// <summary>True when both ids name the same member.</summary>
        public bool Equals(Member other) => index == other.index && generation == other.generation;

        /// <summary>True when the object is an id naming the same member.</summary>
        public override bool Equals(object obj) => obj is Member other && Equals(other);

        /// <summary>A hash of the id.</summary>
        public override int GetHashCode() => HashCode.Combine(index, generation);
    }

    /// <summary>A member a query or radius damage found.</summary>
    public struct Hit
    {
        /// <summary>The member.</summary>
        public Member member;
        /// <summary>Where it is.</summary>
        public Vector3 position;
        /// <summary>Its distance from the query's centre on the ground plane.</summary>
        public float distance;
    }

    /// <summary>A member that died, or touches the target.</summary>
    public struct Event
    {
        /// <summary>The member.</summary>
        public Member member;
        /// <summary>Where it is.</summary>
        public Vector3 position;
        /// <summary>The value it spawned with.</summary>
        public int userValue;
    }

    /// <summary>What damage did to a member.</summary>
    public enum DamageResult
    {
        /// <summary>The member is not alive.</summary>
        Missed,
        /// <summary>The member took the damage and lives.</summary>
        Hurt,
        /// <summary>The damage killed the member.</summary>
        Killed,
    }

    [Tooltip("The mesh every member draws.")]
    public Mesh mesh;

    [Tooltip("The material of every slot of the mesh; empty uses the mesh's own materials.")]
    public Material material;

    [Tooltip("The entity the members walk to, usually the player.")]
    public Entity target;

    [Tooltip("Most members alive at once. Changing it in play mode drops the members.")]
    public int capacity = 2048;

    [Tooltip("Uniform scale of the mesh.")]
    public float scale = 1.0f;

    [Tooltip("Radius (m) on the ground plane: members keep this far apart, and queries measure to it.")]
    public float radius = 0.4f;

    [Tooltip("Top speed (m/s).")]
    public float maxSpeed = 2.0f;

    [Tooltip("How fast a member changes speed and direction (m/s per second).")]
    public float acceleration = 12.0f;

    [Tooltip("How fast a member turns to face where it walks (degrees per second).")]
    public float turnRate = 540.0f;

    [Tooltip("Health a member spawns with.")]
    public float health = 10.0f;

    [Tooltip("Radius (m) of the target: members stop at its edge and touch it from there.")]
    public float targetRadius = 1.0f;

    [Tooltip("How hard overlapping members push each other apart; 0 lets them overlap.")]
    public float separation = 1.0f;

    [Tooltip("The members cast shadows.")]
    public bool castShadows = true;

    [Tooltip("Steps the members on worker threads.")]
    public bool stepInParallel = true;

    [Tooltip("The clip the members walk with. Set, the mesh (it needs bones) is baked and every member animates.")]
    public AnimationClip walkClip;

    [Tooltip("The clip the members play while they touch the target; empty keeps them walking.")]
    public AnimationClip attackClip;

    [Tooltip("Samples per second of the baked clips: more plays smoother and takes more memory.")]
    public float bakeFramesPerSecond = 30.0f;

    [Tooltip("Retargets the clips rest aligned: clips from another export or of other proportions.")]
    public bool retargetClips = false;

    [Tooltip("Turn (degrees) added to the members' heading, for meshes that do not face +Z.")]
    public float meshYawOffset = 0.0f;

    [Tooltip("Emissive color hurt members flash with, on a material instance of the material; needs a material.")]
    public Color hurtFlashColor = new Color(1.0f, 1.0f, 1.0f, 1.0f);

    [Tooltip("Seconds a hurt member's flash lasts.")]
    public float hurtFlashSeconds = 0.15f;

    private const int STEP_ROWS_PER_TASK = 256;
    private const float OVERLAP_CORRECTION = 0.25f;
    private const float MIN_TURNING_SPEED = 0.05f;
    private const float MIN_SEPARATION_DISTANCE = 1e-4f;
    private const float MIN_RADIUS = 0.01f;
    private const float DEGREES_TO_RADIANS = MathF.PI / 180.0f;
    /// <summary>Fastest the walk clip plays, as a share of its own speed, when a member is pushed faster than it walks.</summary>
    private const float MAX_WALK_RATE = 1.5f;

    private Vector3[] positions;
    private Vector3[] nextPositions;
    private Vector3[] velocities;
    private float[] yaws;
    private float[] healths;
    private int[] userValues;
    private uint[] generations;
    private bool[] alive;
    private bool[] jumped;
    private int[] freeRows;
    private int freeCount;
    private int rowCount;
    private int memberCount;
    private int[] queryRows;
    private Matrix4x4[] rowTransforms;
    private Matrix4x4[] drawTransforms;
    private Matrix4x4[] drawPrevious;
    private int[] animationClips;
    private float[] animationTimes;
    private BakedAnimationState[] rowStates;
    private BakedAnimationState[] drawStates;
    private BakedAnimationState[] drawPreviousStates;
    private float[] flashTimes;
    private MaterialInstance hurtFlash;
    private BakedAnimation baked;
    private bool isBakeFailed;
    private int walkClipIndex = -1;
    private int attackClipIndex = -1;
    private readonly SpatialHashGrid grid = new SpatialHashGrid(1.0f);
    private bool isGridDirty = true;
    private readonly List<Event> pendingDeaths = new List<Event>();
    private readonly List<Event> deaths = new List<Event>();
    private readonly List<Event> contacts = new List<Event>();

    /// <summary>How many members are alive.</summary>
    public int count => memberCount;

    /// <summary>The deaths published by the last step (see <see cref="GetDeaths"/>).</summary>
    public int deathCount => deaths.Count;

    /// <summary>The members touching the target at the last step (see <see cref="GetContacts"/>).</summary>
    public int contactCount => contacts.Count;

    public override void OnUpdate()
    {
        EnsureAnimation();
        Step(Time.deltaTime);
    }

    public override void OnLateUpdate()
    {
        Draw();
    }

    public override void OnDestroy()
    {
        baked?.Dispose();
        baked = null;
        hurtFlash?.Dispose();
        hurtFlash = null;
    }

    /// <summary>Adds a member.</summary>
    /// <param name="position">Where it appears; it keeps this height.</param>
    /// <param name="userValue">Any value the game wants back with the member's death or contact.</param>
    /// <returns>The member, or <see cref="Member.Invalid"/> when the horde is full.</returns>
    public Member Spawn(Vector3 position, int userValue = 0)
    {
        EnsureCapacity();
        int row;
        if (freeCount > 0)
        {
            row = freeRows[--freeCount];
        }
        else if (rowCount < capacity)
        {
            row = rowCount++;
        }
        else
        {
            return Member.Invalid;
        }
        positions[row] = position;
        nextPositions[row] = position;
        velocities[row] = Vector3.zero;
        yaws[row] = 0.0f;
        healths[row] = health;
        userValues[row] = userValue;
        alive[row] = true;
        jumped[row] = true;
        flashTimes[row] = 0.0f;
        // A random phase, so members spawned together do not walk in step.
        animationClips[row] = walkClipIndex;
        animationTimes[row] = baked != null ? Random.Range(0.0f, baked.GetLength(walkClipIndex)) : 0.0f;
        memberCount++;
        isGridDirty = true;
        return MakeId(row);
    }

    /// <summary>Removes a member without a death.</summary>
    /// <returns>False if it is not alive.</returns>
    public bool Despawn(Member member)
    {
        int row = GetRow(member);
        if (row < 0)
        {
            return false;
        }
        Release(row);
        return true;
    }

    /// <summary>Removes a member with a death, reported by <see cref="GetDeaths"/>.</summary>
    /// <returns>False if it is not alive.</returns>
    public bool Kill(Member member)
    {
        int row = GetRow(member);
        if (row < 0)
        {
            return false;
        }
        RecordDeath(row);
        Release(row);
        return true;
    }

    /// <summary>Whether a member is alive.</summary>
    public bool IsAlive(Member member) => GetRow(member) >= 0;

    /// <summary>Gets where a member is.</summary>
    /// <returns>False if it is not alive.</returns>
    public bool TryGetPosition(Member member, out Vector3 position)
    {
        int row = GetRow(member);
        position = row >= 0 ? positions[row] : Vector3.zero;
        return row >= 0;
    }

    /// <summary>Moves a member at once, without travel in between.</summary>
    /// <returns>False if it is not alive.</returns>
    public bool SetPosition(Member member, Vector3 position)
    {
        int row = GetRow(member);
        if (row < 0)
        {
            return false;
        }
        positions[row] = position;
        nextPositions[row] = position;
        jumped[row] = true;
        isGridDirty = true;
        return true;
    }

    /// <summary>Takes health from a member; at zero it dies.</summary>
    public DamageResult Damage(Member member, float amount)
    {
        int row = GetRow(member);
        if (row < 0)
        {
            return DamageResult.Missed;
        }
        healths[row] -= amount;
        if (healths[row] > 0.0f)
        {
            Flash(row);
            return DamageResult.Hurt;
        }
        RecordDeath(row);
        Release(row);
        return DamageResult.Killed;
    }

    /// <summary>Damages every member within a radius of a point on the ground plane.</summary>
    /// <param name="hits">Receives the members hit, as many as fit; may be empty.</param>
    /// <returns>How many members were hit, also beyond the buffer.</returns>
    public int DamageRadius(Vector3 center, float damageRadius, float amount, Span<Hit> hits = default)
    {
        int found = FindRows(center, damageRadius);
        for (int i = 0; i < found; i++)
        {
            int row = queryRows[i];
            if (i < hits.Length)
            {
                hits[i] = MakeHit(row, center);
            }
            healths[row] -= amount;
            if (healths[row] > 0.0f)
            {
                Flash(row);
                continue;
            }
            RecordDeath(row);
            Release(row);
        }
        return found;
    }

    /// <summary>Finds the members within a radius of a point on the ground plane, in no particular order.</summary>
    /// <returns>How many were written, at most the buffer's length.</returns>
    public int Query(Vector3 center, float queryRadius, Span<Hit> hits)
    {
        int found = Math.Min(FindRows(center, queryRadius), hits.Length);
        for (int i = 0; i < found; i++)
        {
            hits[i] = MakeHit(queryRows[i], center);
        }
        return found;
    }

    /// <summary>Finds the member nearest to a point on the ground plane.</summary>
    /// <returns>False if no member is within <paramref name="maxRadius"/>.</returns>
    public bool QueryNearest(Vector3 center, float maxRadius, out Hit hit)
    {
        EnsureGrid();
        var nearest = new NearestVisitor { row = -1, distance = float.MaxValue };
        grid.Visit(center, maxRadius, ref nearest);
        hit = nearest.row >= 0 ? MakeHit(nearest.row, center) : default;
        return nearest.row >= 0;
    }

    /// <summary>
    /// Copies the deaths published by the last step: every member that died between the two steps before it,
    /// so all scripts of a frame read the same list. Kills report themselves at once to their caller.
    /// </summary>
    /// <returns>How many were written, at most the buffer's length.</returns>
    public int GetDeaths(Span<Event> buffer) => Copy(deaths, buffer);

    /// <summary>Copies the members touching the target at the last step.</summary>
    /// <returns>How many were written, at most the buffer's length.</returns>
    public int GetContacts(Span<Event> buffer) => Copy(contacts, buffer);

    /// <summary>Removes every member without deaths.</summary>
    public void Clear()
    {
        positions = null;
        EnsureCapacity();
    }

    private void Step(float dt)
    {
        EnsureCapacity();
        deaths.Clear();
        deaths.AddRange(pendingDeaths);
        pendingDeaths.Clear();
        contacts.Clear();
        EnsureGrid();
        bool hasTarget = target.IsValid();
        Vector3 targetPosition = hasTarget ? target.transform.position : Vector3.zero;
        int taskCount = (rowCount + STEP_ROWS_PER_TASK - 1) / STEP_ROWS_PER_TASK;
        if (stepInParallel && taskCount > 1)
        {
            Parallel.For(0, taskCount, task =>
            {
                int first = task * STEP_ROWS_PER_TASK;
                StepRows(first, Math.Min(first + STEP_ROWS_PER_TASK, rowCount), hasTarget, targetPosition, dt);
            });
        }
        else
        {
            StepRows(0, rowCount, hasTarget, targetPosition, dt);
        }
        (positions, nextPositions) = (nextPositions, positions);
        isGridDirty = true;
        EnsureGrid();
        if (hasTarget)
        {
            FindContacts(targetPosition);
        }
    }

    /// <summary>Moves rows [first, last). Runs on worker threads: it reads every row and writes only its own.</summary>
    private void StepRows(int first, int last, bool hasTarget, Vector3 targetPosition, float dt)
    {
        float memberRadius = MathF.Max(radius, MIN_RADIUS);
        float reach = 2.0f * memberRadius;
        float stopDistance = targetRadius + memberRadius;
        float pushGain = separation * maxSpeed / memberRadius;
        float maxChange = acceleration * dt;
        float maxTurn = turnRate * DEGREES_TO_RADIANS * dt;
        for (int row = first; row < last; row++)
        {
            if (!alive[row])
            {
                nextPositions[row] = positions[row];
                continue;
            }
            Vector3 position = positions[row];
            Vector3 desired = Vector3.zero;
            bool isAtTarget = false;
            if (hasTarget)
            {
                var toTarget = new Vector3(targetPosition.x - position.x, 0.0f, targetPosition.z - position.z);
                float distance = toTarget.magnitude;
                float gap = distance - stopDistance;
                isAtTarget = gap <= 0.0f;
                if (gap > 0.0f)
                {
                    // Brakes in time to stop at the target's edge instead of running into it.
                    float speed = MathF.Min(maxSpeed, MathF.Sqrt(2.0f * acceleration * gap));
                    desired = toTarget * (speed / distance);
                }
            }
            var push = new SeparationVisitor { positions = positions, row = row, position = position, reach = reach };
            if (separation > 0.0f)
            {
                grid.Visit(position, reach, ref push);
                desired += push.push * pushGain;
            }
            Vector3 change = desired - velocities[row];
            float changeLength = change.magnitude;
            velocities[row] += changeLength > maxChange ? change * (maxChange / changeLength) : change;
            Vector3 next = position + velocities[row] * dt + push.push * (OVERLAP_CORRECTION * MathF.Min(separation, 1.0f));
            next.y = position.y;
            nextPositions[row] = next;
            Vector3 velocity = velocities[row];
            float walkSpeed = velocity.magnitude;
            if (walkSpeed > MIN_TURNING_SPEED)
            {
                // The shortest turn toward the direction of travel, in [-pi, pi].
                float turn = MathF.IEEERemainder(MathF.Atan2(velocity.x, velocity.z) - yaws[row], 2.0f * MathF.PI);
                yaws[row] += Math.Clamp(turn, -maxTurn, maxTurn);
            }
            if (baked != null)
            {
                AdvanceAnimation(row, isAtTarget, walkSpeed, dt);
            }
        }
    }

    /// <summary>
    /// Plays the attack clip while the member touches the target, else the walk clip at the pace it walks.
    /// Runs on worker threads: it writes only the member's own row.
    /// </summary>
    private void AdvanceAnimation(int row, bool isAtTarget, float walkSpeed, float dt)
    {
        int clip = isAtTarget && attackClipIndex >= 0 ? attackClipIndex : walkClipIndex;
        if (clip != animationClips[row])
        {
            animationClips[row] = clip;
            animationTimes[row] = 0.0f;
        }
        float rate = clip == walkClipIndex ? MathF.Min(walkSpeed / MathF.Max(maxSpeed, MIN_TURNING_SPEED), MAX_WALK_RATE) : 1.0f;
        float length = baked.GetLength(clip);
        animationTimes[row] = length > 0.0f ? (animationTimes[row] + dt * rate) % length : 0.0f;
    }

    /// <summary>Bakes the clips the first time a frame needs them; a mesh without bones keeps drawing without them.</summary>
    private void EnsureAnimation()
    {
        if (baked != null || isBakeFailed || walkClip == null || mesh == null)
        {
            return;
        }
        try
        {
            AnimationClip[] clips = attackClip != null ? new[] { walkClip, attackClip } : new[] { walkClip };
            var retarget = retargetClips ? BakedAnimationRetarget.RestAligned : BakedAnimationRetarget.Copy;
            baked = BakedAnimation.Bake(mesh, clips, bakeFramesPerSecond, retarget);
            walkClipIndex = 0;
            attackClipIndex = attackClip != null ? 1 : -1;
            for (int row = 0; row < rowCount; row++)
            {
                animationClips[row] = walkClipIndex;
                animationTimes[row] = Random.Range(0.0f, baked.GetLength(walkClipIndex));
            }
        }
        catch (ArgumentException e)
        {
            isBakeFailed = true;
            Log.Warning($"SampleHorde draws its members without animation: {e.Message}");
        }
    }

    private void FindContacts(Vector3 targetPosition)
    {
        int found = FindRows(targetPosition, targetRadius + radius);
        for (int i = 0; i < found; i++)
        {
            int row = queryRows[i];
            contacts.Add(new Event { member = MakeId(row), position = positions[row], userValue = userValues[row] });
        }
    }

    private void Flash(int row)
    {
        flashTimes[row] = hurtFlashSeconds;
    }

    private void Draw()
    {
        if (mesh == null || memberCount == 0)
        {
            return;
        }
        bool canFlash = EnsureHurtFlash();
        // Members that do not flash fill the draw arrays from the front, flashing ones from the back: the flashing
        // ones draw with the flash material instance, the others with the material.
        int plainCount = 0;
        int flashingCount = 0;
        int end = drawTransforms.Length;
        float yawOffset = meshYawOffset * DEGREES_TO_RADIANS;
        float dt = Time.deltaTime;
        for (int row = 0; row < rowCount; row++)
        {
            if (!alive[row])
            {
                continue;
            }
            bool isFlashing = canFlash && flashTimes[row] > 0.0f;
            int slot = isFlashing ? end - 1 - flashingCount++ : plainCount++;
            flashTimes[row] = MathF.Max(flashTimes[row] - dt, 0.0f);
            Matrix4x4 world = MakeTransform(positions[row], yaws[row] + yawOffset, scale);
            var state = new BakedAnimationState(animationClips[row], animationTimes[row]);
            drawPrevious[slot] = jumped[row] ? world : rowTransforms[row];
            drawTransforms[slot] = world;
            drawPreviousStates[slot] = jumped[row] ? state : rowStates[row];
            drawStates[slot] = state;
            rowTransforms[row] = world;
            rowStates[row] = state;
            jumped[row] = false;
        }
        DrawMembers(0, plainCount, null);
        DrawMembers(end - flashingCount, flashingCount, hurtFlash);
    }

    /// <summary>
    /// Creates the flash material instance once there is a material, recreates it when the material changes, and
    /// keeps its color current.
    /// </summary>
    /// <returns>False without a material: members do not flash.</returns>
    private bool EnsureHurtFlash()
    {
        if (material == null)
        {
            return false;
        }
        if (hurtFlash != null && hurtFlash.sharedMaterial != material)
        {
            hurtFlash.Dispose();
            hurtFlash = null;
        }
        if (hurtFlash == null)
        {
            hurtFlash = new MaterialInstance(material);
        }
        hurtFlash.emissiveColor = hurtFlashColor;
        return true;
    }

    /// <summary>Draws the members in draw slots [first, first + count) with <paramref name="materialInstance"/>.</summary>
    private void DrawMembers(int first, int count, MaterialInstance materialInstance)
    {
        if (count == 0)
        {
            return;
        }
        if (baked != null && baked.isValid)
        {
            Graphics.DrawMeshInstanced(baked,
                                       material,
                                       drawTransforms.AsSpan(first, count),
                                       drawStates.AsSpan(first, count),
                                       drawPrevious.AsSpan(first, count),
                                       drawPreviousStates.AsSpan(first, count),
                                       castShadows,
                                       materialInstance: materialInstance);
            return;
        }
        Graphics.DrawMeshInstanced(mesh,
                                   material,
                                   drawTransforms.AsSpan(first, count),
                                   drawPrevious.AsSpan(first, count),
                                   castShadows,
                                   materialInstance: materialInstance);
    }

    private static Matrix4x4 MakeTransform(Vector3 position, float yaw, float scale)
    {
        float cos = MathF.Cos(yaw) * scale;
        float sin = MathF.Sin(yaw) * scale;
        return new Matrix4x4(new Vector4(cos, 0.0f, -sin, 0.0f),
                             new Vector4(0.0f, scale, 0.0f, 0.0f),
                             new Vector4(sin, 0.0f, cos, 0.0f),
                             new Vector4(position.x, position.y, position.z, 1.0f));
    }

    private void EnsureCapacity()
    {
        if (positions != null && positions.Length == capacity)
        {
            return;
        }
        int size = Math.Max(capacity, 1);
        positions = new Vector3[size];
        nextPositions = new Vector3[size];
        velocities = new Vector3[size];
        yaws = new float[size];
        healths = new float[size];
        userValues = new int[size];
        generations = new uint[size];
        alive = new bool[size];
        jumped = new bool[size];
        freeRows = new int[size];
        queryRows = new int[size];
        rowTransforms = new Matrix4x4[size];
        drawTransforms = new Matrix4x4[size];
        drawPrevious = new Matrix4x4[size];
        animationClips = new int[size];
        animationTimes = new float[size];
        rowStates = new BakedAnimationState[size];
        drawStates = new BakedAnimationState[size];
        drawPreviousStates = new BakedAnimationState[size];
        flashTimes = new float[size];
        freeCount = 0;
        rowCount = 0;
        memberCount = 0;
        pendingDeaths.Clear();
        deaths.Clear();
        contacts.Clear();
        isGridDirty = true;
    }

    private void EnsureGrid()
    {
        EnsureCapacity();
        if (!isGridDirty)
        {
            return;
        }
        grid.cellSize = 2.0f * MathF.Max(radius, MIN_RADIUS);
        grid.Build(positions.AsSpan(0, rowCount), alive.AsSpan(0, rowCount));
        isGridDirty = false;
    }

    private int FindRows(Vector3 center, float queryRadius)
    {
        EnsureGrid();
        return grid.Query(center, queryRadius, queryRows);
    }

    private Hit MakeHit(int row, Vector3 center)
    {
        float dx = positions[row].x - center.x;
        float dz = positions[row].z - center.z;
        return new Hit { member = MakeId(row), position = positions[row], distance = MathF.Sqrt(dx * dx + dz * dz) };
    }

    private Member MakeId(int row) => new Member { index = row, generation = generations[row] };

    private int GetRow(Member member)
    {
        EnsureCapacity();
        int row = member.index;
        return row >= 0 && row < rowCount && alive[row] && generations[row] == member.generation ? row : -1;
    }

    private void Release(int row)
    {
        alive[row] = false;
        generations[row]++;
        freeRows[freeCount++] = row;
        memberCount--;
        isGridDirty = true;
    }

    private void RecordDeath(int row)
    {
        pendingDeaths.Add(new Event { member = MakeId(row), position = positions[row], userValue = userValues[row] });
    }

    private static int Copy(List<Event> source, Span<Event> buffer)
    {
        int written = Math.Min(source.Count, buffer.Length);
        for (int i = 0; i < written; i++)
        {
            buffer[i] = source[i];
        }
        return written;
    }

    /// <summary>Sums how far the neighbours of one member overlap it, as a push away from them.</summary>
    private struct SeparationVisitor : ISpatialHashVisitor
    {
        public Vector3[] positions;
        public int row;
        public Vector3 position;
        public float reach;
        public Vector3 push;

        public void Visit(int other, float distance)
        {
            if (other == row || distance >= reach)
            {
                return;
            }
            Vector3 away = distance > MIN_SEPARATION_DISTANCE
                               ? new Vector3(position.x - positions[other].x, 0.0f, position.z - positions[other].z) / distance
                               : GetTieDirection(row, other);
            push += away * (reach - distance);
        }

        /// <summary>A fixed direction for two members at the same spot, so they separate the same way every run.</summary>
        private static Vector3 GetTieDirection(int row, int other)
        {
            float angle = (((uint)row * 2654435761u) ^ ((uint)other * 40503u)) * (2.0f * MathF.PI / 4294967296.0f);
            return new Vector3(MathF.Cos(angle), 0.0f, MathF.Sin(angle));
        }
    }

    /// <summary>Keeps the nearest item a query visits.</summary>
    private struct NearestVisitor : ISpatialHashVisitor
    {
        public int row;
        public float distance;

        public void Visit(int item, float itemDistance)
        {
            if (itemDistance < distance)
            {
                distance = itemDistance;
                row = item;
            }
        }
    }
}
