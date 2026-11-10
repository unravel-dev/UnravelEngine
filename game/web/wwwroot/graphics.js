// Chooses the graphics API the page starts the game with: WebGPU, or WebGL 2 where the build allows it and WebGPU is
// unavailable. An API the browser offers only without the graphics card counts as unavailable: the game always runs on
// the graphics card. The build's policy comes from data_manifest.json's graphics block, which the Web profile's
// Graphics settings write. Every check runs before the download and leaves the game canvas untouched: a canvas keeps the first
// kind of context it gives out, and the engine creates its own on it.
//
// decideGraphics and readGraphicsArguments use no browser API, and the probes take the browser's objects as arguments,
// so game/web/tests checks them in Node.

export const WEBGPU = 'webgpu';
export const WEBGL2 = 'webgl2';

/// The policy of a manifest without a graphics block: a build for WebGPU only.
export const DEFAULT_GRAPHICS_POLICY = Object.freeze({
  backends: [WEBGPU],
  fallback_notice: 'tell',
  help_link: '',
});

/// Why WebGPU is unavailable, as the page tells the player, with what usually helps. The names are the engine's
/// --graphics-fallback values (graphics_fallback_reason).
export const WEBGPU_REASONS = Object.freeze({
  insecure_page: {
    sentence: 'This page is not served over HTTPS, which WebGPU needs.',
    help: 'Open the game from its https:// address.',
  },
  api_missing: {
    sentence: 'This browser does not offer WebGPU.',
    help: 'Update the browser, or play in a browser that offers WebGPU.',
  },
  no_adapter: {
    sentence: 'This browser turned WebGPU off for this graphics card or driver.',
    help: 'Turn on hardware acceleration in the browser settings, and update the graphics driver and the browser.',
  },
  device_failed: {
    sentence: 'This graphics card cannot run the WebGPU version.',
    help: 'Update the graphics driver and the browser.',
  },
  software_adapter: {
    sentence: "This browser's WebGPU would draw without the graphics card.",
    help: 'Turn on hardware acceleration in the browser settings, and update the graphics driver.',
  },
});

/// Why WebGL 2 cannot start the game, with what usually helps.
export const WEBGL2_PROBLEMS = Object.freeze({
  webgl2_missing: {
    sentence: 'This browser does not offer WebGL 2.',
    help: 'Update the browser and the graphics driver.',
  },
  webgl2_software: {
    sentence: "This browser's WebGL 2 would draw without the graphics card.",
    help: 'Turn on hardware acceleration in the browser settings, and update the graphics driver.',
  },
  webgl2_float_targets: {
    sentence: "This browser's WebGL 2 cannot draw into the floating-point images the game renders to.",
    help: 'Update the browser and the graphics driver.',
  },
});

/// The WebGL 2 extensions a GPU reads each texture set's formats with, by the manifest's set names. The bc set holds
/// BC4 and BC5 (one- and two-channel images, normal maps) besides BC1 to BC3.
export const WEBGL2_TEXTURE_SET_EXTENSIONS = Object.freeze({
  bc: ['WEBGL_compressed_texture_s3tc', 'EXT_texture_compression_rgtc'],
  astc: ['WEBGL_compressed_texture_astc'],
  etc2: ['WEBGL_compressed_texture_etc'],
});

/// Names of renderers and adapters that draw on the processor.
const SOFTWARE_RENDERER = /swiftshader|llvmpipe|lavapipe|softpipe|software|basic render driver/i;
/// What Chrome and Safari call every renderer unless asked through WEBGL_debug_renderer_info.
const MASKED_RENDERER = /^webkit webgl$/i;

/// Whether the renderer or adapter `name` draws on the processor.
export function isSoftwareRendererName(name) {
  return SOFTWARE_RENDERER.test(name);
}

/**
 * Whether the browser's WebGPU can start the game: { isUsable, adapter, name }, or { isUsable: false, reason } with a
 * WEBGPU_REASONS name. An adapter without the graphics card (a fallback adapter) cannot. The device is created later,
 * from the adapter, only when WebGPU is chosen. `environment` is the page's global object.
 */
