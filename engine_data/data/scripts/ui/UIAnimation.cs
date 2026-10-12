using System;

namespace Unravel.Core
{
    /// <summary>
    /// Easing curves for <see cref="UIElement.Animate"/>. In eases out of the start, Out into the
    /// end, InOut both.
    /// </summary>
    public enum UIEasing
    {
        // Values: curve * 4 + direction, decoded natively (decode_easing in ui_bindings.cpp).
        Linear = 30,
        QuadIn = 33, QuadOut = 34, QuadInOut = 35,
        CubicIn = 17, CubicOut = 18, CubicInOut = 19,
        QuartIn = 37, QuartOut = 38, QuartInOut = 39,
        QuintIn = 41, QuintOut = 42, QuintInOut = 43,
        SineIn = 45, SineOut = 46, SineInOut = 47,
        ExpoIn = 25, ExpoOut = 26, ExpoInOut = 27,
        CircIn = 13, CircOut = 14, CircInOut = 15,
        BackIn = 5, BackOut = 6, BackInOut = 7,
        ElasticIn = 21, ElasticOut = 22, ElasticInOut = 23,
        BounceIn = 9, BounceOut = 10, BounceInOut = 11
    }

    /// <summary>
    /// A running property animation started with <see cref="UIElement.Animate"/>.
    /// The animated value stays on the element as an inline style when it finishes.
    /// </summary>
    public sealed class UIAnimation
    {
        private readonly UIElement element;
        private Action completedHandlers;
        private ScriptComponent completedOwner;

        internal UIAnimation(UIElement element, string property)
        {
            this.element = element;
            this.property = property;
        }

        /// <summary>
        /// The animated property.
        /// </summary>
        public string property { get; }

        /// <summary>
        /// Whether the animation runs: false once it finished, was stopped, was replaced by another
        /// animation of the same property, or could not start.
        /// </summary>
        public bool isRunning { get; private set; }

        /// <summary>
        /// Raised when the animation finishes by itself; not when it is stopped or replaced, nor
        /// for one that loops forever. Ends with the subscribing script.
        /// </summary>
        public event Action completed
        {
            add
            {
                completedHandlers += value;
                completedOwner = value?.Target as ScriptComponent ?? ScriptComponent.running;
            }
            remove => completedHandlers -= value;
        }

        /// <summary>
        /// Stops the animation where it is; the element keeps the current value.
        /// </summary>
        public void Stop()
        {
            if (!isRunning)
            {
                return;
            }
            UIEventManager.UntrackAnimation(element, this);
            element.StopAnimationOf(property);
            End(false);
        }

        internal void Start()
        {
            isRunning = true;
        }

        /// <summary>
        /// Ends the animation; @p finished raises <see cref="completed"/>.
        /// </summary>
        internal void End(bool finished)
        {
            if (!isRunning)
            {
                return;
            }
            isRunning = false;
            Action handlers = completedHandlers;
            completedHandlers = null;
            if (!finished || handlers == null)
            {
                return;
            }
            using (ScriptComponent.Run(completedOwner))
            {
                try
                {
                    handlers();
                }
                catch (Exception ex)
                {
                    Log.Error($"UI animation of '{property}' completed handler: {ex}");
                }
            }
        }

        internal void ForgetHandlersOf(ScriptComponent script)
        {
            if (ReferenceEquals(completedOwner, script))
            {
                completedHandlers = null;
            }
        }
    }
}
