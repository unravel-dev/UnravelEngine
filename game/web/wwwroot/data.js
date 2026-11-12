// The game's data as the page downloads it (game/web/wwwroot/main.js).
//
// A web deploy cuts every data file into chunks and names each by its content (editor/core/deploy/web_chunks.h). The
// manifest lists a file's chunks in order, each with its size and a check of its bytes. A chunk is immutable under
// its name, so the page keeps it for good once it has it: a download that drops costs one chunk, and a later visit
// downloads only the chunks a new deploy changed. Every chunk is checked before it is used or kept: bytes of another
// size or check are not the build's (a host's page for a missing file, a download cut short), and are downloaded
// again a few times before the start fails.
//
// Nothing here touches the page or the network by itself: the downloads and the cache come in as functions, so
// game/web/tests checks this module in Node.

/// How often a chunk is downloaded before the start fails, and how long the page waits before each further try.
export const CHUNK_ATTEMPTS = 5;
export const RETRY_DELAYS_MS = Object.freeze([500, 1500, 4000, 8000]);
/// The manifest format this page reads (web.cpp, WEB_MANIFEST_FORMAT).
export const MANIFEST_FORMAT = 2;

const PRIME32_1 = 0x9E3779B1;
const PRIME32_2 = 0x85EBCA77;
const PRIME32_3 = 0xC2B2AE3D;
const PRIME32_4 = 0x27D4EB2F;
const PRIME32_5 = 0x165667B1;
/// The bytes one round of the check's main loop takes: four 32-bit words.
const STRIPE_SIZE = 16;
const WORD_SIZE = 4;

function rotateLeft(value, bits) {
  return (value << bits) | (value >>> (32 - bits));
}

function mixWord(accumulator, word) {
  return Math.imul(rotateLeft((accumulator + Math.imul(word, PRIME32_2)) | 0, 13), PRIME32_1);
}

/// Reads the little-endian 32-bit words of `bytes`: straight from memory where the bytes start on a word boundary
/// (every browser runs on little-endian hardware), through a view elsewhere.
function makeWordReader(bytes) {
  if (bytes.byteOffset % WORD_SIZE === 0) {
    const words = new Uint32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength >>> 2);
    return (offset) => words[offset >>> 2];
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return (offset) => view.getUint32(offset, true);
}

/// XXH32 with seed 0 of `bytes` (a Uint8Array), as an unsigned number: the check the deploy lists for every chunk
/// (fs::hash_bytes_check).
export function hashBytes(bytes) {
  const length = bytes.byteLength;
  const readWord = makeWordReader(bytes);
  let offset = 0;
  let hash = PRIME32_5;
  if (length >= STRIPE_SIZE) {
    let v1 = (PRIME32_1 + PRIME32_2) | 0;
    let v2 = PRIME32_2 | 0;
    let v3 = 0;
    let v4 = -PRIME32_1 | 0;
    for (const last = length - STRIPE_SIZE; offset <= last; offset += STRIPE_SIZE) {
      v1 = mixWord(v1, readWord(offset));
      v2 = mixWord(v2, readWord(offset + 4));
      v3 = mixWord(v3, readWord(offset + 8));
      v4 = mixWord(v4, readWord(offset + 12));
    }
    hash = rotateLeft(v1, 1) + rotateLeft(v2, 7) + rotateLeft(v3, 12) + rotateLeft(v4, 18);
  }
  hash = (hash + length) | 0;
  for (; offset + WORD_SIZE <= length; offset += WORD_SIZE) {
    hash = Math.imul(rotateLeft((hash + Math.imul(readWord(offset), PRIME32_3)) | 0, 17), PRIME32_4);
  }
  for (; offset < length; ++offset) {
    hash = Math.imul(rotateLeft((hash + Math.imul(bytes[offset], PRIME32_5)) | 0, 11), PRIME32_1);
  }
  hash ^= hash >>> 15;
  hash = Math.imul(hash, PRIME32_2);
  hash ^= hash >>> 13;
  hash = Math.imul(hash, PRIME32_3);
  hash ^= hash >>> 16;
  return hash >>> 0;
}

