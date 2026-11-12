// Starts the browser player: the graphics, the .NET runtime, the game's files, then Main, which starts the engine
// (game/web/Program.cs).
//
// The page chooses the graphics API first (graphics.js): WebGPU, or WebGL 2 where the build allows it and the browser's
// WebGPU is unavailable, as the manifest's graphics block says. Depending on that block it asks the player before the
// download, or tells them once the game runs. For WebGPU the page creates the device, because bgfx cannot wait for it
// inside the engine without JSPI or Asyncify, which the .NET runtime does not use; the engine hands it to bgfx. For
// WebGL 2 bgfx creates the context on the canvas itself. The engine's arguments come from the page's address, one per
// arg= (index.html?arg=--log-level&arg=trace), with the graphics API the page chose; graphics=webgl2 in the address
// starts WebGL 2 where the build ships it. A page that is not served from this computer passes on only the arguments
// that are harmless in a published game (data.js, filterArguments).
//
// The game's files come from data_manifest.json, which a web deploy writes next to this page, in chunks named by
// their content (data.js): each is checked against the manifest, kept in the browser's Cache Storage for good, and
// downloaded again a few times when it does not arrive. A pack goes chunk by chunk straight into the player's memory,
// where the engine maps it, so the page never holds a second copy of it; the other files go into the module's file
// system at the paths the engine reads them from. The runtime's files are kept in Cache Storage under their names,
// which carry their fingerprints. Each page keeps its own caches, drops what its deploy no longer lists once the game
// started, and drops the caches of pages on its site whose build is gone. Cache Storage and WebGPU are only given to
// secure pages: the page is served over HTTPS, or from this computer.
//
// The player's own files (user:/ and cache:/, in the game's folder the manifest names, where SDL puts them) are kept in
// the browser's IndexedDB, in a database of that folder, so games on one site keep their saves apart: read in before
// the engine starts, stored again whenever a written file is closed, and at once when the page is hidden or closed.
//
// The loading screen (index.html) shows the game's title from the manifest and a bar that the downloads move by
// their bytes: the runtime's, weighed by the manifest's runtime_size, then the game's files by their sizes. Once the
// game runs, the page says so when it stops: the engine ended it, the program failed, or the browser took the
// graphics device away. The canvas would otherwise keep the last frame as if the game had frozen.
//
// A deploy with several texture sets lists them in the manifest (texture_sets, the one computers read first) and
// names each set's packs (texture_set): the page downloads the packs of the first set the device's GPU reads (on WebGPU
// it lists the set's feature and creates a texture of its format, on WebGL 2 it offers the set's extensions), the
// first set's when it reads none, and the engine mounts them over the game's packs. A deploy for both graphics APIs
// ships their shaders the same way (shader_sets, shader_set), and the page downloads the set of the API it chose.
//
// The page talks with the game through globalThis.unravelPage (engine/web/web_page.h, the scripts' WebPage class):
// the functions a page of the project's own offers the game (functions.<name>), and the messages it posts for it
// (post(text)).

// The page's code is loaded under its build's id (index.html), and so is every module it imports: a host's cached
// copy of an older build's code never runs beside this build's.
const BUILD_QUERY = new URL(import.meta.url).search;
const { dotnet } = await import(`./_framework/dotnet.js${BUILD_QUERY}`);
const {
  DEFAULT_GRAPHICS_POLICY,
  WEBGL2,
  WEBGPU,
  WEBGPU_REASONS,
  decideGraphics,
  describeFailure,
  isFallbackReason,
  makeGraphicsArguments,
  probeWebGL2,
  probeWebGPU,
  readGraphicsArguments,
  recall,
  remember,
} = await import(`./graphics.js${BUILD_QUERY}`);
const { resumeStoppedDevices } = await import(`./audio.js${BUILD_QUERY}`);
const {
  MANIFEST_FORMAT,
  MEMORY_FILES_ARGUMENT,
  dropTrimmedAssemblies,
  fetchWithRetry,
  filterArguments,
  loadFiles,
} = await import(`./data.js${BUILD_QUERY}`);

