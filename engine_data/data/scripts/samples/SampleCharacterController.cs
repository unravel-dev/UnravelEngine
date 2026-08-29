using System;
using Unravel.Core;

/// <summary>
/// Production-grade third-person character controller.
///
/// Uses the built-in <see cref="CharacterControllerComponent"/> for capsule-based
/// sweep movement, gravity and collisions (no manual rigidbody integration), and
/// layers on top:
///   - Acceleration / deceleration toward target speed (walk / run / sprint)
///   - Coyote time + jump buffering for forgiving jump feel
///   - Rotation toward movement direction with in-place turning
///   - Turn damping (speed scaling when changing direction sharply)
///   - Foot IK with hips dipping to match uneven ground
///   - Spine look-at IK + two-handed aim IK driven by the cursor
///
/// Shooting/ability logic lives in the separate <c>CharacterShooter</c> component
/// so movement can be reused across non-combat characters (NPCs, cutscene actors).
///
/// The script auto-resolves common bone names (Mixamo rig by default). Drop this
/// onto any humanoid rig with a <see cref="CharacterControllerComponent"/> and it
/// will work out of the box; override the bone slots in the inspector for
/// non-Mixamo rigs.
/// </summary>
[ScriptSourceFile]
public class SampleCharacterController : ScriptComponent
{
    // ---------------------------------------------------------------------
    // Movement configuration
    // ---------------------------------------------------------------------
    [Header("Movement")]
    [Tooltip("Animation-tier threshold for the 'walk' blend, and the cap applied while moving backwards. " +
             "The character does NOT stop at this speed when walking forward - it accelerates straight " +
             "through it on its way to RunSpeed.")]
    public float WalkSpeed = 2.0f;

    [Tooltip("Default target speed when forward input is held and the Run axis is 0. " +
             "The character will accelerate smoothly from 0 up to this value, passing through walk " +
             "and (optional) jog animation tiers on the way.")]
    public float RunSpeed = 5.0f;

    [Tooltip("Target speed when forward input is held and the Run axis is fully engaged. " +
             "Enables the 'sprint' animation tier above RunSpeed.")]
    public float SprintSpeed = 7.5f;

    [Tooltip("Rate at which the character accelerates toward target speed (units/s^2).")]
    public float Acceleration = 16.0f;

    [Tooltip("Rate at which the character decelerates toward target speed (units/s^2).")]
    public float Deceleration = 20.0f;

    [Tooltip("Air-control multiplier applied to acceleration while not grounded (0 = no air control, 1 = full).")]
    [Range(0, 1)]
    public float AirControl = 0.35f;

    [Tooltip("Preserve horizontal momentum while airborne: input can steer direction but will not brake the character. " +
             "Matches most production third-person action games (sprint-jump keeps its speed even if sprint is released mid-air).")]
    public bool PreserveAirMomentum = true;

    [Tooltip("Rate (1/s) at which the Run axis is smoothed when RAMPING UP. Keyboard binds this axis to 0/1 with no " +
             "travel, so without smoothing sprint engages instantly. Analog triggers / sticks are unaffected because the " +
             "smoother just tracks them. Releases always snap to 0 instantly so that letting go of sprint, stopping, and " +
             "re-pressing move does NOT spuriously re-enter sprint (the stale smoothed value would otherwise still be " +
             "above the sprint threshold for ~200ms after release). 2 = ~0.5s walk-to-run ramp, 4 = ~0.25s, 0 = instant.")]
    [Min(0)]
    public float RunSmoothing = 2.5f;

    [Header("Rotation")]
    [Tooltip("Turn-to-velocity responsiveness when walking (higher = snappier).")]
    public float RotateSpeed = 8.0f;

    [Tooltip("Turn-to-velocity responsiveness when running (higher = snappier).")]
    public float RunRotateSpeed = 12.0f;

    [Tooltip("In-place rotation speed (degrees per second) when only strafe input is held.")]
    public float InPlaceTurnSpeed = 120.0f;

    [Tooltip("Horizontal input threshold above which in-place turning is detected.")]
    [Range(0, 1)]
    public float InPlaceTurnThreshold = 0.2f;

    // ---------------------------------------------------------------------
    // Turn damping (speed scaling while turning)
    // ---------------------------------------------------------------------
    [Header("Turn Damping")]
    [Tooltip("If true, caps target speed based on how far the desired move direction is from the current " +
             "forward. Prevents unrealistic high-speed U-turns, mitigates foot-sliding on the run/sprint " +
             "clips, and lets the animator naturally drop into jog/walk during sharp arcs.")]
    public bool EnableTurnDamping = true;