export async function probeWebGPU(environment = globalThis) {
  if (!environment.isSecureContext) {
    return { isUsable: false, reason: 'insecure_page' };
  }
  const gpu = environment.navigator?.gpu;
  if (!gpu) {
    return { isUsable: false, reason: 'api_missing' };
  }
  let adapter = null;
  try {
    adapter = await gpu.requestAdapter({ powerPreference: 'high-performance' });
  } catch {
    adapter = null;
  }
  if (!adapter) {
    return { isUsable: false, reason: 'no_adapter' };
  }
  const info = adapter.info ?? {};
  const name = [info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(' ');
  const isFallbackAdapter = Boolean(info.isFallbackAdapter ?? adapter.isFallbackAdapter);
  if (isFallbackAdapter || isSoftwareRendererName(name)) {
    return { isUsable: false, reason: 'software_adapter' };
  }
  return { isUsable: true, adapter, name: name || 'an unnamed adapter' };
}

function createWebGL2Context(attributes) {
  try {
    return document.createElement('canvas').getContext('webgl2', attributes);
  } catch {
    return null;
  }
}

function readRendererName(gl) {
  const name = String(gl.getParameter(gl.RENDERER) ?? '');
  if (!MASKED_RENDERER.test(name)) {
    return name;
  }
  const debug = gl.getExtension('WEBGL_debug_renderer_info');
  return debug ? String(gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) ?? name) : name;
}

/// The texture sets whose formats `gl` reads (WEBGL2_TEXTURE_SET_EXTENSIONS).
function findReadableTextureSets(gl) {
  const extensions = new Set(gl.getSupportedExtensions() ?? []);
  const sets = new Set();
  for (const [set, needed] of Object.entries(WEBGL2_TEXTURE_SET_EXTENSIONS)) {
    if (needed.every((extension) => extensions.has(extension))) {
      sets.add(set);
    }
  }
  return sets;
}

/// Releases a throwaway context at once instead of when the page collects it.
function loseContext(gl) {
  gl.getExtension('WEBGL_lose_context')?.loseContext();
}

/// Why `gl`, a context on the graphics card, cannot start the game, or null when it can.
function findWebGL2Problem(gl, name) {
  if (isSoftwareRendererName(name)) {
    return 'webgl2_software';
  }
  // The engine's vertex shaders stay within the 256 uniform vectors every WebGL 2 device grants: bone palettes and
  // particle data come from textures (the "eshader files" tests count them).
  return gl.getExtension('EXT_color_buffer_float') ? null : 'webgl2_float_targets';
}

/**
 * Whether the browser's WebGL 2 can start the game, checked on throwaway canvases: { isUsable, name, textureSets }, or
 * { isUsable: false, problem } with a WEBGL2_PROBLEMS name. A context that refuses to start where it would draw on the
 * processor (failIfMajorPerformanceCaveat) tells software from hardware; without the graphics card WebGL 2 cannot.
 * `createContext` makes a WebGL 2 context with the given attributes on a new canvas, or null.
 */
export function probeWebGL2(createContext = createWebGL2Context) {
  const gl = createContext({ failIfMajorPerformanceCaveat: true });
  if (!gl) {
    const software = createContext({});
    if (software) {
      loseContext(software);
    }
    return { isUsable: false, problem: software ? 'webgl2_software' : 'webgl2_missing' };
  }
  try {
    const name = readRendererName(gl);
    const problem = findWebGL2Problem(gl, name);
    if (problem) {
      return { isUsable: false, problem };
    }
    return { isUsable: true, name: name || 'an unnamed renderer', textureSets: findReadableTextureSets(gl) };
  } finally {
    loseContext(gl);
  }
}

/**
 * The graphics API to start: { backend, reason } with backend WEBGPU, WEBGL2 or null when neither can start the game.
 * reason says why WebGPU is not used (a WEBGPU_REASONS name, 'forced' for an address that asked for WebGL 2) or
 * 'none'; with no backend, webgl2Problem says why WebGL 2 could not stand in.
 *   policy: the manifest's graphics block; forced: WEBGPU or WEBGL2 when the address asks for one, else null;
 *   webgpu, webgl2: probeWebGPU's and probeWebGL2's results, for the APIs the policy allows.
 */