/// The manifest's chunks of a file ([name, size, check] each) as objects with the offset each starts at in the file.
export function readChunks(file) {
  let offset = 0;
  return (file.chunks ?? []).map(([name, size, check]) => {
    const chunk = { name, size, check, offset };
    offset += size;
    return chunk;
  });
}

/// Why the manifest's list of `file` cannot be loaded, as a sentence; empty when it can: its chunks add up to its size.
export function describeFileProblem(file) {
  const total = readChunks(file).reduce((sum, chunk) => sum + chunk.size, 0);
  if (!Number.isFinite(file.size) || total !== file.size) {
    return `The build lists ${file.path} with ${file.size} bytes and chunks of ${total} bytes.`;
  }
  return '';
}

/// Whether `bytes` are the chunk the manifest lists: its size and its check.
export function isChunk(bytes, chunk) {
  return bytes.byteLength === chunk.size && hashBytes(bytes) === chunk.check;
}

/// What a download of `chunk` that gave other bytes is refused with.
function describeForeignBytes(chunk, bytes) {
  return `${chunk.name} arrived with ${bytes.byteLength} bytes that are not the build's ${chunk.size}: the host sent `
    + 'another file, or the download was cut short.';
}

/**
 * The bytes of `chunk`: the cached ones when they are the chunk's, else downloaded, checked and then cached. A cached
 * copy that is not the chunk is dropped. A download that fails or gives other bytes is tried again after a wait, up to
 * CHUNK_ATTEMPTS times; the last failure is thrown.
 *
 * `io` does the work:
 *   readCache(name)            the cached bytes of a chunk, or null
 *   writeCache(name, bytes)    keeps a chunk; may fail silently
 *   dropCache(name)            forgets a cached chunk
 *   download(name, onBytes)    the chunk's bytes from the network, counted into onBytes as they arrive; throws when
 *                              the host does not answer with the file
 *   wait(ms)                   resolves after the wait
 *   onBytes(count)             moves the loading bar; a failed try takes its bytes back with a negative count
 */
export async function loadChunk(chunk, io) {
  const cached = await io.readCache(chunk.name);
  if (cached) {
    if (isChunk(cached, chunk)) {
      io.onBytes(chunk.size);
      return cached;
    }
    await io.dropCache(chunk.name);
  }
  let failure = null;
  for (let attempt = 0; attempt < CHUNK_ATTEMPTS; ++attempt) {
    if (attempt > 0) {
      await io.wait(RETRY_DELAYS_MS[Math.min(attempt, RETRY_DELAYS_MS.length) - 1]);
    }
    let counted = 0;
    const count = (bytes) => {
      counted += bytes;
      io.onBytes(bytes);
    };
    try {
      const bytes = await io.download(chunk.name, count);
      if (isChunk(bytes, chunk)) {
        await io.writeCache(chunk.name, bytes);
        io.onBytes(chunk.size - counted);
        return bytes;
      }
      failure = new Error(describeForeignBytes(chunk, bytes));
    } catch (e) {
      failure = e;
    }
    io.onBytes(-counted);
  }
  throw failure;
}

/// Runs `action` on every item, at most `limit` at a time; the first failure stops the others from starting and is
/// thrown once the running ones ended.
export async function forEachLimited(items, limit, action) {
  let next = 0;
  let failure = null;
  const work = async () => {
    while (next < items.length && !failure) {
      try {
        await action(items[next++]);
      } catch (e) {
        failure = failure ?? e;
      }
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, work));
  if (failure) {
    throw failure;
  }
}

/**
 * Loads every file of `files` chunk by chunk, at most `limit` chunks at a time, and hands each chunk's bytes to the
 * file's writer as it arrives, in any order: `openFile(file)` gives { write(offset, bytes), close() }. A file is
 * closed once its last chunk is written; a file without bytes at once. Files of the same content list the same
 * chunk: while one of them loads it, the others wait for those bytes, so it is downloaded once.
 */