    [Tooltip("Turn angle (degrees from forward) below which no speed damping is applied. Within this cone " +
             "the character runs at full target speed. Too small = twitchy damping on micro-turns.")]
    [Range(0, 180)]
    public float TurnDampStartAngle = 30.0f;

    [Tooltip("Turn angle (degrees from forward) at which speed is clamped to MinTurnSpeedScale * targetSpeed. " +
             "Between Start and End the scale lerps smoothly. 180 = only full reversals hit the minimum.")]
    [Range(0, 180)]
    public float TurnDampEndAngle = 150.0f;

    [Tooltip("Minimum target-speed multiplier when the desired direction is fully opposite forward. " +
             "0.4 = character slows to 40% of the uncapped target during a 180° turn.")]
    [Range(0, 1)]
    public float MinTurnSpeedScale = 0.4f;

    // ---------------------------------------------------------------------
    // Jump configuration
    // ---------------------------------------------------------------------
    [Header("Jump")]
    [Tooltip("Initial upward speed (m/s) applied when the character jumps.")]
    public float JumpSpeed = 2.0f;

    [Tooltip("Duration (seconds) after leaving ground during which a jump is still allowed.")]
    public float CoyoteTime = 0.15f;

    [Tooltip("Duration (seconds) to buffer a jump input before landing.")]
    public float JumpBuffer = 0.10f;

    // ---------------------------------------------------------------------
    // IK configuration
    // ---------------------------------------------------------------------
    [Header("Foot IK")]
    [Tooltip("Whether foot and hip IK should be evaluated.")]
    public bool ApplyFootIK = true;

    [Tooltip("Unused by two-bone foot IK (always hip-knee-foot). Kept for inspector compatibility.")]
    public int FootChainLength = 2;

    [Tooltip("Foot height above the character root at which foot IK starts blending out.")]
    public float FootHeightMin = 0.14f;

    [Tooltip("Foot height above the character root at which foot IK has fully blended out.")]
    public float FootHeightMax = 0.22f;

    [Tooltip("Vertical offset above the foot from which to start the ground raycast.")]
    public float FootRaycastOffset = 0.5f;

    [Tooltip("Maximum downward distance the foot raycast will look for ground.")]
    public float FootRaycastDistance = 1.0f;

    [Tooltip("Additional vertical offset applied to the foot IK target for foot thickness.")]
    public float FootTargetYOffset = 0.1f;

    [Tooltip("Ground drop below the animated foot at which foot IK starts fading out for that foot. " +
             "Lets a touchdown foot conform to a slope while keeping a swing foot from being pulled " +
             "onto ground far below it.")]
    public float FootDropStart = 0.15f;

    [Tooltip("Ground drop below the animated foot at which foot IK is fully off for that foot.")]
    public float FootDropMax = 0.3f;

    [Tooltip("Maximum distance the hips may dip to help a leg reach lower ground.")]
    public float MaxHipsDrop = 0.3f;

    [Tooltip("Sideways offset of each leg's knee pole (positive = knees bias outward). " +
             "Prevents the knees from collapsing toward each other on slopes.")]
    public float KneePoleOutwardBias = 0.15f;

    [Tooltip("Smoothing factor for the hips dip offset (0 = no smoothing, 1 = snap).")]
    [Range(0, 1)]
    public float HipsSmoothing = 0.15f;

    [Header("Aim IK")]
    [Tooltip("Whether spine / hand IK should be evaluated when aiming.")]
    public bool ApplyAimIK = true;

    [Tooltip("When true the right mouse button engages aim / spine IK.")]
    public bool AimWithRightMouse = true;

    [Tooltip("Maximum raycast distance used to resolve the aim point.")]
    public float AimMaxDistance = 500.0f;

    [Range(0, 5)]
    [Tooltip("Rate at which spine IK weight ramps in/out when aiming.")]
    public float SpineIKSpeed = 5.0f;

    [Range(0, 5)]
    [Tooltip("Rate at which hand IK weight ramps in/out when aiming.")]
    public float HandIKSpeed = 5.0f;

    [Range(0, 90)]
    [Tooltip("Aim angle (degrees) at which the off-hand IK begins to fade out.")]
    public float HandSwitchStartAngle = 20.0f;

    [Range(0, 90)]
    [Tooltip("Aim angle (degrees) at which the off-hand IK has fully faded out.")]
    public float HandSwitchEndAngle = 30.0f;

