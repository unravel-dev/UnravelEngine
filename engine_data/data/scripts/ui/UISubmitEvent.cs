using System;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// A "submit" from a form: the values of its named controls.
    /// </summary>
    public class UISubmitEvent : UIEventBase
    {
        private const char NameSeparator = '\x1f';
        private const char FieldSeparator = '\x1e';

        /// <summary>
        /// Control name to value; a name used by several controls maps to their values joined with ", ".
        /// The submitting button's name maps to its value, or "submit" when it has no name.
        /// </summary>
        public readonly Dictionary<string, string> values = new Dictionary<string, string>();

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            values.Clear();
            if (string.IsNullOrEmpty(text))
            {
                return;
            }
            foreach (string field in text.Split(FieldSeparator))
            {
                int separator = field.IndexOf(NameSeparator);
                if (separator > 0)
                {
                    values[field.Substring(0, separator)] = field.Substring(separator + 1);
                }
            }
        }
    }
}