/// What changes when WebGL 2 stands in for WebGPU, as the page tells the player.
const WEBGL2_CHANGES = 'The game runs on WebGL 2, with simpler lighting and effects.';
/// The text of a link to the build's help page on turning WebGPU on.
const HELP_LINK_TEXT = 'How to turn on WebGPU';
/// How long the notice that WebGL 2 stands in stays, and how long a notice takes to fade (index.html, .notice).
const FALLBACK_NOTICE_MS = 12000;
const NOTICE_FADE_MS = 400;
/// The help links the page opens: a page on the web, never a script.
const WEB_LINK = /^https?:\/\//i;
/// Chunks downloaded at once: each is held until it is checked, kept and written, so fewer keep the page's memory
/// low.
const LOAD_CONCURRENCY = 4;
/// Where the downloads start on the loading bar. They end where the engine's start takes over, which holds the page
/// without a repaint: the bar creeps through it on its own (index.html, a transition the browser runs while the page
/// is held). The page keeps how long its last start's downloads and engine start took, so a later visit splits the
/// bar and paces the creep by them; a first visit takes the defaults.
const PROGRESS_DOWNLOADS_START = 0.03;
const DEFAULT_ENGINE_SHARE = 0.3;
const MIN_ENGINE_SHARE = 0.15;
const MAX_ENGINE_SHARE = 0.6;
const DEFAULT_ENGINE_START_MS = 10000;
/// The creep takes a little longer than the last start and stops short of the end: only the started game fills it.
const ENGINE_CREEP_SLACK = 1.2;
const ENGINE_CREEP_END = 0.99;
/// Where the page keeps its last start's timings.
const TIMINGS_KEY = `unravel-loading:${location.pathname}`;
/// The runtime's weight when the manifest gives none: about the player's runtime.
const FALLBACK_RUNTIME_SIZE = 40 * 1024 * 1024;
const BYTES_PER_MEGABYTE = 1024 * 1024;
/// How long the loading screen takes to fade out (index.html, .loader), with a frame to spare.
const LOADER_FADE_MS = 650;
/// What the names of the page's caches start with, before the kind and the page's path; the kinds this page keeps,
/// and the one pages of older builds kept whole data files in.
const CACHE_PREFIX = 'unravel-';
const RUNTIME_CACHE = 'runtime';
const CHUNKS_CACHE = 'chunks';
const LEGACY_DATA_CACHE = 'data';
/// The manifest beside every page, which tells a page that is gone from one that is there.
const MANIFEST_NAME = 'data_manifest.json';
/// The boundary a pack's memory starts on, as a mapped file's does.
const MEMORY_ALIGNMENT = 65536;
/// The WebGPU feature a GPU reads each texture set's formats with, by the manifest's set names, and a format of the
/// feature the page creates a texture of before it trusts it: a device can list a feature whose formats it refuses
/// (Safari on an iPhone 15 Pro lists texture-compression-bc and fails every BC texture).
const TEXTURE_SET_FEATURES = {
  bc: { feature: 'texture-compression-bc', format: 'bc1-rgba-unorm' },
  astc: { feature: 'texture-compression-astc', format: 'astc-4x4-unorm' },
  etc2: { feature: 'texture-compression-etc2', format: 'etc2-rgb8unorm' },
};
/// The side of the texture the page tries a format with: one compression block.
const TEXTURE_SET_TEST_SIDE = 4;
/// The adapter information a device gets when the browser has none: the engine's WebGPU bindings read every field.
const EMPTY_ADAPTER_INFO = {
  vendor: '',
  architecture: '',
  device: '',
  description: '',
  subgroupMinSize: 0,
  subgroupMaxSize: 0,
};
/// What the page says of a game that stopped, by what stopped it.
const STOPPED_TEXTS = Object.freeze({
  ended: { title: 'The game has ended.', text: 'Reload the page to play again.' },
  failed: {
    title: 'The game stopped because of an error.',
    text: 'Reload the page to start it again. The browser\'s console says what went wrong.',
  },
  device_lost: {
    title: 'The game lost its graphics device.',
    text: 'The browser took the GPU away from the page, as it does when its graphics process restarts or a driver '
      + 'is updated. Reload the page to go on playing.',
  },
});

const loader = {
  root: document.getElementById('loader'),
  title: document.getElementById('loader-title'),
  bar: document.getElementById('loader-bar'),
  fill: document.getElementById('loader-fill'),
  stage: document.getElementById('loader-stage'),
  percent: document.getElementById('loader-percent'),
  detail: document.getElementById('loader-detail'),
  error: document.getElementById('loader-error'),
  errorText: document.getElementById('loader-error-text'),
  errorHelp: document.getElementById('loader-error-help'),
  retry: document.getElementById('loader-retry'),
  ask: document.getElementById('loader-ask'),
  askText: document.getElementById('loader-ask-text'),
  askAdvice: document.getElementById('loader-ask-advice'),
  askHelp: document.getElementById('loader-ask-help'),
  askContinue: document.getElementById('loader-ask-continue'),
  progress: 0,
};

const stopped = {
  root: document.getElementById('stopped'),
  title: document.getElementById('stopped-title'),
  text: document.getElementById('stopped-text'),
  reload: document.getElementById('stopped-reload'),
};

/// Whether the game runs: from the engine's start until the page said it stopped.
let isGameRunning = false;

/// SDL's window takes the page once the engine creates it, and hides every element of the body but the canvas. The
/// screens the page shows over the game stay above it as children of the document itself.
function keepAboveGame(element) {
  document.documentElement.appendChild(element);
}

function report(stage) {
  loader.stage.textContent = stage;
  loader.detail.textContent = '';
  console.log('[player] ' + stage);
}

function showTitle(title) {
  if (!title) {
    return;
  }
  document.title = title;
  loader.title.textContent = title;
  loader.title.classList.add('is-shown');
}