    [Tooltip("Maximum deviation (degrees) from forward allowed for spine / hand aim IK.")]
    [Range(0, 180)]
    public float MaxAimForwardAngle = 110.0f;

    [Tooltip("Unused by two-bone arm IK (always shoulder-elbow-hand). Kept for inspector compatibility.")]
    public int HandChainLength = 2;

    [Tooltip("Ask the rig at startup which of the spine bone's local axes currently points " +
             "along the character's forward, and aim along that. Rigs disagree about this " +
             "and importers do not normalise it, so deriving it beats guessing. Uncheck to " +
             "use SpineAimAxis exactly as authored.")]
    public bool AutoResolveSpineAimAxis = true;

    [Tooltip("Bone-local axis of the spine that should FACE the aim target, used when " +
             "AutoResolveSpineAimAxis is off. Note this is NOT the axis the bone runs along " +
             "(local +Y on Mixamo rigs): aiming a spine along its own length points the " +
             "torso at the target and folds the character in half.")]
    public Vector3 SpineAimAxis = Vector3.forward;

    // ---------------------------------------------------------------------
    // Bone references
    // ---------------------------------------------------------------------
    [Header("Bones (auto-resolved for Mixamo rigs)")]
    public Entity Hips;
    public Entity Spine;
    public Entity LeftFoot;
    public Entity RightFoot;
    public Entity LeftHand;
    public Entity RightHand;

    // ---------------------------------------------------------------------
    // Public runtime state (consumed by animator / UI / other systems)
    // ---------------------------------------------------------------------
    public float CurrentSpeed        { get; private set; } = 0.0f;
    public float CurrentRotateSpeed  { get; private set; } = 0.0f;
    public bool  IsGrounded          { get; private set; } = true;
    public bool  IsMoving             { get; private set; } = false;
    public bool  IsBackwards          { get; private set; } = false;
    public bool  IsRunning            { get; private set; } = false;
    public bool  IsRotatingInPlace    { get; private set; } = false;
    public bool  IsAiming             { get; private set; } = false;
    public float TurningInPlaceDirection { get; private set; } = 0.0f;

    // ---------------------------------------------------------------------
    // Internal state
    // ---------------------------------------------------------------------
    private CharacterControllerComponent cc;
    private CameraComponent mainCamera;

    private Vector3 horizontalVelocity = Vector3.zero;
    private float smoothedRunInput = 0.0f;
    private float timeSinceUngrounded = 0.0f;
    private float timeSinceJumpPressed = float.MaxValue;

    private float leftFootBlend = 1.0f;
    private float rightFootBlend = 1.0f;
    private float hipsOffset = 0.0f;

    private float spineIKWeight = 0.0f;
    private float handIKWeight = 0.0f;
    private float leftHandWeight = 0.0f;
    private float rightHandWeight = 0.0f;

    // ---------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------
    public override void OnStart()
    {
        cc = owner.GetComponent<CharacterControllerComponent>();
        if (cc == null)
        {
            Log.Error($"{nameof(SampleCharacterController)} requires a {nameof(CharacterControllerComponent)} on the same entity.");
            return;
        }

        ResolveBones();
        ResolveCamera();

        // Which local axis is the chest's "front" is a property of how the rig was
        // authored, not of the engine, so read it off the rig in its rest pose
        // rather than assuming a convention.
        if (AutoResolveSpineAimAxis && Spine.IsValid())
            SpineAimAxis = IK.GetFacingAxis(Spine, transform.forward);
    }

    public override void OnUpdate()
    {
        if (cc == null)
            return;

        float dt = Time.deltaTime;

        // Ground state comes from the character controller itself; no need for
        // manual sphere-casts.
        IsGrounded = cc.isGrounded;

        UpdateTimers(dt);

        Vector2 moveInput = ReadMoveInput();
        float runInput = Input.GetAxis("Run");

        UpdateMovement(moveInput, runInput, dt);
        UpdateRotation(moveInput, dt);
        TryJump();

        if (ApplyFootIK)
        {
            UpdateFootIK(dt);
        }

        if (ApplyAimIK)
        {
            UpdateAimIK(dt);
        }
    }

    // ---------------------------------------------------------------------
    // Input / timers
    // ---------------------------------------------------------------------
    private Vector2 ReadMoveInput()
    {
        return new Vector2(Input.GetAxis("Horizontal"), Input.GetAxis("Vertical"));
    }

