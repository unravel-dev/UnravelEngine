// Checks how the page loads the game's data (game/web/wwwroot/data.js): the check of a chunk's bytes, a chunk from
// the cache or the network with its retries, files assembled from their chunks, the arguments an address may pass
// on, and the libraries a trimmed build takes off the runtime's list. Run with: node --test game/web/tests/data.test.mjs
import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  CHUNK_ATTEMPTS,
  MEMORY_FILES_ARGUMENT,
  RETRY_DELAYS_MS,
  describeFileProblem,
  dropTrimmedAssemblies,
  fetchWithRetry,
  filterArguments,
  forEachLimited,
  hashBytes,
  isChunk,
  loadChunk,
  loadFiles,
  readChunks,
} from '../wwwroot/data.js';

const encoder = new TextEncoder();

function bytesOf(text) {
  return encoder.encode(text);
}

/// A chunk as the manifest lists it, of `bytes`.
function chunkOf(name, bytes, offset = 0) {
  return { name, size: bytes.byteLength, check: hashBytes(bytes), offset };
}

/// What loadChunk works through, recording what it did: `cached` and `hosted` map chunk names to bytes; `hosted`
/// values may also be an array of answers given in turn, each bytes or an Error to throw.
function makeIo({ cached = {}, hosted = {} } = {}) {
  const io = {
    cached: { ...cached },
    downloads: [],
    waits: [],
    dropped: [],
    progress: 0,
    readCache: async (name) => io.cached[name] ?? null,
    writeCache: async (name, bytes) => { io.cached[name] = bytes; },
    dropCache: async (name) => { io.dropped.push(name); delete io.cached[name]; },
    download: async (name, onBytes) => {
      io.downloads.push(name);
      const answers = hosted[name];
      const answer = Array.isArray(answers) ? answers.shift() : answers;
      if (answer === undefined) {
        throw new Error(`${name} could not be downloaded (404).`);
      }
      if (answer instanceof Error) {
        throw answer;
      }
      onBytes(answer.byteLength);
      return answer;
    },
    wait: async (ms) => { io.waits.push(ms); },
    onBytes: (count) => { io.progress += count; },
  };
  return io;
}

test('the check of a chunk is XXH32 with seed 0, as the deploy computes it', () => {
  // The same texts and values as the deploy library's test of fs::hash_bytes_check.
  assert.equal(hashBytes(bytesOf('')), 0x02CC5D05);
  assert.equal(hashBytes(bytesOf('a')), 0x550D7456);
  assert.equal(hashBytes(bytesOf('abc')), 0x32D153FF);
  assert.equal(hashBytes(bytesOf('Nobody inspects the spammish repetition')), 0xE2293B2F);
});

test('the check does not depend on where the bytes lie in their buffer', () => {
  const text = bytesOf('Nobody inspects the spammish repetition');
  for (const shift of [1, 2, 3, 4]) {
    const shifted = new Uint8Array(text.byteLength + shift);
    shifted.set(text, shift);
    assert.equal(hashBytes(shifted.subarray(shift)), 0xE2293B2F, `shifted by ${shift}`);
  }
});

test('a chunk is its size and its check', () => {
  const bytes = bytesOf('chunk of a pack');
  const chunk = chunkOf('a.bin', bytes);
  assert.ok(isChunk(bytes, chunk));
  assert.ok(!isChunk(bytes.subarray(1), chunk), 'a shorter download');
  assert.ok(!isChunk(bytesOf('chunk of a PACK'), chunk), 'other bytes of the same size');
});

test('a file\'s chunks start where the ones before them end', () => {
  const file = { path: 'data/app/content.pak', size: 12, chunks: [['a.bin', 5, 1], ['b.bin', 4, 2], ['c.bin', 3, 3]] };
  assert.deepEqual(readChunks(file).map((chunk) => [chunk.name, chunk.offset, chunk.size, chunk.check]),
    [['a.bin', 0, 5, 1], ['b.bin', 5, 4, 2], ['c.bin', 9, 3, 3]]);
  assert.equal(describeFileProblem(file), '');
  assert.notEqual(describeFileProblem({ ...file, size: 13 }), '', 'chunks that do not add up to the size');
  assert.equal(describeFileProblem({ path: 'data/app/empty', size: 0, chunks: [] }), '');
});

test('a cached chunk is used without a download', async () => {
  const bytes = bytesOf('kept from an earlier visit');
  const io = makeIo({ cached: { 'a.bin': bytes } });
  assert.equal(await loadChunk(chunkOf('a.bin', bytes), io), bytes);
  assert.deepEqual(io.downloads, []);
  assert.equal(io.progress, bytes.byteLength);
});

