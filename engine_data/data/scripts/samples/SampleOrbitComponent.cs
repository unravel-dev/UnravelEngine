using System;
using Ace.Core;

public class SampleOrbitComponent : ScriptComponent
{
    public float minimumDistance = 5f;  // Minimum distance to maintain
    public float orbitSpeed = 30f;  // Base speed of rotation around the target
    public float axisChangeSpeed = 0.5f;  // Speed at which the axis changes
    public float changeInterval = 2f;  // Time interval between axis changes
    public float positionSmoothTime = 0.5f; // Smoothing time f

    public Entity targetEntity;

    private Vector3 currentAxis;  // Current axis of rotation
    private Vector3 targetAxis;   // Target axis to transition to
    private float changeTimer;    // Timer to manage axis change intervals
    private Vector3 velocity = Vector3.zero; // For smooth position interpolation (used by SmoothDamp)


	public override void OnCreate()
	{

	}

	public override void OnStart()
	{

        // Initialize with a random axis
        currentAxis = Random.onUnitSphere;
        targetAxis = Random.onUnitSphere;
        changeTimer = changeInterval;

	}

	public override void OnDestroy()
	{
	}

	public override void OnUpdate()
	{
        if(!targetEntity.IsValid())
        {
            return;
        }
 
		//var sphere = Scene.FindEntityByTag("Sphere");
		var target = targetEntity.transform;

        // Calculate the desired position based on minimum distance
        Vector3 desiredPosition = target.position + (transform.position - target.position).normalized * minimumDistance;

        // Smoothly interpolate the object's position towards the desired position
        transform.position = Vector3.SmoothDamp(transform.position, desiredPosition, ref velocity, positionSmoothTime, Mathf.Infinity, Time.deltaTime);

        // Update the timer for axis change
        changeTimer -= Time.deltaTime;
        if (changeTimer <= 0f)
        {
            // Choose a new random axis
            targetAxis = Random.onUnitSphere;
            changeTimer = changeInterval;
        }

        // Smoothly interpolate to the new axis
        currentAxis = Vector3.Slerp(currentAxis, targetAxis, axisChangeSpeed * Time.deltaTime);

        // Rotate around the target using the dynamic axis
        transform.RotateAround(target.position, currentAxis, orbitSpeed * Time.deltaTime);

        // Optionally face the target
        //transform.LookAt(target);
        transform.forward = (target.position - transform.position).normalized;
	}
}