export async function loadFiles(files, limit, io, openFile) {
  const loading = new Map();
  const loadOnce = async (chunk) => {
    const shared = loading.get(chunk.name);
    if (shared) {
      const bytes = await shared;
      io.onBytes(chunk.size);
      return bytes;
    }
    // Only while it loads: kept longer, every chunk's bytes would stay in memory beside the files they went into.
    const load = loadChunk(chunk, io).finally(() => loading.delete(chunk.name));
    loading.set(chunk.name, load);
    return load;
  };
  const work = [];
  for (const file of files) {
    const problem = describeFileProblem(file);
    if (problem) {
      throw new Error(problem);
    }
    const chunks = readChunks(file);
    const writer = openFile(file);
    const state = { writer, left: chunks.length };
    if (chunks.length === 0) {
      writer.close();
    }
    for (const chunk of chunks) {
      work.push({ chunk, state });
    }
  }
  await forEachLimited(work, limit, async ({ chunk, state }) => {
    state.writer.write(chunk.offset, await loadOnce(chunk));
    if (--state.left === 0) {
      state.writer.close();
    }
  });
}

/// Tries `request`, which gives a Response, again after a wait when the network fails or the host has a passing
/// problem (a 5xx answer, or 408 and 429); any other answer is the host's and is returned as it is.
export async function fetchWithRetry(request, wait) {
  const isPassing = (status) => status >= 500 || status === 408 || status === 429;
  let failure = null;
  for (let attempt = 0; attempt < CHUNK_ATTEMPTS; ++attempt) {
    if (attempt > 0) {
      await wait(RETRY_DELAYS_MS[Math.min(attempt, RETRY_DELAYS_MS.length) - 1]);
    }
    try {
      const response = await request();
      if (!isPassing(response.status) || attempt === CHUNK_ATTEMPTS - 1) {
        return response;
      }
    } catch (e) {
      failure = e;
    }
  }
  throw failure;
}

/// The engine options a page address may pass on to a game anyone can open: the ones that change what a run shows
/// or logs, and nothing that names a folder, a file or a debug view. Each with whether a value follows it.
const PUBLIC_ARGUMENTS = Object.freeze({
  '--gfx-stats': false,
  '--novsync': false,
  '--log-level': true,
  '--max-fps': true,
});

/// Whether a page at `hostname` is a developer's own: served from this computer.
export function isLocalHost(hostname) {
  return hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '[::1]' || hostname.endsWith('.localhost');
}

/// The option the page itself names the packs in the player's memory with (game.cpp): never taken from an address,
/// where it would name memory that holds no pack.
export const MEMORY_FILES_ARGUMENT = '--memory-files';

/**
 * The engine arguments of `given` (the page address's arg= values, in order) a page at `hostname` passes on: all of
 * them on this computer, where Deploy and Run sets the profile's run arguments; elsewhere only PUBLIC_ARGUMENTS, so
 * a link to a published game cannot point its files elsewhere or switch a debug view on. An option that takes a
 * value is followed by it, or carries it after '='.
 */
export function filterArguments(given, hostname) {
  const isLocal = isLocalHost(hostname);
  const kept = [];
  for (let i = 0; i < given.length; ++i) {
    const [name] = given[i].split('=', 1);
    const hasOwnValue = given[i].includes('=');
    if (name === MEMORY_FILES_ARGUMENT) {
      i += hasOwnValue ? 0 : 1;
      continue;
    }
    if (isLocal) {
      kept.push(given[i]);
      continue;
    }
    if (!(name in PUBLIC_ARGUMENTS)) {
      continue;
    }
    kept.push(given[i]);
    if (PUBLIC_ARGUMENTS[name] && !hasOwnValue && i + 1 < given.length) {
      kept.push(given[++i]);
    }
  }
  return kept;
}

/**
 * Takes the libraries a trimmed build left out off the runtime's list of what to download. `config` is the runtime's
 * own, as it hands it over once loaded; `trimmed` the manifest's list of the files' names, missing for a build that
 * ships the whole framework. Returns how many were taken off.
 */
export function dropTrimmedAssemblies(config, trimmed) {
  if (!trimmed || trimmed.length === 0) {
    return 0;
  }
  const listed = config?.resources?.assembly;
  if (!Array.isArray(listed)) {
    throw new Error('The runtime lists its libraries in a form this page does not know. Deploy the build again with '
      + 'its dependencies.');
  }
  const leftOut = new Set(trimmed);
  config.resources.assembly = listed.filter((assembly) => !leftOut.has(assembly.name));
  return listed.length - config.resources.assembly.length;
}