function slideFill(fraction) {
  loader.fill.style.transform = `translateX(${((fraction - 1) * 100).toFixed(3)}%)`;
}

/// Moves the bar to `fraction` (0 to 1); it never goes back. The fill glides there (index.html).
function setProgress(fraction) {
  if (fraction <= loader.progress) {
    return;
  }
  loader.progress = Math.min(1, fraction);
  slideFill(loader.progress);
  const percent = Math.floor(loader.progress * 100);
  loader.percent.textContent = `${percent}%`;
  loader.bar.setAttribute('aria-valuenow', String(percent));
}

/// Lets the bar creep to `fraction` over `ms` on its own while the page is held, without its percentage.
function creepProgress(fraction, ms) {
  loader.root.classList.add('is-starting');
  loader.fill.style.transition = `transform ${Math.round(ms)}ms cubic-bezier(0.25, 0.6, 0.35, 1)`;
  slideFill(fraction);
}

/// How long the page's last start took ({ downloads_ms, engine_ms }), or null.
function readLastTimings() {
  try {
    const timings = JSON.parse(localStorage.getItem(TIMINGS_KEY) ?? 'null');
    return timings && timings.downloads_ms >= 0 && timings.engine_ms > 0 ? timings : null;
  } catch {
    return null;
  }
}

function storeTimings(timings) {
  try {
    localStorage.setItem(TIMINGS_KEY, JSON.stringify(timings));
  } catch {
    // Without storage the next visit takes the defaults.
  }
}

/// Points `anchor` at the build's help page; hidden without one.
function showHelpLink(anchor, link) {
  anchor.hidden = !WEB_LINK.test(link ?? '');
  if (!anchor.hidden) {
    anchor.href = link;
  }
}

function showError(message, helpLink) {
  loader.root.classList.add('has-error');
  loader.errorText.textContent = message;
  showHelpLink(loader.errorHelp, helpLink);
  loader.error.hidden = false;
  loader.retry.addEventListener('click', () => location.reload());
  console.log('[player] The game could not start: ' + message);
}

/// Covers the game with the screen that says why it stopped, once: what comes after the first reason follows from it.
function showStopped(reason) {
  if (!isGameRunning) {
    return;
  }
  isGameRunning = false;
  const { title, text } = STOPPED_TEXTS[reason];
  stopped.title.textContent = title;
  stopped.text.textContent = text;
  stopped.root.hidden = false;
  stopped.reload.focus();
  console.log(`[player] ${title}`);
}

