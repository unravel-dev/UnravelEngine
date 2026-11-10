// Starts the browser player: the graphics, the .NET runtime, the game's files, then Main, which starts the engine
// (game/web/Program.cs).
//
// The page chooses the graphics API first (graphics.js): WebGPU, or WebGL 2 where the build allows it and the browser's
// WebGPU is unavailable, as the manifest's graphics block says. Depending on that block it asks the player before the
// download, or tells them once the game runs. For WebGPU the page creates the device, because bgfx cannot wait for it
// inside the engine without JSPI or Asyncify, which the .NET runtime does not use; the engine hands it to bgfx. For
// WebGL 2 bgfx creates the context on the canvas itself. The game's files come from data_manifest.json, which a web
// deploy writes next to this page, into the module's file system at the paths the engine reads them from. The
// engine's arguments come from the page's address, one per arg= (index.html?arg=--log-level&arg=trace), with the
// graphics API the page chose; graphics=webgl2 in the address starts WebGL 2 where the build ships it.
//
// The player's own files (user:/ and cache:/, in the game's folder the manifest names, where SDL puts them) are kept in
// the browser's IndexedDB, in a database of that folder, so games on one site keep their saves apart: read in before
// the engine starts, stored again whenever a written file is closed, and at once when the page is hidden or closed.
//
// The game's files and the runtime's are kept in the browser's Cache Storage, so a later visit downloads only what a
// new deploy changed: a data file under its fingerprint from the manifest, a runtime file under its name, which
// carries its own. Each page keeps its own caches, and drops what its deploy no longer lists once the game started.
// WebGPU and Cache Storage are only given to secure pages: the page is served over HTTPS, or from this computer.
//
// The loading screen (index.html) shows the game's title from the manifest and a bar that the downloads move by
// their bytes: the runtime's, weighed by the manifest's runtime_size, then the game's files by their sizes.
//
// A deploy with several texture sets lists them in the manifest (texture_sets, the one computers read first) and
// names each set's packs (texture_set): the page downloads the packs of the first set the device's GPU reads (on WebGPU
// it lists the set's feature and creates a texture of its format, on WebGL 2 it offers the set's extensions), the
// first set's when it reads none, and the engine mounts them over the game's packs. A deploy for both graphics APIs
// ships their shaders the same way (shader_sets, shader_set), and the page downloads the set of the API it chose.
import { dotnet } from './_framework/dotnet.js';
import {
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
} from './graphics.js';

/// What changes when WebGL 2 stands in for WebGPU, as the page tells the player.
const WEBGL2_CHANGES = 'The game runs on WebGL 2, with simpler lighting and effects.';
/// The text of a link to the build's help page on turning WebGPU on.
const HELP_LINK_TEXT = 'How to turn on WebGPU';
/// How long the notice that WebGL 2 stands in stays, and how long a notice takes to fade (index.html, .notice).
const FALLBACK_NOTICE_MS = 12000;
const NOTICE_FADE_MS = 400;
/// The help links the page opens: a page on the web, never a script.
const WEB_LINK = /^https?:\/\//i;
/// Data files downloaded at once: each is held twice while it goes into the cache and the file system, so fewer keep
/// the page's memory low on a large game.
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

/// Resolves after `count` frames: a change shows on the screen once the frame after it starts.
function nextFrames(count) {
  return new Promise((resolve) => {
    const wait = (left) => (left === 0 ? resolve() : requestAnimationFrame(() => wait(left - 1)));
    wait(count);
  });
}

/// Fades the loading screen out over the game's first frames.
async function hideLoader() {
  loader.fill.style.transition = '';
  setProgress(1);
  await nextFrames(2);
  loader.root.classList.add('is-done');
  await new Promise((resolve) => setTimeout(resolve, LOADER_FADE_MS));
  loader.root.remove();
}

function toMegabytes(bytes) {
  return (bytes / BYTES_PER_MEGABYTE).toFixed(1);
}