    private void UpdateTimers(float dt)
    {
        if (IsGrounded)
        {
            timeSinceUngrounded = 0.0f;
        }
        else
        {
            timeSinceUngrounded += dt;
        }

        if (Input.IsPressed("Jump"))
        {
            timeSinceJumpPressed = 0.0f;
        }
        else
        {
            timeSinceJumpPressed += dt;
        }
    }

    // ---------------------------------------------------------------------
    // Movement
    // ---------------------------------------------------------------------
    private void UpdateMovement(Vector2 moveInput, float runInput, float dt)
    {
        // Only forward/back input counts as locomotion. Strafe-only input is
        // reserved for turning in place and must produce NO translation,
        // otherwise the character slides sideways every frame as the turn
        // rotates the strafe vector around the world.
        bool hasForward = Mathf.Abs(moveInput.y) > 0.01f;
        bool hasStrafe  = Mathf.Abs(moveInput.x) > InPlaceTurnThreshold;
        bool strafeOnly = hasStrafe && !hasForward;

        IsMoving = hasForward;

        // "Backwards" is decided purely by the forward axis. Strafe input (A/D)
        // while holding back is interpreted as STEERING (rotation about the up
        // axis), not as translation, so S+A walks the character straight
        // backward in local space and rotates it CCW - the same way a tank or
        // car reverses.
        //
        // Input-driven (not velocity-driven): without this, S+A produces a
        // diagonal world velocity, UpdateRotation steers forward toward that
        // velocity, the character rotates, the rotated local "-Z" now points
        // somewhere else, the velocity chases, rotation chases - runaway spin
        // at many times the in-place turn rate.
        //
        // During the deceleration tail (no input but velocity hasn't yet hit
        // zero) we LATCH the previous value so the post-stop tail can't drive
        // rotation.
        bool backwardHeld = moveInput.y < -0.01f;
        if (IsMoving)
        {
            IsBackwards = backwardHeld;
        }
        else if (horizontalVelocity.sqrMagnitude < 0.0001f)
        {
            IsBackwards = false;
        }
        // else: decelerating from a previous frame - keep IsBackwards latched.

        // Backwards input disables sprinting so the character never "sprints" backwards.
        if (IsBackwards)
        {
            runInput = 0.0f;
        }

        // Smooth the raw run axis so keyboard (binary) input doesn't snap
        // from run to sprint in a single frame. Analog sources (gamepad
        // triggers / sticks) already produce a continuous value and will
        // simply be tracked by the smoother.
        //
        // Asymmetric on purpose: we only smooth when RAMPING UP. Releases
        // snap to 0 immediately, because a symmetric ramp-down would cause
        // "stop-then-immediately-move-again" to re-enter sprint using the
        // stale smoothed value (it stays above the 0.5 sprint threshold for
        // ~200ms after Shift is released).
        if (runInput > smoothedRunInput && RunSmoothing > 0.0f)
            smoothedRunInput = Mathf.MoveTowards(smoothedRunInput, runInput, RunSmoothing * dt);
        else
            smoothedRunInput = runInput;

        IsRunning = smoothedRunInput > 0.5f;

        // Target forward speed.
        //   * Forward:   RunSpeed → SprintSpeed based on (smoothed) Run axis.
        //   * Backwards: capped at WalkSpeed regardless of Run axis.
        // The character always accelerates from 0 up to this target, so it
        // naturally passes through walk / jog / run animation tiers on the way.
        float targetSpeed = IsBackwards
            ? WalkSpeed
            : Mathf.Lerp(RunSpeed, SprintSpeed, smoothedRunInput);
        float targetRotateSpeed = Mathf.Lerp(RotateSpeed, RunRotateSpeed, smoothedRunInput);

        // Build the local-space move direction. When moving backward the
        // strafe axis is reserved for STEERING (see UpdateRotation), not
        // translation, so we zero it here - otherwise a diagonal velocity
        // would fight the rotator for control of forward and cause runaway
        // spin (see IsBackwards comment above).
        Vector3 targetVel = Vector3.zero;
        if (IsMoving)
        {
            Vector3 inputDirLocal = new Vector3(IsBackwards ? 0.0f : moveInput.x,
                                                0.0f,
                                                moveInput.y);
            if (inputDirLocal.sqrMagnitude > 1.0f)
                inputDirLocal = inputDirLocal.normalized;
            Vector3 inputDirWorld = transform.TransformDirection(inputDirLocal);

            // Turn damping: cap target speed based on how far the desired direction
            // is from the character's current forward. Applied only while grounded
            // and moving forward, so air-momentum and backwards walk are untouched.
            // The animator naturally reacts to the reduced CurrentSpeed by dropping
            // into a lower tier (jog -> walk) during a tight arc.
            float speedScale = 1.0f;
            if (EnableTurnDamping && IsGrounded && !IsBackwards)
            {
                Vector3 planarInput = new Vector3(inputDirWorld.x, 0.0f, inputDirWorld.z);
                if (planarInput.sqrMagnitude > 1e-6f)
                {
                    Vector3 fwd = transform.forward; fwd.y = 0.0f;
                    if (fwd.sqrMagnitude > 1e-6f)
                    {
                        float angle = Vector3.Angle(fwd.normalized, planarInput.normalized);
                        float t = Mathf.InverseLerp(TurnDampStartAngle, TurnDampEndAngle, angle);
                        speedScale = Mathf.Lerp(1.0f, MinTurnSpeedScale, t);
                    }
                }
            }

            targetVel = inputDirWorld * (targetSpeed * speedScale);
        }

        float accel = (targetVel.sqrMagnitude > horizontalVelocity.sqrMagnitude) ? Acceleration : Deceleration;
        if (!IsGrounded)
            accel *= AirControl;

        if (!IsGrounded && PreserveAirMomentum)
        {
            // In the air we preserve horizontal momentum: the player can steer
            // the direction of motion toward the desired input, but cannot
            // slow the character down by releasing / reversing input. This
            // keeps sprint-jumps honest and feels natural in third-person games.
            horizontalVelocity = SteerAirVelocity(horizontalVelocity, targetVel, accel * dt);
        }
        else
        {
            horizontalVelocity = Vector3.MoveTowards(horizontalVelocity, targetVel, accel * dt);
        }

        // Snap tiny residual velocity to zero so we don't feed micro-displacements
        // to the character controller (which would read as drift on the CC side).
        if (!IsMoving && horizontalVelocity.sqrMagnitude < 1e-4f)
        {
            horizontalVelocity = Vector3.zero;
        }

        CurrentSpeed       = new Vector2(horizontalVelocity.x, horizontalVelocity.z).magnitude;
        CurrentRotateSpeed = Mathf.MoveTowards(CurrentRotateSpeed, targetRotateSpeed, accel * dt);

        // Hand the displacement to the character controller. The CC integrates
        // gravity and collisions internally; we only supply horizontal motion.
        cc.Move(horizontalVelocity * dt);
    }