/// Whether `error` ended the program: a trap or an abort inside the module, or memory that ran out. Anything else a
/// page's script throws leaves the game running.
function isProgramFailure(error) {
  const message = String(error?.message ?? error ?? '');
  return error instanceof WebAssembly.RuntimeError || /^Aborted\(|out of memory/i.test(message);
}

/// Says so when the running game's program fails: the error reaches the page as an uncaught one.
function watchForProgramFailure() {
  const check = (error) => isProgramFailure(error) && showStopped('failed');
  window.addEventListener('error', (event) => check(event.error ?? event.message));
  window.addEventListener('unhandledrejection', (event) => check(event.reason));
}

/// Says so when the browser takes the graphics device from the running game. A device the page destroyed itself is
/// not lost.
function watchForDeviceLoss(graphics) {
  if (graphics.device) {
    graphics.device.lost.then((info) => info.reason !== 'destroyed' && showStopped('device_lost'));
    return;
  }
  document.getElementById('canvas').addEventListener('webglcontextlost', () => showStopped('device_lost'));
}

/// The object the page and the game talk through. A page of the project's own may have created it before this
/// script ran, with the functions it offers the game.
function createPageInterface() {
  const page = (globalThis.unravelPage ??= {});
  page.functions ??= {};
  page.messages ??= [];
  page.post ??= (text) => {
    page.messages.push(String(text));
  };
  page.onGameEnded = () => showStopped('ended');
  return page;
}

/// Resolves after `count` frames: a change shows on the screen once the frame after it starts.
function nextFrames(count) {
  return new Promise((resolve) => {
    const wait = (left) => (left === 0 ? resolve() : requestAnimationFrame(() => wait(left - 1)));
    wait(count);
  });
}

function wait(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/// Fades the loading screen out over the game's first frames.
async function hideLoader() {
  loader.fill.style.transition = '';
  setProgress(1);
  await nextFrames(2);
  loader.root.classList.add('is-done');
  await wait(LOADER_FADE_MS);
  loader.root.remove();
}

function toMegabytes(bytes) {
  return (bytes / BYTES_PER_MEGABYTE).toFixed(1);
}

/// Counts the bytes of the runtime and of the game's files as they arrive and moves the bar by them up to
/// `downloadsEnd`.
function createDownloadProgress(manifest, downloadsEnd) {
  const runtimeSize = manifest.runtime_size > 0 ? manifest.runtime_size : FALLBACK_RUNTIME_SIZE;
  const dataSize = manifest.files.reduce((sum, file) => sum + file.size, 0);
  const totalSize = runtimeSize + dataSize;
  let runtimeBytes = 0;
  let dataBytes = 0;
  const update = () => {
    const done = Math.min(runtimeBytes, runtimeSize) + Math.min(Math.max(dataBytes, 0), dataSize);
    setProgress(PROGRESS_DOWNLOADS_START + (downloadsEnd - PROGRESS_DOWNLOADS_START) * done / totalSize);
    loader.detail.textContent = `${toMegabytes(done)} of ${toMegabytes(totalSize)} MB`;
  };
  return {
    addRuntimeBytes: (count) => {
      runtimeBytes += count;
      update();
    },
    finishRuntime: () => {
      runtimeBytes = runtimeSize;
      update();
    },
    addDataBytes: (count) => {
      dataBytes += count;
      update();
    },
  };
}

/// A device of `adapter` with every limit it offers and every feature but `excludedFeatures`.
async function requestDeviceFrom(adapter, excludedFeatures) {
  const requiredLimits = {};
  for (const key in adapter.limits) {
    requiredLimits[key] = adapter.limits[key];
  }
  const requiredFeatures = [...adapter.features].filter((feature) => !excludedFeatures.includes(feature));
  const device = await adapter.requestDevice({ requiredFeatures, requiredLimits });
  // The engine's WebGPU bindings read the device's adapterInfo, which Safari's devices lack: they get the adapter's.
  if (!device.adapterInfo) {
    Object.defineProperty(device, 'adapterInfo', { value: adapter.info ?? EMPTY_ADAPTER_INFO, configurable: true });
  }
  return device;
}

/// Whether `device` creates a texture of `format`; a creation that throws counts as refused.
async function canCreateTexture(device, format) {
  device.pushErrorScope('validation');
  let texture = null;
  try {
    texture = device.createTexture({
      size: [TEXTURE_SET_TEST_SIDE, TEXTURE_SET_TEST_SIDE],
      format,
      usage: GPUTextureUsage.TEXTURE_BINDING,
    });
  } catch {
    texture = null;
  }
  const error = await device.popErrorScope();
  texture?.destroy();
  return texture !== null && !error;
}

/// The compression features `device` lists whose formats it refuses to create textures of.
async function findRefusedCompressionFeatures(device) {
  const refused = [];
  for (const { feature, format } of Object.values(TEXTURE_SET_FEATURES)) {
    if (device.features.has(feature) && !(await canCreateTexture(device, format))) {
      refused.push(feature);
    }
  }
  return refused;
}

/// The page's WebGPU device on the probed `adapter`. A compression feature the device lists but refuses textures of
/// is left out of a second device, from a new adapter (an adapter gives one device), so the engine decodes those
/// formats when they load instead of failing every texture of them. Throws when no device can be created.
async function createDevice(adapter) {
  let device = await requestDeviceFrom(adapter, []);
  const refused = await findRefusedCompressionFeatures(device);
  if (refused.length > 0) {
    console.warn(`[player] The GPU lists ${refused.join(', ')} but refuses its textures: they are decoded when they load.`);
    device.destroy();
    const again = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!again) {
      throw new Error('The browser gave no second WebGPU adapter.');
    }
    device = await requestDeviceFrom(again, refused);
  }
  return device;
}

/// The graphics the game starts with: decideGraphics's choice over the probes of the APIs the build allows, with the
/// WebGPU device when it chose WebGPU. A device that cannot be created lets WebGL 2 stand in where the build allows it.
async function chooseGraphics(policy, forced) {
  const allowsWebGL2 = policy.backends.includes(WEBGL2);
  const isWebGL2Forced = forced === WEBGL2 && allowsWebGL2;
  let webgpu = policy.backends.includes(WEBGPU) && !isWebGL2Forced ? await probeWebGPU() : undefined;
  // WebGL 2 is probed only where it may start the game: its throwaway contexts cost a little.
  let webgl2 = allowsWebGL2 && !webgpu?.isUsable ? probeWebGL2() : undefined;
  const choice = decideGraphics({ policy, forced, webgpu, webgl2 });
  if (choice.backend !== WEBGPU) {
    return { ...choice, webgpu, webgl2 };
  }
  try {
    return { ...choice, device: await createDevice(webgpu.adapter), webgpu, webgl2 };
  } catch (e) {
    console.warn('[player] The WebGPU device could not be created: ' + e.message);
    webgpu = { isUsable: false, reason: 'device_failed' };
    webgl2 = webgl2 ?? (allowsWebGL2 ? probeWebGL2() : undefined);
    return { ...decideGraphics({ policy, forced, webgpu, webgl2 }), webgpu, webgl2 };
  }
}

/// One console line on what the game renders with and why, for a developer reading a player's console.
function logGraphics(graphics) {
  if (graphics.backend === WEBGPU) {
    console.log(`[player] Graphics: WebGPU on ${graphics.webgpu.name}`);
    return;
  }
  const instead = graphics.reason === 'none' ? '' : ` in place of WebGPU (${graphics.reason})`;
  console.log(`[player] Graphics: WebGL 2 on ${graphics.webgl2.name}${instead}`);
}

/// The texture sets whose formats the chosen API reads: the WebGPU device's features (createDevice leaves out the
/// features it refuses), or the WebGL 2 extensions.
function findReadableTextureSets(graphics) {
  if (graphics.backend === WEBGL2) {
    return graphics.webgl2.textureSets;
  }
  const features = graphics.device.features;
  return new Set(Object.keys(TEXTURE_SET_FEATURES).filter((set) => features.has(TEXTURE_SET_FEATURES[set].feature)));
}

/// The texture set the page downloads: the first of the manifest's sets in `readable`, the first one when none is (the
/// engine decodes those textures when they load); null for a deploy with one set.
function chooseTextureSet(sets, readable) {
  if (!sets || sets.length === 0) {
    return null;
  }
  return sets.find((set) => readable.has(set)) ?? sets[0];
}

/// The manifest's files the page downloads: every file but the packs of the texture sets it did not choose and of the
/// other graphics API's shaders.
function selectDataFiles(files, textureSet, backend) {
  return files.filter((file) => (!file.texture_set || file.texture_set === textureSet)
    && (!file.shader_set || file.shader_set === backend));
}

/// A notice above the canvas, which takes the page once the engine starts, with the build's help page when `link`
/// names one; it goes when closed, and after `duration` ms when one is given.
function showNotice({ text, link = '', duration = 0 }) {
  const notice = document.createElement('div');
  notice.className = 'notice';
  notice.setAttribute('role', 'status');
  const body = document.createElement('span');
  body.textContent = text;
  notice.appendChild(body);
  if (WEB_LINK.test(link)) {
    const anchor = document.createElement('a');
    anchor.className = 'notice-link';
    anchor.href = link;
    anchor.target = '_blank';
    anchor.rel = 'noopener';
    anchor.textContent = HELP_LINK_TEXT;
    notice.append(' ', anchor);
  }
  const close = document.createElement('button');
  close.className = 'notice-close';
  close.type = 'button';
  close.setAttribute('aria-label', 'Close');
  close.textContent = '\u00D7';
  notice.appendChild(close);
  const dismiss = () => {
    notice.classList.add('is-gone');
    setTimeout(() => notice.remove(), NOTICE_FADE_MS);
  };
  close.addEventListener('click', dismiss, { once: true });
  keepAboveGame(notice);
  if (duration > 0) {
    setTimeout(dismiss, duration);
  }
}

/// What the page says about WebGPU being unavailable for `reason`: its sentence, and the advice unless the build's help
/// page gives it.
function describeReason(reason, helpLink) {
  const { sentence, help } = WEBGPU_REASONS[reason];
  return WEB_LINK.test(helpLink) ? sentence : `${sentence} ${help}`;
}

/// Before the download, asks the player whether to continue on WebGL 2 (the build's "ask"), once per game and reason:
/// resolves when they continue. The page holds there otherwise; the help link opens beside it.
function askToContinue(reason, policy) {
  const { sentence, help } = WEBGPU_REASONS[reason];
  report('Waiting for your answer');
  loader.root.classList.add('is-asking');
  loader.askText.textContent = `${sentence} ${WEBGL2_CHANGES}`;
  loader.askAdvice.textContent = `To play with WebGPU: ${help}`;
  showHelpLink(loader.askHelp, policy.help_link);
  loader.ask.hidden = false;
  loader.askContinue.focus();
  return new Promise((resolve) => {
    loader.askContinue.addEventListener('click', () => {
      remember('asked', reason);
      loader.ask.hidden = true;
      loader.root.classList.remove('is-asking');
      resolve();
    }, { once: true });
  });
}

/// The notice of a started game that WebGL 2 stands in (the build's "tell"), once per game and reason.
function tellAboutGraphics(graphics, policy) {
  if (graphics.backend !== WEBGL2 || !isFallbackReason(graphics.reason) || policy.fallback_notice !== 'tell'
    || recall('told', graphics.reason)) {
    return;
  }
  remember('told', graphics.reason);
  showNotice({
    text: `${WEBGL2_CHANGES} ${describeReason(graphics.reason, policy.help_link)}`,
    link: policy.help_link,
    duration: FALLBACK_NOTICE_MS,
  });
}

function getCacheName(kind, pagePath) {
  return `${CACHE_PREFIX}${kind}:${pagePath}`;
}

async function openCache(kind) {
  try {
    return globalThis.caches ? await caches.open(getCacheName(kind, location.pathname)) : null;
  } catch {
    return null;
  }
}

/// The host's answer for `url`; throws when it is not the file. `cacheMode` is the browser's own cache's part in it.
async function download(url, cacheMode = 'no-cache') {
  const response = await fetch(url, { cache: cacheMode });
  if (!response.ok) {
    throw new Error(`${url} could not be downloaded (${response.status}).`);
  }
  return response;
}

/// The runtime's response for `url` from `cache` under `key`, or from the network, which then goes into the cache;
/// its bytes counted into `onBytes`. A download is counted as it arrives: storing it reads it through. A host with a
/// passing problem is asked again.
async function fetchCached(cache, url, key, used, onBytes) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return countBytes(cached, onBytes);
  }
  const answer = await fetchWithRetry(() => fetch(url, { cache: 'no-cache' }), wait);
  if (!answer.ok) {
    throw new Error(`${url} could not be downloaded (${answer.status}).`);
  }
  const response = countBytes(answer, onBytes);
  if (cache) {
    await cache.put(key, response.clone()).catch(() => {});
  }
  return response;
}