/// Counts the bytes of the runtime and of the game's files as they arrive and moves the bar by them up to
/// `downloadsEnd`. Without the files' sizes (an older manifest) the files count by number, weighed as the runtime.
function createDownloadProgress(manifest, downloadsEnd) {
  const runtimeSize = manifest.runtime_size > 0 ? manifest.runtime_size : FALLBACK_RUNTIME_SIZE;
  const files = manifest.files;
  const hasSizes = files.every((file) => Number.isFinite(file.size));
  const dataSize = hasSizes ? files.reduce((sum, file) => sum + file.size, 0) : runtimeSize;
  const totalSize = runtimeSize + dataSize;
  let runtimeBytes = 0;
  let dataBytes = 0;
  let filesDone = 0;
  const update = () => {
    const runtime = Math.min(runtimeBytes, runtimeSize);
    const data = hasSizes ? Math.min(dataBytes, dataSize) : dataSize * filesDone / Math.max(1, files.length);
    const done = (runtime + data) / totalSize;
    setProgress(PROGRESS_DOWNLOADS_START + (downloadsEnd - PROGRESS_DOWNLOADS_START) * done);
    if (hasSizes) {
      loader.detail.textContent = `${toMegabytes(runtime + data)} of ${toMegabytes(totalSize)} MB`;
    }
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
    finishFile: () => {
      filesDone++;
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
  close.textContent = '\u00d7';
  notice.appendChild(close);
  const dismiss = () => {
    notice.classList.add('is-gone');
    setTimeout(() => notice.remove(), NOTICE_FADE_MS);
  };
  close.addEventListener('click', dismiss, { once: true });
  document.body.appendChild(notice);
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

async function openCache(kind) {
  try {
    return globalThis.caches ? await caches.open(`unravel-${kind}:${location.pathname}`) : null;
  } catch {
    return null;
  }
}

async function download(url) {
  const response = await fetch(url, { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error(`${url} could not be downloaded (${response.status}).`);
  }
  return response;
}

/// The runtime's response for `url` from `cache` under `key`, or from the network, which then goes into the cache;
/// its bytes counted into `onBytes`. A download is counted as it arrives: storing it reads it through.
async function fetchCached(cache, url, key, used, onBytes) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return countBytes(cached, onBytes);
  }
  const response = countBytes(await download(url), onBytes);
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

/// The body of `response`, read chunk by chunk into `onBytes`, into a buffer of the `size` the manifest gives.
async function readBytes(response, size, onBytes) {
  if (!response.body || !Number.isFinite(size)) {
    const whole = new Uint8Array(await response.arrayBuffer());
    onBytes(whole.byteLength);
    return whole;
  }
  let bytes = new Uint8Array(size);
  let length = 0;
  const reader = response.body.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) {
      break;
    }
    if (length + value.byteLength > bytes.byteLength) {
      const grown = new Uint8Array(Math.max(bytes.byteLength * 2, length + value.byteLength));
      grown.set(bytes.subarray(0, length));
      bytes = grown;
    }
    bytes.set(value, length);
    length += value.byteLength;
    onBytes(value.byteLength);
  }
  return length === bytes.byteLength ? bytes : bytes.slice(0, length);
}

/// The bytes of a data file from `cache` under `key`, or from the network, then stored in the cache from the same
/// bytes: the download is read once.
async function fetchDataFile(cache, url, key, used, size, onBytes) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return readBytes(cached, size, onBytes);
  }
  const bytes = await readBytes(await download(url), size, onBytes);
  if (cache) {
    await cache.put(key, new Response(bytes)).catch(() => {});
  }
  return bytes;
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

/// Runs `action` on every item, at most `limit` at a time.
async function forEachLimited(items, limit, action) {
  let next = 0;
  const work = async () => {
    while (next < items.length) {
      await action(items[next++]);
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, work));
}

async function loadFiles(module, cache, used, files, progress) {
  await forEachLimited(files, LOAD_CONCURRENCY, async ({ path, hash, size }) => {
    const url = new URL(path, location.href);
    const bytes = await fetchDataFile(cache, url.href, `${url.href}?fingerprint=${hash}`, used, size,
      progress.addDataBytes);
    const slash = path.lastIndexOf('/');
    module.FS_createPath('/', path.substring(0, slash), true, true);
    module.FS_createDataFile('/' + path.substring(0, slash), path.substring(slash + 1), bytes, true, true, true);
    progress.finishFile();
  });
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

async function start() {
  const downloadsBegin = performance.now();
  report('Getting ready');
  const manifest = await (await download('data_manifest.json')).json();
  showTitle(manifest.title);
  const last = readLastTimings();
  const engineShare = last
    ? Math.min(MAX_ENGINE_SHARE, Math.max(MIN_ENGINE_SHARE, last.engine_ms / (last.engine_ms + last.downloads_ms)))
    : DEFAULT_ENGINE_SHARE;
  const downloadsEnd = 1 - engineShare;
  report('Starting the graphics');
  const policy = { ...DEFAULT_GRAPHICS_POLICY, ...manifest.graphics };
  const { args, forced } = readGraphicsArguments(location.search);
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
  const runtimeCache = await openCache('runtime');
  const dataCache = await openCache('data');
  const usedRuntime = new Set();
  const usedData = new Set();
  const runtime = await dotnet
    .withModuleConfig(graphics.device ? { preinitializedWebGPUDevice: graphics.device } : {})
    .withResourceLoader(loadRuntimeResource(runtimeCache, usedRuntime, progress.addRuntimeBytes))
    .withApplicationArguments(...args, ...makeGraphicsArguments(graphics))
    .create();
  progress.finishRuntime();
  const module = runtime.Module;
  report('Loading the game');
  await loadFiles(module, dataCache, usedData, manifest.files, progress);
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
  const result = await runtime.runMain();
  if (result !== 0) {
    throw new Error(`The engine did not start (${result}).`);
  }
  storeTimings({ downloads_ms: Math.round(downloadsMs), engine_ms: Math.round(performance.now() - engineBegin) });
  if (isStored) {
    storeUserFilesWhenHidden(module);
  }
  unlockAudioOnKey();
  console.log('[player] started');
  hideLoader().then(() => tellAboutGraphics(graphics, policy));
  return () => Promise.all([dropUnused(runtimeCache, usedRuntime), dropUnused(dataCache, usedData)]);
}

let dropUnusedFiles = null;
try {
  dropUnusedFiles = await start();
} catch (e) {
  showError(e.message, e.helpLink);
  console.error(e);
}
if (dropUnusedFiles) {
  dropUnusedFiles().catch((e) => console.warn('[player] Older cached files could not be dropped: ' + e.message));
}