    private void UpdateRotation(Vector2 moveInput, float dt)
    {
        IsRotatingInPlace = false;
        TurningInPlaceDirection = 0.0f;

        bool hasStrafe  = Mathf.Abs(moveInput.x) > InPlaceTurnThreshold;
        bool hasForward = Mathf.Abs(moveInput.y) > InPlaceTurnThreshold;

        if (hasStrafe && !hasForward)
        {
            IsRotatingInPlace = true;
            TurningInPlaceDirection = Mathf.Sign(moveInput.x);

            float angle = TurningInPlaceDirection * InPlaceTurnSpeed * dt;
            transform.RotateByEuler(Vector3.up * angle);
            return;
        }

        // Reverse steering: while walking backward, A/D rotates the character
        // at the in-place rate without tagging IsRotatingInPlace. The animator
        // therefore keeps playing walkBackwards (rather than the turn clips),
        // and the rotation rate matches pure in-place turning so it doesn't
        // feel faster than standing-still rotation.
        if (IsBackwards && hasStrafe)
        {
            float angle = Mathf.Sign(moveInput.x) * InPlaceTurnSpeed * dt;
            transform.RotateByEuler(Vector3.up * angle);
            return;
        }

        if (IsBackwards)
            return;

        Vector3 planar = new Vector3(horizontalVelocity.x, 0.0f, horizontalVelocity.z);
        if (planar.sqrMagnitude < 0.0001f)
            return;

        Quaternion target = Quaternion.LookRotation(planar.normalized, Vector3.up);
        float alpha = Mathf.Clamp01(CurrentRotateSpeed * dt);
        transform.rotation = Quaternion.Slerp(transform.rotation, target, alpha);
    }

    private void TryJump()
    {
        bool canJumpNow = cc.canJump || timeSinceUngrounded < CoyoteTime;
        bool jumpBuffered = timeSinceJumpPressed < JumpBuffer;
        if (canJumpNow && jumpBuffered)
        {
            cc.Jump(JumpSpeed);
            timeSinceJumpPressed = float.MaxValue;
            timeSinceUngrounded = float.MaxValue;
        }
    }

