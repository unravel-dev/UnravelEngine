using System;

namespace Ace.Core
{
    /// <summary>
    /// Specifies a range for a numeric field in a class or struct.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
    public sealed class RangeAttribute : Attribute
    {
        /// <summary>
        /// The minimum value of the range.
        /// </summary>
        public readonly float min;

        /// <summary>
        /// The maximum value of the range.
        /// </summary>
        public readonly float max;

        /// <summary>
        /// Initializes a new instance of the <see cref="RangeAttribute"/> class.
        /// </summary>
        /// <param name="min">The minimum value of the range.</param>
        /// <param name="max">The maximum value of the range.</param>
        public RangeAttribute(float min, float max)
        {
            this.min = min;
            this.max = max;
        }
    }

    /// <summary>
    /// Specifies the maximum allowable value for a numeric field.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
    public sealed class MaxAttribute : Attribute
    {
        /// <summary>
        /// The maximum allowable value.
        /// </summary>
        public readonly float max;

        /// <summary>
        /// Initializes a new instance of the <see cref="MaxAttribute"/> class.
        /// </summary>
        /// <param name="max">The maximum allowable value.</param>
        public MaxAttribute(float max)
        {
            this.max = max;
        }
    }

    /// <summary>
    /// Specifies the minimum allowable value for a numeric field.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
    public sealed class MinAttribute : Attribute
    {
        /// <summary>
        /// The minimum allowable value.
        /// </summary>
        public readonly float min;

        /// <summary>
        /// Initializes a new instance of the <see cref="MinAttribute"/> class.
        /// </summary>
        /// <param name="min">The minimum allowable value.</param>
        public MinAttribute(float min)
        {
            this.min = min;
        }
    }

    /// <summary>
    /// Specifies a step increment for a numeric field in a class or struct.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
    public sealed class StepAttribute : Attribute
    {
        /// <summary>
        /// The step increment value.
        /// </summary>
        public readonly float step;

        /// <summary>
        /// Initializes a new instance of the <see cref="StepAttribute"/> class.
        /// </summary>
        /// <param name="step">The step increment value.</param>
        public StepAttribute(float step)
        {
            this.step = step;
        }
    }

    /// <summary>
    /// Adds a tooltip to a field, providing a brief description or hint.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
    public sealed class TooltipAttribute : Attribute
    {
        /// <summary>
        /// The text of the tooltip.
        /// </summary>
        public readonly string tooltip;

        /// <summary>
        /// Initializes a new instance of the <see cref="TooltipAttribute"/> class.
        /// </summary>
        /// <param name="tooltip">The text of the tooltip.</param>
        public TooltipAttribute(string tooltip)
        {
            this.tooltip = tooltip;
        }
    }
}