test('a chunk the cache does not hold is downloaded and kept', async () => {
  const bytes = bytesOf('fresh from the host');
  const io = makeIo({ hosted: { 'a.bin': bytes } });
  assert.equal(await loadChunk(chunkOf('a.bin', bytes), io), bytes);
  assert.equal(io.cached['a.bin'], bytes);
  assert.equal(io.progress, bytes.byteLength);
  assert.deepEqual(io.waits, []);
});

test('a cached copy that is not the chunk is dropped and downloaded again', async () => {
  const bytes = bytesOf('the build\'s bytes');
  const io = makeIo({ cached: { 'a.bin': bytesOf('a host\'s error page') }, hosted: { 'a.bin': bytes } });
  assert.equal(await loadChunk(chunkOf('a.bin', bytes), io), bytes);
  assert.deepEqual(io.dropped, ['a.bin']);
  assert.equal(io.cached['a.bin'], bytes);
});

test('a download of other bytes or a failed one is tried again, and never cached', async () => {
  const bytes = bytesOf('the build\'s bytes');
  const answers = [bytesOf('<!DOCTYPE html>'), new Error('The network dropped.'), bytes];
  const io = makeIo({ hosted: { 'a.bin': answers } });
  assert.equal(await loadChunk(chunkOf('a.bin', bytes), io), bytes);
  assert.equal(io.downloads.length, 3);
  assert.deepEqual(io.waits, [RETRY_DELAYS_MS[0], RETRY_DELAYS_MS[1]]);
  assert.equal(io.progress, bytes.byteLength, 'the failed tries took their bytes back');
  assert.equal(io.cached['a.bin'], bytes);
});