    // ---------------------------------------------------------------------
    // Foot IK
    // ---------------------------------------------------------------------

    /// <summary>
    /// A resolved ground contact for one foot: where to put it, how to orient it,
    /// and how strongly to apply both.
    /// </summary>
    private struct FootGoal
    {
        public Vector3 position;
        public Vector3 normal;
        public float verticalOffset;
        public float weight;
    }

    private void UpdateFootIK(float dt)
    {
        if (!Hips.IsValid())
            return;

        bool leftHit = ProcessFoot(LeftFoot, ref leftFootBlend, dt, out FootGoal left);
        bool rightHit = ProcessFoot(RightFoot, ref rightFootBlend, dt, out FootGoal right);

        // Lower the hips by the deepest foot drop so bent knees look correct
        // on steps and slopes. Clamped so a foot over a long drop can never
        // fold the character - past the clamp that foot's IK weight has faded
        // out anyway (see the drop gate in ProcessFoot).
        float targetOffset = 0.0f;
        if (leftHit) targetOffset = Mathf.Min(targetOffset, left.verticalOffset);
        if (rightHit) targetOffset = Mathf.Min(targetOffset, right.verticalOffset);
        targetOffset = Mathf.Max(targetOffset, -MaxHipsDrop);
        hipsOffset = Mathf.Lerp(hipsOffset, targetOffset, SmoothingAlpha(HipsSmoothing, dt));

        Vector3 hipsPos = Hips.transform.position;
        Hips.transform.position = new Vector3(hipsPos.x, hipsPos.y + hipsOffset, hipsPos.z);

        if (leftHit) ApplyFootGoal(LeftFoot, left, -1.0f);
        if (rightHit) ApplyFootGoal(RightFoot, right, 1.0f);
    }

    // Knee pole geometry: a point in front of the pelvis at roughly knee
    // height. See the pole comment in ApplyFootGoal.
    private const float KneePoleForward = 1.0f;
    private const float KneePoleDown = 0.35f;

    private void ApplyFootGoal(Entity foot, FootGoal goal, float side)
    {
        if (!foot.IsValid() || goal.weight <= 0.0f)
            return;

        // Pole anchored at the pelvis, never at the foot: the solver flattens
        // the pole against the hip-to-target axis, and on a slope that axis
        // tilts forward - a foot-derived pole then loses its forward part and
        // its leftover inward lean (feet travel inboard of the hip joints)
        // steers BOTH knees toward the midline until the thighs intersect.
        // A pelvis anchor with an explicit outward bias keeps each knee
        // bending forward and slightly out on any terrain.
        Vector3 kneePole = Hips.transform.position
                         + transform.forward * KneePoleForward
                         + transform.right * (side * KneePoleOutwardBias)
                         - transform.up * KneePoleDown;

        // Two-bone rather than FABRIK: hip-knee-foot is exactly the analytical
        // case, so it lands on the target in a single pass instead of iterating
        // toward it, and it cannot pick a different bend from one frame to the next.
        if (!IK.SetIKPositionTwoBone(foot, goal.position, kneePole, goal.weight))
            return;

        // A position solve orients the thigh and shin, never the foot itself, so
        // on its own the foot keeps the flat-ground orientation animation gave it
        // and buries its toe in any slope. Tilt that animated orientation onto
        // the ground normal.
        Quaternion align = Quaternion.FromToRotation(Vector3.up, goal.normal);
        IK.SetIKRotation(foot, align * foot.transform.rotation, goal.weight);
    }

