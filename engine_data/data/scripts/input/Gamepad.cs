namespace Unravel.Core
{
    /// <summary>
    /// An analog control of a gamepad. Sticks read from -1 to 1, negative to the left and up; triggers
    /// read from 0 (released) to 1 (pulled all the way).
    /// </summary>
    public enum GamepadAxis : int
    {
        /// <summary>The left stick, left to right.</summary>
        LeftX,
        /// <summary>The left stick, up to down.</summary>
        LeftY,
        /// <summary>The right stick, left to right.</summary>
        RightX,
        /// <summary>The right stick, up to down.</summary>
        RightY,
        /// <summary>The left trigger.</summary>
        LeftTrigger,
        /// <summary>The right trigger.</summary>
        RightTrigger,
    }

    /// <summary>
    /// A button of a gamepad, named by where it sits (the face buttons by compass point), not by its label.
    /// </summary>
    public enum GamepadButton : int
    {
        /// <summary>The bottom face button: Xbox A, PlayStation Cross.</summary>
        South,
        /// <summary>The right face button: Xbox B, PlayStation Circle.</summary>
        East,
        /// <summary>The left face button: Xbox X, PlayStation Square.</summary>
        West,
        /// <summary>The top face button: Xbox Y, PlayStation Triangle.</summary>
        North,
        /// <summary>The Back (View, Share) button.</summary>
        Back,
        /// <summary>The Guide (Xbox, PS) button.</summary>
        Guide,
        /// <summary>The Start (Menu, Options) button.</summary>
        Start,
        /// <summary>Pressing the left stick in.</summary>
        LeftStick,
        /// <summary>Pressing the right stick in.</summary>
        RightStick,
        /// <summary>The left shoulder button (bumper).</summary>
        LeftShoulder,
        /// <summary>The right shoulder button (bumper).</summary>
        RightShoulder,
        /// <summary>The directional pad, up.</summary>
        DPadUp,
        /// <summary>The directional pad, down.</summary>
        DPadDown,
        /// <summary>The directional pad, left.</summary>
        DPadLeft,
        /// <summary>The directional pad, right.</summary>
        DPadRight,
        /// <summary>An extra button: Xbox Series Share, PS5 microphone, Switch Pro capture.</summary>
        Misc1,
        /// <summary>The upper paddle under the right hand.</summary>
        RightPaddle1,
        /// <summary>The upper paddle under the left hand.</summary>
        LeftPaddle1,
        /// <summary>The lower paddle under the right hand.</summary>
        RightPaddle2,
        /// <summary>The lower paddle under the left hand.</summary>
        LeftPaddle2,
        /// <summary>The PlayStation touchpad button.</summary>
        Touchpad,
        /// <summary>An extra button.</summary>
        Misc2,
        /// <summary>An extra button.</summary>
        Misc3,
        /// <summary>An extra button.</summary>
        Misc4,
        /// <summary>An extra button.</summary>
        Misc5,
        /// <summary>An extra button.</summary>
        Misc6,
    }
}