test('a chunk that never arrives fails the start after the last try', async () => {
  const bytes = bytesOf('the build\'s bytes');
  const io = makeIo({ hosted: { 'a.bin': Array.from({ length: CHUNK_ATTEMPTS }, () => bytesOf('not it')) } });
  await assert.rejects(loadChunk(chunkOf('a.bin', bytes), io), /not the build's/);
  assert.equal(io.downloads.length, CHUNK_ATTEMPTS);
  assert.equal(io.waits.length, CHUNK_ATTEMPTS - 1);
  assert.equal(io.progress, 0);
  assert.equal(io.cached['a.bin'], undefined);
});

test('files are written chunk by chunk at their offsets and closed once', async () => {
  const first = bytesOf('first ');
  const second = bytesOf('second');
  const only = bytesOf('one chunk');
  const files = [
    { path: 'data/app/content.pak', size: 12, chunks: [['1.bin', 6, hashBytes(first)], ['2.bin', 6, hashBytes(second)]] },
    { path: 'data/app/settings.cfg', size: 9, chunks: [['3.bin', 9, hashBytes(only)]] },
    { path: 'data/app/empty', size: 0, chunks: [] },
  ];
  const io = makeIo({ hosted: { '1.bin': first, '2.bin': second, '3.bin': only } });
  const written = {};
  const closed = [];
  await loadFiles(files, 2, io, (file) => {
    const content = new Uint8Array(file.size);
    return {
      write: (offset, bytes) => content.set(bytes, offset),
      close: () => { closed.push(file.path); written[file.path] = new TextDecoder().decode(content); },
    };
  });
  assert.deepEqual(written, { 'data/app/content.pak': 'first second', 'data/app/settings.cfg': 'one chunk', 'data/app/empty': '' });
  assert.deepEqual([...closed].sort(), ['data/app/content.pak', 'data/app/empty', 'data/app/settings.cfg']);
  assert.equal(io.progress, 21);
});

test('a chunk that files share is downloaded once and counted for each of them', async () => {
  const same = bytesOf('the same bytes');
  const chunk = ['same.bin', same.byteLength, hashBytes(same)];
  const files = ['a.pak', 'b.pak', 'c.pak'].map((path) => ({ path, size: same.byteLength, chunks: [chunk] }));
  const io = makeIo({ hosted: { 'same.bin': same } });
  const written = [];
  await loadFiles(files, 3, io, (file) => ({
    write: (offset, bytes) => written.push(`${file.path}:${new TextDecoder().decode(bytes)}`),
    close: () => {},
  }));
  assert.deepEqual(io.downloads, ['same.bin']);
  assert.deepEqual(written.sort(), ['a.pak:the same bytes', 'b.pak:the same bytes', 'c.pak:the same bytes']);
  assert.equal(io.progress, 3 * same.byteLength, 'the loading bar gets the bytes of every file');
  const failing = makeIo();
  await assert.rejects(loadFiles(files, 3, failing, () => ({ write: () => {}, close: () => {} })), /404/);
});

test('a file whose chunks do not add up to its size is refused before any download', async () => {
  const io = makeIo();
  const files = [{ path: 'data/app/content.pak', size: 10, chunks: [['1.bin', 6, 0]] }];
  await assert.rejects(loadFiles(files, 2, io, () => assert.fail('no file is opened')), /content\.pak/);
  assert.deepEqual(io.downloads, []);
});

test('work runs a few items at a time, and a failure starts no more', async () => {
  let running = 0;
  let most = 0;
  const done = [];
  await forEachLimited([1, 2, 3, 4, 5, 6], 2, async (item) => {
    most = Math.max(most, ++running);
    await new Promise((resolve) => setTimeout(resolve, 1));
    --running;
    done.push(item);
  });
  assert.equal(most, 2);
  assert.equal(done.length, 6);
  const started = [];
  await assert.rejects(forEachLimited([1, 2, 3, 4, 5, 6], 2, async (item) => {
    started.push(item);
    if (item === 2) {
      throw new Error('the second failed');
    }
    await new Promise((resolve) => setTimeout(resolve, 1));
  }), /the second failed/);
  assert.ok(started.length < 6, 'items after the failure were not started');
});

test('a request is tried again while the network or the host has a passing problem', async () => {
  const waits = [];
  const wait = async (ms) => { waits.push(ms); };
  const answers = [new Error('offline'), { status: 503 }, { status: 200 }];
  const response = await fetchWithRetry(async () => {
    const answer = answers.shift();
    if (answer instanceof Error) {
      throw answer;
    }
    return answer;
  }, wait);
  assert.equal(response.status, 200);
  assert.equal(waits.length, 2);
  let requests = 0;
  const missing = await fetchWithRetry(async () => { ++requests; return { status: 404 }; }, wait);
  assert.equal(missing.status, 404);
  assert.equal(requests, 1, 'the host\'s own answer is not asked for again');
  await assert.rejects(fetchWithRetry(async () => { throw new Error('offline'); }, wait), /offline/);
  let busy = 0;
  const still = await fetchWithRetry(async () => { ++busy; return { status: 503 }; }, wait);
  assert.equal(still.status, 503, 'the last answer is returned as it is');
  assert.equal(busy, CHUNK_ATTEMPTS);
});

test('a page on this computer passes every argument on, a published one only the harmless ones', () => {
  const given = ['--gfx-stats', '--appdata', '/elsewhere', '--log-level', 'trace', '--debug-pass=3', '--max-fps=30'];
  assert.deepEqual(filterArguments(given, '127.0.0.1'), given);
  assert.deepEqual(filterArguments(given, 'localhost'), given);
  assert.deepEqual(filterArguments(given, 'game.example.com'), ['--gfx-stats', '--log-level', 'trace', '--max-fps=30']);
  assert.deepEqual(filterArguments(['--log-level'], 'game.example.com'), ['--log-level']);
});

test('the option that names memory is never taken from an address', () => {
  const given = [MEMORY_FILES_ARGUMENT, 'data/app/content.pak:8:8', '--novsync', `${MEMORY_FILES_ARGUMENT}=a:1:1`];
  assert.deepEqual(filterArguments(given, '127.0.0.1'), ['--novsync']);
  assert.deepEqual(filterArguments(given, 'game.example.com'), ['--novsync']);
});

test('a trimmed build takes its left-out libraries off the list the runtime downloads', () => {
  const makeConfig = () => ({
    resources: {
      coreAssembly: [{ name: 'System.Private.CoreLib.aaaa.wasm' }],
      assembly: [{ name: 'System.Linq.bbbb.wasm' }, { name: 'System.Data.Common.cccc.wasm' }, { name: 'System.Xml.dddd.wasm' }],
    },
  });
  const config = makeConfig();
  assert.equal(dropTrimmedAssemblies(config, ['System.Data.Common.cccc.wasm', 'System.Xml.dddd.wasm', 'Gone.wasm']), 2);
  assert.deepEqual(config.resources.assembly, [{ name: 'System.Linq.bbbb.wasm' }]);
  assert.equal(config.resources.coreAssembly.length, 1, 'the libraries the runtime starts with are not on that list');
  const whole = makeConfig();
  assert.equal(dropTrimmedAssemblies(whole, undefined), 0);
  assert.equal(dropTrimmedAssemblies(whole, []), 0);
  assert.equal(whole.resources.assembly.length, 3, 'a build that ships the whole framework changes nothing');
  assert.throws(() => dropTrimmedAssemblies({ resources: { assembly: { 'System.Linq.wasm': 'hash' } } }, ['System.Linq.wasm']),
    /a form this page does not know/);
});