    private bool ProcessFoot(Entity foot, ref float blend, float dt, out FootGoal goal)
    {
        goal = new FootGoal();

        if (!foot.IsValid())
            return false;

        blend = Mathf.Clamp01(blend + (IsGrounded ? dt : -dt));

        Vector3 footPos = foot.transform.position;
        float relativeHeight = footPos.y - transform.position.y;
        float heightBlend = 1.0f - Mathf.InverseLerp(FootHeightMin, FootHeightMax, relativeHeight);
        float effectiveBlend = blend * heightBlend;

        if (effectiveBlend <= 0.0f)
            return false;

        Ray ray;
        ray.origin = footPos + Vector3.up * FootRaycastOffset;
        ray.direction = Vector3.down;

        RaycastHit? hit = Physics.Raycast(ray, FootRaycastDistance + FootRaycastOffset, cc.collisionLayers, false);
        if (!hit.HasValue)
            return false;

        goal.position = hit.Value.point + Vector3.up * FootTargetYOffset;
        goal.normal = hit.Value.normal;

        // Drop gate: fade IK by how far the target would pull the foot DOWN
        // from its animated position. The lift gate above reads the clip's
        // authored foot height, which cannot exclude a slow-gait swing foot
        // (barely lifted), and on a downhill slope the ground under such a
        // foot is arbitrarily far below - without this gate the foot gets
        // slammed onto the slope and, through the hips dip, drags the pelvis
        // down with it. Ground near or above the foot (uphill step-up) passes
        // through untouched.
        float drop = footPos.y - goal.position.y;
        effectiveBlend *= 1.0f - Mathf.InverseLerp(FootDropStart, FootDropMax, drop);

        if (effectiveBlend <= 0.0f)
            return false;

        goal.verticalOffset = (goal.position.y - footPos.y) * effectiveBlend;
        // Fade the IK weight rather than dragging the target part of the way
        // down: a part-way target is a point in mid-air, so the foot would hover
        // instead of planting.
        goal.weight = effectiveBlend;
        return true;
    }

    // ---------------------------------------------------------------------
    // Aim IK (spine look + two-handed arm IK)
    // ---------------------------------------------------------------------
    private void UpdateAimIK(float dt)
    {
        IsAiming = AimWithRightMouse ? Input.IsDown(MouseButton.Right) : false;

        float targetSpine = IsAiming ? 1.0f : 0.0f;
        float targetHands = IsAiming ? 1.0f : 0.0f;
        spineIKWeight = Mathf.MoveTowards(spineIKWeight, targetSpine, SpineIKSpeed * dt);
        handIKWeight  = Mathf.MoveTowards(handIKWeight,  targetHands, HandIKSpeed  * dt);

        if (spineIKWeight <= 0.0f && handIKWeight <= 0.0f)
            return;

        if (mainCamera == null)
        {
            ResolveCamera();
            if (mainCamera == null)
                return;
        }

        if (!mainCamera.ScreenPointToRay(Input.mousePosition, out Ray ray))
            return;

        RaycastHit? hit = Physics.Raycast(ray, AimMaxDistance, cc.collisionLayers, false);
        if (!hit.HasValue)
            return;

        Vector3 hitPoint = hit.Value.point;
        Vector3 lookDir  = (hitPoint - transform.position);
        lookDir.y = 0.0f;
        if (lookDir.sqrMagnitude < 0.0001f)
            return;
        lookDir = lookDir.normalized;

        // Forward cone gate: if the player aims behind the character, disable
        // aim IK entirely so the character doesn't twist unnaturally.
        float forwardDot = Vector3.Dot(transform.forward, lookDir);
        if (forwardDot < Mathf.Cos(MaxAimForwardAngle * Mathf.Deg2Rad))
        {
            spineIKWeight = 0.0f;
            handIKWeight  = 0.0f;
            leftHandWeight  = Mathf.MoveTowards(leftHandWeight,  0.0f, HandIKSpeed * dt);
            rightHandWeight = Mathf.MoveTowards(rightHandWeight, 0.0f, HandIKSpeed * dt);
            return;
        }

        if (spineIKWeight > 0.0f && Spine.IsValid())
        {
            // SpineAimAxis is the chest's FACING, not the axis the bone runs
            // along - aiming a spine along its length points the torso at the
            // target like a lance and folds the character in half. No up
            // reference is passed, so the animated spine roll is preserved
            // rather than overwritten. The cone limit is measured against the
            // animated pose each frame, so the chest eases to a stop at it.
            IK.SetIKAim(Spine, hitPoint, SpineAimAxis, Vector3.up, Vector3.zero,
                        MaxAimForwardAngle, spineIKWeight);
        }

        // Blend between leading / trailing hand as the aim direction sweeps
        // past the forward axis - avoids the "crossed arms" look.
        float sideAngle = Vector3.SignedAngle(transform.forward, lookDir, transform.up);
        float absAngle = Mathf.Abs(sideAngle);
        float t = absAngle >= HandSwitchStartAngle
            ? Mathf.InverseLerp(HandSwitchStartAngle, HandSwitchEndAngle, absAngle)
            : 0.0f;

        float targetLeft, targetRight;
        if (sideAngle < 0.0f)
        {
            targetRight = handIKWeight;
            targetLeft  = handIKWeight * (1.0f - t);
        }
        else
        {
            targetLeft  = handIKWeight;
            targetRight = handIKWeight * (1.0f - t);
        }

        rightHandWeight = Mathf.MoveTowards(rightHandWeight, targetRight, HandIKSpeed * dt);
        leftHandWeight  = Mathf.MoveTowards(leftHandWeight,  targetLeft,  HandIKSpeed * dt);

        // Elbow poles. We bias outward (character's right/left) and downward so
        // the elbow always tucks sideways when aiming, instead of flipping
        // behind the back or clipping through the chest.
        Vector3 rightElbowPole = transform.position + transform.right * 1.5f - transform.up * 0.25f;
        Vector3 leftElbowPole  = transform.position - transform.right * 1.5f - transform.up * 0.25f;

        // Aim at the hit point at partial WEIGHT rather than solving toward a
        // lerped target. Lerping the target points the arm at a spot in mid-air,
        // so the elbow travels through poses the arm never actually holds; a
        // weight blends between the animated pose and the fully aimed one.
        if (rightHandWeight > 0.0f && RightHand.IsValid())
        {
            IK.SetIKPositionTwoBone(RightHand, hitPoint, rightElbowPole, rightHandWeight);
        }

        if (leftHandWeight > 0.0f && LeftHand.IsValid())
        {
            IK.SetIKPositionTwoBone(LeftHand, hitPoint, leftElbowPole, leftHandWeight);
        }
    }

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------
    private void ResolveBones()
    {
        if (!Hips.IsValid())      Hips      = FindBone("mixamorig:Hips",      "Hips");
        if (!Spine.IsValid())     Spine     = FindBone("mixamorig:Spine",     "Spine");
        if (!LeftFoot.IsValid())  LeftFoot  = FindBone("mixamorig:LeftFoot",  "LeftFoot",  "foot.L");
        if (!RightFoot.IsValid()) RightFoot = FindBone("mixamorig:RightFoot", "RightFoot", "foot.R");
        if (!LeftHand.IsValid())  LeftHand  = FindBone("mixamorig:LeftHand",  "LeftHand",  "hand.L");
        if (!RightHand.IsValid()) RightHand = FindBone("mixamorig:RightHand", "RightHand", "hand.R");
    }