/// `response` with its body counted into `onBytes` as it is read.
function countBytes(response, onBytes) {
  if (!response.body) {
    return response;
  }
  const reader = response.body.getReader();
  const body = new ReadableStream({
    async pull(controller) {
      const { done, value } = await reader.read();
      if (done) {
        controller.close();
        return;
      }
      onBytes(value.byteLength);
      controller.enqueue(value);
    },
    cancel(reason) {
      return reader.cancel(reason);
    },
  });
  return new Response(body, { status: response.status, statusText: response.statusText, headers: response.headers });
}

/// The body of `response` as bytes, counted into `onBytes` as it arrives.
async function readBytes(response, onBytes) {
  if (!response.body) {
    const whole = new Uint8Array(await response.arrayBuffer());
    onBytes(whole.byteLength);
    return whole;
  }
  const parts = [];
  let length = 0;
  const reader = response.body.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) {
      break;
    }
    parts.push(value);
    length += value.byteLength;
    onBytes(value.byteLength);
  }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) {
    bytes.set(part, offset);
    offset += part.byteLength;
  }
  return bytes;
}

/// What data.js loads chunks through: the manifest's chunks folder on the host, and the page's cache of chunks. A
/// chunk the page keeps itself stays out of the browser's own cache, which would hold it a second time.
function createChunkIo(folder, cache, used, onBytes) {
  const getUrl = (name) => new URL(`${folder}/${name}`, location.href).href;
  return {
    readCache: async (name) => {
      used.add(getUrl(name));
      const kept = cache ? await cache.match(getUrl(name)) : undefined;
      return kept ? new Uint8Array(await kept.arrayBuffer()) : null;
    },
    writeCache: async (name, bytes) => {
      if (cache) {
        await cache.put(getUrl(name), new Response(bytes)).catch(() => {});
      }
    },
    dropCache: async (name) => {
      await cache?.delete(getUrl(name));
    },
    download: async (name, count) => readBytes(await download(getUrl(name), cache ? 'no-store' : 'default'), count),
    wait,
    onBytes,
  };
}

