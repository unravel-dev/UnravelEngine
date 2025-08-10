using System;
using System.Runtime.CompilerServices;
using Ace.Core;

public class SampleMouseLookComponent : ScriptComponent
{
	public Vector2 mouseAbsolute;
    public Vector2 mouseSensitivity = new Vector2(10, 10);
    public float moveSpeed = 20.0f;

	public override void OnCreate()
	{
	}

	public override void OnStart()
	{
	}

	public override void OnUpdate()
	{
		if (!Input.IsDown("Mouse Right"))
           return;
        var chidlren = transform.children;

        var mouseDelta = new Vector2(Input.GetAxis("Mouse X"), Input.GetAxis("Mouse Y"));
        var moveDelta = new Vector3(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"));

        // if (Input.GetKey(KeyCode.Q))
        //     moveDelta.y += 1;
        // if (Input.GetKey(KeyCode.E))
        //     moveDelta.y -= 1;
        var speedBoost = false;//Input.GetKey(KeyCode.LeftShift);
        var moveSpeedAdjust = Input.GetAxis("Mouse ScrollWheel");

        mouseDelta = Vector2.Scale(mouseDelta, mouseSensitivity);
        mouseDelta *= Time.deltaTime;
        moveSpeed = Mathf.Pow(moveSpeed, 1 + moveSpeedAdjust * Time.deltaTime * 30);
        moveDelta *= moveSpeed;
        if (speedBoost)
            moveDelta *= 4;
        moveDelta *= Time.deltaTime;
        mouseAbsolute += mouseDelta;
        
        transform.rotation = Quaternion.AngleAxis(mouseAbsolute.x, Vector3.up) * Quaternion.AngleAxis(-mouseAbsolute.y, Vector3.right);
        transform.position = transform.position + transform.rotation * moveDelta;
	}

}
