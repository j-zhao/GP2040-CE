import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import ts from 'typescript';

async function loadTypeScript(path) {
	const source = await readFile(new URL(path, import.meta.url), 'utf8');
	const { outputText } = ts.transpileModule(source, {
		compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
	});
	return import(`data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`);
}

const { pollDisplayFrames } = await loadTypeScript('../src/Services/pollDisplayFrames.ts');
const { default: http } = await loadTypeScript('../src/Services/Http.ts');
const frame = {
	width: 128, height: 64, isButtonLayout: true, flip: 2, invert: true,
	frame: Buffer.alloc(1024, 0xa5).toString('base64'),
};

class Visibility extends EventTarget {
	hidden = false;
	set(hidden) {
		this.hidden = hidden;
		this.dispatchEvent(new Event('visibilitychange'));
	}
}

async function flush() {
	for (let i = 0; i < 5; i++) await Promise.resolve();
}

function setup(t, { autoAbort = true, hidden = false } = {}) {
	t.mock.timers.enable({ apis: ['setTimeout'] });
	const visibility = new Visibility();
	visibility.hidden = hidden;
	const calls = [];
	const frames = [];
	const stop = pollDisplayFrames((signal) => new Promise((resolve, reject) => {
		calls.push({ signal, resolve, reject });
		if (autoAbort) signal.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
	}), (value) => frames.push(value), visibility);
	t.after(stop);
	return { calls, frames, visibility, stop };
}

test('waits for completion before scheduling another request', async (t) => {
	const { calls, frames } = setup(t);
	t.mock.timers.tick(4000);
	assert.equal(calls.length, 1);
	calls[0].resolve(frame);
	await flush();
	assert.deepEqual(frames, [frame]);
	t.mock.timers.tick(999);
	assert.equal(calls.length, 1);
	t.mock.timers.tick(1);
	assert.equal(calls.length, 2);
});

test('cleanup aborts the request and suppresses a late result', async (t) => {
	const { calls, frames, visibility, stop } = setup(t, { autoAbort: false });
	stop();
	assert.equal(calls[0].signal.aborted, true);
	calls[0].resolve(frame);
	await flush();
	visibility.set(true);
	visibility.set(false);
	t.mock.timers.tick(60000);
	assert.deepEqual(frames, []);
	assert.equal(calls.length, 1);
});

test('times out and retries without disabling the preview', async (t) => {
	const { calls, frames } = setup(t);
	t.mock.timers.tick(5000);
	assert.equal(calls[0].signal.aborted, true);
	await flush();
	t.mock.timers.tick(1000);
	assert.equal(calls.length, 2);
	calls[1].resolve(frame);
	await flush();
	assert.deepEqual(frames, [frame]);
});

test('caps retry delays and resets them after recovery', async (t) => {
	const { calls, frames } = setup(t);
	for (const delay of [1000, 2000, 4000, 8000, 15000, 15000]) {
		const count = calls.length;
		calls.at(-1).reject(new Error('Disconnected'));
		await flush();
		t.mock.timers.tick(delay - 1);
		assert.equal(calls.length, count);
		t.mock.timers.tick(1);
		assert.equal(calls.length, count + 1);
	}
	calls.at(-1).resolve(frame);
	await flush();
	t.mock.timers.tick(1000);
	const count = calls.length;
	calls.at(-1).reject(new Error('Disconnected'));
	await flush();
	t.mock.timers.tick(1000);
	assert.equal(calls.length, count + 1);
	assert.deepEqual(frames, [frame]);
});

test('pauses hidden tabs and resumes after cancellation settles', async (t) => {
	const { calls, frames, visibility } = setup(t, { autoAbort: false });
	visibility.set(true);
	assert.equal(calls[0].signal.aborted, true);
	visibility.set(false);
	t.mock.timers.tick(0);
	assert.equal(calls.length, 1);
	calls[0].resolve(frame);
	await flush();
	t.mock.timers.tick(0);
	assert.equal(calls.length, 2);
	assert.deepEqual(frames, []);
});

test('does not request a frame until an initially hidden tab becomes visible', (t) => {
	const { calls, visibility } = setup(t, { hidden: true });
	t.mock.timers.tick(60000);
	assert.equal(calls.length, 0);
	visibility.set(false);
	t.mock.timers.tick(0);
	assert.equal(calls.length, 1);
});

test('stops unsupported endpoints but retries server failures', async (t) => {
	const { calls, frames, visibility } = setup(t);
	calls[0].reject(Object.assign(new Error('Unavailable'), { status: 503 }));
	await flush();
	t.mock.timers.tick(1000);
	assert.equal(calls.length, 2);
	calls[1].reject(Object.assign(new Error('Missing'), { status: 404 }));
	await flush();
	visibility.set(true);
	visibility.set(false);
	t.mock.timers.tick(60000);
	assert.equal(calls.length, 2);
	assert.deepEqual(frames, [null]);
});

test('rejects malformed frames and preserves the last valid frame', async (t) => {
	const { calls, frames } = setup(t);
	calls[0].resolve(frame);
	await flush();
	for (const invalid of [
		{ ...frame, width: 256 }, { ...frame, frame: '!' },
		{ ...frame, frame: '' }, { ...frame, flip: 4 },
		{ ...frame, invert: 1 }, { ...frame, isButtonLayout: 0 },
	]) {
		t.mock.timers.tick(15000);
		calls.at(-1).resolve(invalid);
		await flush();
		assert.deepEqual(frames, [frame]);
	}
});

test('accepts unavailable displays and both supported sizes', async (t) => {
	const { calls, frames } = setup(t);
	const empty = { ...frame, width: 0, height: 0, frame: '' };
	calls[0].resolve(empty);
	await flush();
	t.mock.timers.tick(1000);
	const small = { ...frame, height: 32, frame: Buffer.alloc(512).toString('base64') };
	calls[1].resolve(small);
	await flush();
	assert.deepEqual(frames, [empty, small]);
});

test('checks HTTP status before JSON parsing only when requested', async (t) => {
	const controller = new AbortController();
	let parsed = 0;
	t.mock.method(globalThis, 'fetch', async (_url, options) => {
		assert.equal(options.signal, controller.signal);
		return { ok: false, status: 501, json: async () => { parsed++; return frame; } };
	});
	await assert.rejects(http.get('/frame', { signal: controller.signal, requireSuccess: true }), { status: 501 });
	assert.equal(parsed, 0);
	assert.deepEqual(await http.get('/frame', { signal: controller.signal }), { data: frame });
	assert.equal(parsed, 1);
});
