// The page's part in the game's sound. The engine plays through miniaudio's Web Audio devices, which it lists in the
// page (globalThis.miniaudio); what the browser stops there, only the page can start again.

/**
 * Tells every sound device that plays, and whose sound the browser stopped, to go on, and returns how many it told.
 * `audio` is miniaudio's page object (its devices and the device states' numbers); it is missing until the engine has
 * made a device. A device the game itself stopped stays stopped.
 */
export function resumeStoppedDevices(audio) {
  let resumed = 0;
  for (const device of audio?.devices ?? []) {
    const context = device?.webaudio;
    const isPlaying = device?.state === audio.device_state.started;
    if (context && isPlaying && context.state !== 'running' && context.state !== 'closed') {
      context.resume().catch(() => {});
      ++resumed;
    }
  }
  return resumed;
}
