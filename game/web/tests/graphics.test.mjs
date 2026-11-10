// Checks the page's choice of graphics API (game/web/wwwroot/graphics.js) for every combination of what a browser
// offers. Run with: node --test game/web/tests/graphics.test.mjs
import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  DEFAULT_GRAPHICS_POLICY,
  WEBGL2,
  WEBGPU,
  decideGraphics,
  describeFailure,
  isFallbackReason,
  isSoftwareRendererName,
  makeGraphicsArguments,
  probeWebGL2,
  probeWebGPU,
  readGraphicsArguments,
} from '../wwwroot/graphics.js';

const BOTH = { ...DEFAULT_GRAPHICS_POLICY, backends: [WEBGPU, WEBGL2] };
const WEBGL2_ONLY = { ...DEFAULT_GRAPHICS_POLICY, backends: [WEBGL2] };
const HARDWARE_WEBGPU = { isUsable: true };
const SOFTWARE_WEBGPU = { isUsable: false, reason: 'software_adapter' };
const HARDWARE_WEBGL2 = { isUsable: true };
const SOFTWARE_WEBGL2 = { isUsable: false, problem: 'webgl2_software' };
const NO_WEBGL2 = { isUsable: false, problem: 'webgl2_missing' };

function decide(policy, webgpu, webgl2, forced = null) {
  return decideGraphics({ policy, forced, webgpu, webgl2 });
}

/// A page's global object whose WebGPU gives `adapter` (or throws `error`) when asked.
function makeWebGPUPage(adapter, { error = null, isSecureContext = true } = {}) {
  const gpu = { requestAdapter: async () => { if (error) { throw error; } return adapter; } };
  return { isSecureContext, navigator: { gpu } };
}

/// A WebGL 2 context named `renderer` that offers `extensions`.
function makeWebGL2Context(renderer, extensions) {
  const RENDERER = 0x1f01;
  return {
    RENDERER,
    getParameter: (parameter) => (parameter === RENDERER ? renderer : null),
    getExtension: (name) => (extensions.includes(name) || name === 'WEBGL_lose_context' ? { loseContext() {} } : null),
    getSupportedExtensions: () => extensions,
  };
}

/// A context factory that gives `hardware` where the page refuses software contexts, `software` elsewhere.
function makeContextFactory(hardware, software) {
  return (attributes) => (attributes.failIfMajorPerformanceCaveat ? hardware : software);
}

const FULL_WEBGL2_EXTENSIONS = ['EXT_color_buffer_float', 'WEBGL_compressed_texture_s3tc', 'EXT_texture_compression_rgtc'];

test('WebGPU on the graphics card wins wherever the build allows it', () => {
  assert.deepEqual(decide(BOTH, HARDWARE_WEBGPU, HARDWARE_WEBGL2), { backend: WEBGPU, reason: 'none' });
  assert.deepEqual(decide(DEFAULT_GRAPHICS_POLICY, HARDWARE_WEBGPU, undefined), { backend: WEBGPU, reason: 'none' });
});

test('WebGL 2 stands in with the reason WebGPU is unavailable', () => {
  for (const reason of ['insecure_page', 'api_missing', 'no_adapter', 'device_failed', 'software_adapter']) {
    assert.deepEqual(decide(BOTH, { isUsable: false, reason }, HARDWARE_WEBGL2), { backend: WEBGL2, reason });
    assert.ok(isFallbackReason(reason));
  }
});

test('an API without the graphics card counts as unavailable', () => {
  assert.deepEqual(decide(BOTH, SOFTWARE_WEBGPU, HARDWARE_WEBGL2), { backend: WEBGL2, reason: 'software_adapter' });
  const neither = decide(BOTH, SOFTWARE_WEBGPU, SOFTWARE_WEBGL2);
  assert.deepEqual(neither, { backend: null, reason: 'software_adapter', webgl2Problem: 'webgl2_software' });
  assert.match(describeFailure(neither),
    /WebGPU would draw without the graphics card\. .*WebGL 2 would draw without the graphics card\. Turn on hardware acceleration/);
  assert.deepEqual(decide(DEFAULT_GRAPHICS_POLICY, SOFTWARE_WEBGPU, undefined),
    { backend: null, reason: 'software_adapter', webgl2Problem: undefined });
  assert.match(describeFailure(decide(WEBGL2_ONLY, undefined, SOFTWARE_WEBGL2)), /Turn on hardware acceleration/);
  assert.ok(isSoftwareRendererName('Google SwiftShader') && isSoftwareRendererName('Microsoft Basic Render Driver'));
  assert.ok(!isSoftwareRendererName('ANGLE (NVIDIA, NVIDIA GeForce RTX 4090 Direct3D11 vs_5_0 ps_5_0, D3D11)'));
});

test('the WebGPU probe takes only an adapter on the graphics card', async () => {
  assert.deepEqual(await probeWebGPU({ isSecureContext: false }), { isUsable: false, reason: 'insecure_page' });
  assert.deepEqual(await probeWebGPU({ isSecureContext: true, navigator: {} }), { isUsable: false, reason: 'api_missing' });
  assert.deepEqual(await probeWebGPU(makeWebGPUPage(null)), { isUsable: false, reason: 'no_adapter' });
  assert.deepEqual(await probeWebGPU(makeWebGPUPage(null, { error: new Error('blocked') })),
    { isUsable: false, reason: 'no_adapter' });
  const fallback = { info: { vendor: 'google', isFallbackAdapter: true } };
  assert.deepEqual(await probeWebGPU(makeWebGPUPage(fallback)), { isUsable: false, reason: 'software_adapter' });
  const basicRender = { info: { vendor: 'microsoft', description: 'Microsoft Basic Render Driver', isFallbackAdapter: false } };
  assert.deepEqual(await probeWebGPU(makeWebGPUPage(basicRender)), { isUsable: false, reason: 'software_adapter' });
  const hardware = { info: { vendor: 'nvidia', architecture: 'lovelace', isFallbackAdapter: false } };
  const usable = await probeWebGPU(makeWebGPUPage(hardware));
  assert.equal(usable.isUsable, true);
  assert.equal(usable.adapter, hardware);
  assert.equal(usable.name, 'nvidia lovelace');
});