    private Entity FindBone(params string[] names)
    {
        foreach (string name in names)
        {
            Entity e = transform.FindChild(name, true);
            if (e.IsValid())
                return e;
        }
        return Entity.Invalid;
    }

    /// <summary>
    /// Frame-rate independent equivalent of a fixed Lerp factor. A raw
    /// Lerp(a, b, k) converges k per FRAME, so the hips dip is visibly stiffer
    /// at 144Hz than at 60Hz; this converts k into the same rate per second.
    /// </summary>
    private static float SmoothingAlpha(float factorPerFrameAt60, float dt)
    {
        if (factorPerFrameAt60 <= 0.0f) return 0.0f;
        if (factorPerFrameAt60 >= 1.0f) return 1.0f;
        return 1.0f - Mathf.Pow(1.0f - factorPerFrameAt60, dt * 60.0f);
    }

    private void ResolveCamera()
    {
        Entity camEntity = Scene.FindEntityByName("Main Camera");
        if (!camEntity.IsValid())
            return;
        mainCamera = camEntity.GetComponent<CameraComponent>();
    }

    /// <summary>
    /// Blends the current horizontal velocity toward the input target while
    /// preserving the larger magnitude of the two. Allows steering in the air
    /// without letting the player brake mid-jump by releasing the stick.
    /// </summary>
    private static Vector3 SteerAirVelocity(Vector3 current, Vector3 target, float maxDelta)
    {
        float currentMag = current.magnitude;

        // No input in the air: keep full momentum, do nothing.
        if (target.sqrMagnitude < 1e-6f)
            return current;

        // Below target magnitude: normal accel-toward-target (raises speed).
        if (currentMag <= target.magnitude)
            return Vector3.MoveTowards(current, target, maxDelta);

        // Already faster than target: steer direction only, keep magnitude.
        Vector3 targetDir = target.normalized;
        Vector3 redirected = Vector3.MoveTowards(current, targetDir * currentMag, maxDelta);
        // Re-normalize to guard against tiny magnitude drift from the lerp.
        if (redirected.sqrMagnitude > 1e-6f)
            redirected = redirected.normalized * currentMag;
        return redirected;
    }
}
