// Checks the page's part in the game's sound (game/web/wwwroot/audio.js): which of miniaudio's devices it tells to go
// on after the browser stopped their sound. Run with: node --test game/web/tests/audio.test.mjs
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { resumeStoppedDevices } from '../wwwroot/audio.js';

const STATES = Object.freeze({ stopped: 1, started: 2 });

/// A device as miniaudio lists it in the page; `resumes` counts what its context was told.
function deviceOf(state, contextState) {
  const device = { state, resumes: 0 };
  device.webaudio = { state: contextState, resume: async () => { ++device.resumes; } };
  return device;
}

test('a device that plays goes on when the browser stopped its sound', () => {
  const suspended = deviceOf(STATES.started, 'suspended');
  const interrupted = deviceOf(STATES.started, 'interrupted');
  const running = deviceOf(STATES.started, 'running');
  const closed = deviceOf(STATES.started, 'closed');
  const stoppedByGame = deviceOf(STATES.stopped, 'suspended');
  const devices = [suspended, null, interrupted, running, closed, stoppedByGame, { state: STATES.started }];
  assert.equal(resumeStoppedDevices({ devices, device_state: STATES }), 2);
  assert.equal(suspended.resumes, 1);
  assert.equal(interrupted.resumes, 1, 'Safari names the state of a hidden page\'s sound "interrupted"');
  assert.equal(running.resumes, 0);
  assert.equal(closed.resumes, 0, 'a closed context cannot go on');
  assert.equal(stoppedByGame.resumes, 0, 'a device the game stopped stays stopped');
});

test('a page without sound devices has nothing to tell', () => {
  assert.equal(resumeStoppedDevices(undefined), 0);
  assert.equal(resumeStoppedDevices({ devices: [], device_state: STATES }), 0);
});

test('a refused resume is not an error of the page', async () => {
  const device = { state: STATES.started, webaudio: { state: 'suspended', resume: () => Promise.reject(new Error('no gesture')) } };
  assert.equal(resumeStoppedDevices({ devices: [device], device_state: STATES }), 1);
  await new Promise((resolve) => setImmediate(resolve));
});