/// The module's memory as it is now: growing it replaces the buffer, so a view is never kept.
function getHeap(runtime) {
  return runtime.localHeapViewU8 ? runtime.localHeapViewU8() : runtime.Module.HEAPU8;
}

/// Where `size` bytes for a pack start in the module's memory. Never freed: a pack stays mapped while the game runs.
function allocatePackMemory(module, size) {
  const block = module._malloc(size + MEMORY_ALIGNMENT) >>> 0;
  if (block === 0) {
    throw new Error(`The browser has no memory left for ${toMegabytes(size)} MB of the game's files.`);
  }
  return block + (MEMORY_ALIGNMENT - (block % MEMORY_ALIGNMENT)) % MEMORY_ALIGNMENT;
}

/// Puts `content` into the module's file system at `path`, where the engine reads it as a file.
function createFile(module, path, content) {
  const slash = path.lastIndexOf('/');
  module.FS_createPath('/', path.substring(0, slash), true, true);
  module.FS_createDataFile('/' + path.substring(0, slash), path.substring(slash + 1), content, true, true, true);
}

/// The writer of a data file's chunks (data.js, loadFiles). A file the engine maps (a pack) is written straight into
/// the module's memory and named to the engine in `memoryFiles`, as "path:address:size"; any other becomes a file of
/// the module's file system once it is whole.
function openDataFile(runtime, file, memoryFiles) {
  const module = runtime.Module;
  if (file.mapped && file.size > 0 && typeof module._malloc === 'function') {
    const address = allocatePackMemory(module, file.size);
    return {
      write: (offset, bytes) => getHeap(runtime).set(bytes, address + offset),
      close: () => memoryFiles.push(`${file.path}:${address}:${file.size}`),
    };
  }
  const content = new Uint8Array(file.size);
  return {
    write: (offset, bytes) => content.set(bytes, offset),
    close: () => createFile(module, file.path, content),
  };
}

/// Starts the engine. When it does not start, the cached chunks go: one of them may be what stops it, and the next
/// visit then downloads them again instead of reading the same bytes back.
async function runEngine(runtime, engineArguments, chunkCache) {
  const dropCachedData = () => dropUnused(chunkCache, new Set()).catch(() => {});
  let result = -1;
  try {
    result = await runtime.runMain(undefined, engineArguments);
  } catch (e) {
    await dropCachedData();
    throw e;
  }
  if (result !== 0) {
    await dropCachedData();
    throw new Error(`The engine did not start (${result}).`);
  }
}