test('the WebGL 2 probe takes only a context on the graphics card that renders to float images', () => {
  assert.deepEqual(probeWebGL2(makeContextFactory(null, null)), { isUsable: false, problem: 'webgl2_missing' });
  const software = makeWebGL2Context('Google SwiftShader', FULL_WEBGL2_EXTENSIONS);
  assert.deepEqual(probeWebGL2(makeContextFactory(null, software)), { isUsable: false, problem: 'webgl2_software' });
  assert.deepEqual(probeWebGL2(makeContextFactory(software, software)), { isUsable: false, problem: 'webgl2_software' });
  const noFloat = makeWebGL2Context('ANGLE (NVIDIA)', ['WEBGL_compressed_texture_s3tc']);
  assert.deepEqual(probeWebGL2(makeContextFactory(noFloat, noFloat)), { isUsable: false, problem: 'webgl2_float_targets' });
  const hardware = makeWebGL2Context('ANGLE (NVIDIA)', FULL_WEBGL2_EXTENSIONS);
  const usable = probeWebGL2(makeContextFactory(hardware, hardware));
  assert.equal(usable.isUsable, true);
  assert.equal(usable.name, 'ANGLE (NVIDIA)');
  assert.deepEqual([...usable.textureSets], ['bc']);
});

test('a WebGL 2 only build starts WebGL 2 without a fallback reason', () => {
  assert.deepEqual(decide(WEBGL2_ONLY, undefined, HARDWARE_WEBGL2), { backend: WEBGL2, reason: 'none' });
  assert.equal(isFallbackReason('none'), false);
});

test('neither API: no backend, with both reasons for the error screen', () => {
  const choice = decide(BOTH, { isUsable: false, reason: 'no_adapter' }, NO_WEBGL2);
  assert.deepEqual(choice, { backend: null, reason: 'no_adapter', webgl2Problem: 'webgl2_missing' });
  assert.match(describeFailure(choice), /turned WebGPU off.*does not offer WebGL 2.*hardware acceleration/);
  const webgpuOnly = decide(DEFAULT_GRAPHICS_POLICY, { isUsable: false, reason: 'api_missing' }, undefined);
  assert.deepEqual(webgpuOnly, { backend: null, reason: 'api_missing', webgl2Problem: undefined });
  assert.equal(describeFailure(webgpuOnly), 'This browser does not offer WebGPU. Update the browser, or play in a browser that offers WebGPU.');
  const noFloatTargets = decide(WEBGL2_ONLY, undefined, { isUsable: false, problem: 'webgl2_float_targets' });
  assert.match(describeFailure(noFloatTargets), /floating-point images the game renders to\. Update the browser and the graphics driver\./);
});

test('the address forces an API the build ships, and nothing else', () => {
  assert.deepEqual(decide(BOTH, HARDWARE_WEBGPU, HARDWARE_WEBGL2, WEBGL2), { backend: WEBGL2, reason: 'forced' });
  assert.equal(isFallbackReason('forced'), false);
  assert.deepEqual(decide(BOTH, SOFTWARE_WEBGPU, HARDWARE_WEBGL2, WEBGPU), { backend: null, reason: 'software_adapter' });
  assert.deepEqual(decide(BOTH, HARDWARE_WEBGPU, SOFTWARE_WEBGL2, WEBGL2),
    { backend: null, reason: 'forced', webgl2Problem: 'webgl2_software' });
  assert.deepEqual(decide(DEFAULT_GRAPHICS_POLICY, HARDWARE_WEBGPU, undefined, WEBGL2), { backend: WEBGPU, reason: 'none' });
  assert.deepEqual(decide(BOTH, HARDWARE_WEBGPU, NO_WEBGL2, WEBGL2),
    { backend: null, reason: 'forced', webgl2Problem: 'webgl2_missing' });
});

test('the page reads the forced API from graphics= or the engine arguments and passes its own', () => {
  assert.deepEqual(readGraphicsArguments('?graphics=webgl2&arg=--log-level&arg=trace'),
    { args: ['--log-level', 'trace'], forced: WEBGL2 });
  assert.deepEqual(readGraphicsArguments('?arg=--graphics&arg=webgl2&arg=--novsync'), { args: ['--novsync'], forced: WEBGL2 });
  assert.deepEqual(readGraphicsArguments('?arg=--graphics=webgpu&arg=--graphics-fallback&arg=forced'), { args: [], forced: WEBGPU });
  assert.deepEqual(readGraphicsArguments('?graphics=vulkan'), { args: [], forced: null });
  assert.deepEqual(makeGraphicsArguments({ backend: WEBGPU, reason: 'none' }), ['--graphics', WEBGPU]);
  assert.deepEqual(makeGraphicsArguments({ backend: WEBGL2, reason: 'no_adapter' }),
    ['--graphics', WEBGL2, '--graphics-fallback', 'no_adapter']);
  assert.deepEqual(makeGraphicsArguments({ backend: WEBGL2, reason: 'none' }), ['--graphics', WEBGL2]);
});
