using System;
using System.Globalization;

namespace Unravel.Core
{
    /// <summary>
    /// An &lt;input type="range"&gt;. <see cref="value"/> is the number; the base class
    /// <see cref="UIElement.value"/> is the same value as text.
    /// </summary>
    public class UISlider : UIElement
    {
        internal UISlider(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// The value between <see cref="min"/> and <see cref="max"/>, on a <see cref="step"/>.
        /// Setting it sends "change"; see <see cref="SetValueWithoutNotify(float)"/>.
        /// </summary>
        public new float value
        {
            get => Parse(base.value, 0f);
            set => base.value = Format(value);
        }

        /// <summary>
        /// Sets <see cref="value"/> without sending "change" to scripts.
        /// </summary>
        public void SetValueWithoutNotify(float newValue)
        {
            SetValueWithoutNotify(Format(newValue));
        }

        /// <summary>
        /// The lowest value ('min' attribute, default 0).
        /// </summary>
        public float min
        {
            get => Parse(GetAttribute("min"), 0f);
            set => SetAttribute("min", Format(value));
        }

        /// <summary>
        /// The highest value ('max' attribute, default 100).
        /// </summary>
        public float max
        {
            get => Parse(GetAttribute("max"), 100f);
            set => SetAttribute("max", Format(value));
        }

        /// <summary>
        /// The step values snap to ('step' attribute, default 1).
        /// </summary>
        public float step
        {
            get => Parse(GetAttribute("step"), 1f);
            set => SetAttribute("step", Format(value));
        }

        private static float Parse(string text, float fallback)
        {
            return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) ? parsed : fallback;
        }

        private static string Format(float number)
        {
            return number.ToString("R", CultureInfo.InvariantCulture);
        }
    }
}