/// Drops what `cache` holds that this start did not use: files an older deploy had.
async function dropUnused(cache, used) {
  if (!cache) {
    return;
  }
  for (const request of await cache.keys()) {
    if (!used.has(request.url)) {
      await cache.delete(request);
    }
  }
}

/// The folder a page at `pagePath` lies in, with its last '/'.
function getPageFolder(pagePath) {
  return pagePath.substring(0, pagePath.lastIndexOf('/') + 1);
}

/// Whether the host says for sure that no page is at `pagePath` any more: its manifest is gone. A host that cannot
/// be asked says nothing.
async function isPageGone(pagePath) {
  try {
    const manifest = new URL(getPageFolder(pagePath) + MANIFEST_NAME, location.origin);
    const answer = await fetch(manifest, { method: 'HEAD', cache: 'no-store' });
    return answer.status === 404 || answer.status === 410;
  } catch {
    return false;
  }
}

/// Drops the caches this site's pages no longer need: the one pages of older builds kept this page's data files in,
/// and the caches of other pages whose build is gone from the host (a game that moved, a profile that was renamed).
async function dropStaleCaches() {
  if (!globalThis.caches) {
    return;
  }
  await caches.delete(getCacheName(LEGACY_DATA_CACHE, location.pathname));
  for (const name of await caches.keys()) {
    const pagePath = name.startsWith(CACHE_PREFIX) ? name.substring(name.indexOf(':') + 1) : '';
    if (pagePath && getPageFolder(pagePath) !== getPageFolder(location.pathname) && await isPageGone(pagePath)) {
      await caches.delete(name);
    }
  }
}

/// The runtime's binary files (assemblies, the wasm module, globalization data) from the cache, counted for the
/// loading bar; its JavaScript modules load as the runtime loads them.
const CACHED_RUNTIME_TYPES = new Set(['assembly', 'pdb', 'dotnetwasm', 'globalization']);

function loadRuntimeResource(cache, used, onBytes) {
  return (type, name, defaultUri) => {
    if (!CACHED_RUNTIME_TYPES.has(type)) {
      return null;
    }
    const url = new URL(defaultUri, location.href).href;
    return fetchCached(cache, url, url, used, onBytes);
  };
}

function syncFiles(module, populate) {
  return new Promise((resolve, reject) => module.FS.syncfs(populate, (error) => (error ? reject(error) : resolve())));
}

/// Keeps the game's folder in IndexedDB and reads the stored files in. When they cannot be read, the folder stays in
/// memory only: storing it would replace the saves it could not read. True when the folder is stored.
async function mountUserFiles(module, folder) {
  module.FS.mkdirTree(folder);
  module.FS.mount(module.FS.filesystems.IDBFS, { autoPersist: true }, folder);
  try {
    await syncFiles(module, true);
    return true;
  } catch (e) {
    console.warn('[player] The saved files could not be read, so the game starts without them and keeps what it '
      + 'writes in memory only: ' + e.message);
    module.FS.unmount(folder);
    module.FS.mkdirTree(folder);
    return false;
  }
}

/// A written file is stored on a timer: going hidden (another tab, the browser minimized, a phone switching apps)
/// stores the files at once, in case the browser ends the page from there. Registered after the engine's own listener,
/// which lets the game save first (OnApplicationPause). A tab that closes or reloads goes hidden as it unloads, after
/// pagehide, and an unloading page cannot finish the store, which IndexedDB makes asynchronous: both try, no more.
function storeUserFilesWhenHidden(module) {
  const store = () => module.FS.syncfs(false, (error) => error && console.warn('[player] ' + error));
  document.addEventListener('visibilitychange', () => document.visibilityState === 'hidden' && store());
  window.addEventListener('pagehide', store);
}

/// miniaudio starts the sound on the page's first click or touch, as browsers require; a key starts it too, for a game
/// played with the keyboard. A key that is no user activation (Escape) cannot start it, so the page keeps listening
/// until one does.
function unlockAudioOnKey() {
  const unlock = () => {
    if (navigator.userActivation && !navigator.userActivation.isActive) {
      return;
    }
    globalThis.miniaudio?.unlock();
    document.removeEventListener('keydown', unlock, true);
  };
  document.addEventListener('keydown', unlock, true);
}

/// Safari stops the page's sound while the page is hidden (another app, the screen off) and leaves it stopped. When the
/// page shows again, and at the player's next touch for a browser that wants one first, the devices that play are told
/// to go on.
function resumeAudioWhenShown() {
  const resume = () => resumeStoppedDevices(globalThis.miniaudio);
  document.addEventListener('visibilitychange', () => document.visibilityState === 'visible' && resume());
  document.addEventListener('touchend', resume, true);
}

