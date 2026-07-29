namespace Unravel.Core
{
    /// <summary>
    /// Mixer bus for routing audio sources. Master gain is applied on top of these.
    /// </summary>
    public enum AudioBus : byte
    {
        /// <summary>Gameplay sound effects.</summary>
        Sfx = 0,
        /// <summary>Music and stems.</summary>
        Music = 1,
        /// <summary>UI and menu sounds (typically non-spatial).</summary>
        Ui = 2
    }
}