export function decideGraphics({ policy, forced, webgpu, webgl2 }) {
  const allowsWebGPU = policy.backends.includes(WEBGPU);
  const allowsWebGL2 = policy.backends.includes(WEBGL2);
  if (forced === WEBGL2 && allowsWebGL2) {
    return webgl2?.isUsable
      ? { backend: WEBGL2, reason: 'forced' }
      : { backend: null, reason: 'forced', webgl2Problem: webgl2?.problem };
  }
  if (forced === WEBGPU && allowsWebGPU) {
    return webgpu?.isUsable
      ? { backend: WEBGPU, reason: 'none' }
      : { backend: null, reason: webgpu?.reason ?? 'api_missing' };
  }
  if (allowsWebGPU && webgpu?.isUsable) {
    return { backend: WEBGPU, reason: 'none' };
  }
  const hasWebGL2 = allowsWebGL2 && Boolean(webgl2?.isUsable);
  const reason = allowsWebGPU ? (webgpu?.reason ?? 'api_missing') : 'none';
  if (hasWebGL2) {
    return { backend: WEBGL2, reason };
  }
  return { backend: null, reason, webgl2Problem: allowsWebGL2 ? webgl2?.problem : undefined };
}

/// Whether `reason` means WebGL 2 stands in for a WebGPU the browser could not give: what the player is told about.
export function isFallbackReason(reason) {
  return Object.prototype.hasOwnProperty.call(WEBGPU_REASONS, reason);
}

/**
 * The engine's arguments from the page's address (`search`, one per arg=) and the graphics API it asks for: graphics=
 * or an engine argument --graphics, which the page passes on itself with the API it starts. { args, forced }.
 */
export function readGraphicsArguments(search) {
  const params = new URLSearchParams(search);
  let forced = params.get('graphics');
  const args = [];
  const given = params.getAll('arg');
  for (let i = 0; i < given.length; ++i) {
    const arg = given[i];
    if (arg === '--graphics' || arg === '--graphics-fallback') {
      const value = given[++i];
      forced = arg === '--graphics' ? (forced ?? value) : forced;
      continue;
    }
    if (arg.startsWith('--graphics=')) {
      forced = forced ?? arg.slice('--graphics='.length);
      continue;
    }
    if (arg.startsWith('--graphics-fallback=')) {
      continue;
    }
    args.push(arg);
  }
  return { args, forced: forced === WEBGPU || forced === WEBGL2 ? forced : null };
}

/// The engine's arguments that start `choice`.
export function makeGraphicsArguments(choice) {
  const args = ['--graphics', choice.backend];
  if (choice.backend === WEBGL2 && choice.reason !== 'none') {
    args.push('--graphics-fallback', choice.reason);
  }
  return args;
}

/// What the error screen says when neither API can start the game, with what usually helps.
export function describeFailure(choice) {
  const webgpu = WEBGPU_REASONS[choice.reason];
  const webgl2 = WEBGL2_PROBLEMS[choice.webgl2Problem];
  const sentences = [webgpu?.sentence, webgl2?.sentence, webgpu?.help ?? webgl2?.help].filter(Boolean);
  return sentences.length > 0 ? sentences.join(' ') : 'This browser cannot start the game.';
}

/// The page's remembered answers per game (its path) and reason: whether the player chose to continue on WebGL 2, and
/// whether the notice was shown. Without storage nothing is remembered.
function makeMemoryKey(kind, reason) {
  return `unravel-graphics:${location.pathname}:${kind}:${reason}`;
}

export function recall(kind, reason) {
  try {
    return localStorage.getItem(makeMemoryKey(kind, reason)) !== null;
  } catch {
    return false;
  }
}

export function remember(kind, reason) {
  try {
    localStorage.setItem(makeMemoryKey(kind, reason), '1');
  } catch {
    // Without storage the page asks or tells again next time.
  }
}