/// The engine arguments the page's address gives, without the ones a page that is not this computer's must not pass
/// on, and the graphics API the address asks for.
function readAddressArguments() {
  const { args, forced } = readGraphicsArguments(location.search);
  const passed = filterArguments(args, location.hostname);
  if (passed.length < args.length) {
    console.warn(`[player] ${args.length - passed.length} argument(s) of the page's address are not passed to a `
      + 'game that is not served from this computer.');
  }
  return { args: passed, forced };
}

async function start(manifest) {
  const downloadsBegin = performance.now();
  report('Getting ready');
  if (manifest.format !== MANIFEST_FORMAT) {
    throw new Error('The page and the build\'s list of files come from different engine versions. Deploy the build '
      + 'again with its dependencies.');
  }
  showTitle(manifest.title);
  createPageInterface();
  const last = readLastTimings();
  const engineShare = last
    ? Math.min(MAX_ENGINE_SHARE, Math.max(MIN_ENGINE_SHARE, last.engine_ms / (last.engine_ms + last.downloads_ms)))
    : DEFAULT_ENGINE_SHARE;
  const downloadsEnd = 1 - engineShare;
  report('Starting the graphics');
  const policy = { ...DEFAULT_GRAPHICS_POLICY, ...manifest.graphics };
  const { args, forced } = readAddressArguments();
  const graphics = await chooseGraphics(policy, forced);
  if (!graphics.backend) {
    throw Object.assign(new Error(describeFailure(graphics)), { helpLink: policy.help_link });
  }
  logGraphics(graphics);
  if (policy.fallback_notice === 'ask' && isFallbackReason(graphics.reason) && !recall('asked', graphics.reason)) {
    await askToContinue(graphics.reason, policy);
    report('Starting the graphics');
  }
  const textureSet = chooseTextureSet(manifest.texture_sets, findReadableTextureSets(graphics));
  if (textureSet) {
    console.log(`[player] textures: ${textureSet}`);
  }
  manifest.files = selectDataFiles(manifest.files, textureSet, graphics.backend);
  const progress = createDownloadProgress(manifest, downloadsEnd);
  setProgress(PROGRESS_DOWNLOADS_START);
  report('Loading the runtime');
  const runtimeCache = await openCache(RUNTIME_CACHE);
  const chunkCache = await openCache(CHUNKS_CACHE);
  const usedRuntime = new Set();
  const usedChunks = new Set();
  const runtime = await dotnet
    .withModuleConfig({
      ...(graphics.device ? { preinitializedWebGPUDevice: graphics.device } : {}),
      onConfigLoaded: (config) => dropTrimmedAssemblies(config, manifest.trimmed),
    })
    .withResourceLoader(loadRuntimeResource(runtimeCache, usedRuntime, progress.addRuntimeBytes))
    .create();
  progress.finishRuntime();
  const module = runtime.Module;
  report('Loading the game');
  const memoryFiles = [];
  await loadFiles(manifest.files,
    LOAD_CONCURRENCY,
    createChunkIo(manifest.chunks, chunkCache, usedChunks, progress.addDataBytes),
    (file) => openDataFile(runtime, file, memoryFiles));
  const isStored = await mountUserFiles(module, manifest.user_folder);
  const downloadsMs = performance.now() - downloadsBegin;
  report('Starting the game');
  setProgress(downloadsEnd);
  // Starting the engine holds the page until it returns: the stage is painted and the creep handed to the browser
  // first.
  await nextFrames(2);
  creepProgress(ENGINE_CREEP_END, (last ? last.engine_ms : DEFAULT_ENGINE_START_MS) * ENGINE_CREEP_SLACK);
  await nextFrames(2);
  const engineBegin = performance.now();
  const engineArguments = [...args, ...makeGraphicsArguments(graphics)];
  if (memoryFiles.length > 0) {
    engineArguments.push(MEMORY_FILES_ARGUMENT, memoryFiles.join('|'));
  }
  await runEngine(runtime, engineArguments, chunkCache);
  isGameRunning = true;
  watchForProgramFailure();
  watchForDeviceLoss(graphics);
  storeTimings({ downloads_ms: Math.round(downloadsMs), engine_ms: Math.round(performance.now() - engineBegin) });
  if (isStored) {
    storeUserFilesWhenHidden(module);
  }
  unlockAudioOnKey();
  resumeAudioWhenShown();
  console.log('[player] started');
  hideLoader().then(() => tellAboutGraphics(graphics, policy));
  return () => Promise.all([dropUnused(runtimeCache, usedRuntime), dropUnused(chunkCache, usedChunks), dropStaleCaches()]);
}

/// Starts the game the manifest describes, and shows on the loading screen why it could not start.
export async function run(manifest) {
  keepAboveGame(loader.root);
  keepAboveGame(stopped.root);
  stopped.reload.addEventListener('click', () => location.reload());
  let dropStaleFiles = null;
  try {
    dropStaleFiles = await start(manifest);
  } catch (e) {
    showError(e.message, e.helpLink);
    console.error(e);
  }
  if (dropStaleFiles) {
    dropStaleFiles().catch((e) => console.warn('[player] Older cached files could not be dropped: ' + e.message));
  }
}
