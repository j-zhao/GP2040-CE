export interface DisplayFrame {
	width: number;
	height: number;
	isButtonLayout: boolean;
	flip: number;
	invert: boolean;
	frame: string;
}

function validateFrame(data: DisplayFrame): DisplayFrame {
	if (!data || typeof data.frame !== 'string') throw new Error('Invalid display frame');
	const empty = data.width === 0 && data.height === 0;
	const supported = data.width === 128 && [32, 64].includes(data.height);
	if (!empty && !supported) throw new Error('Invalid display dimensions');
	validateMetadata(data);
	if (atob(data.frame).length !== (data.width * data.height) / 8)
		throw new Error('Invalid display frame length');
	return data;
}

function validateMetadata(data: DisplayFrame) {
	if (!Number.isInteger(data.flip) || data.flip < 0 || data.flip > 3)
		throw new Error('Invalid display orientation');
	if (typeof data.invert !== 'boolean' || typeof data.isButtonLayout !== 'boolean')
		throw new Error('Invalid display metadata');
}

export function pollDisplayFrames(
	request: (signal: AbortSignal) => Promise<DisplayFrame>,
	receive: (frame: DisplayFrame | null) => void,
	visibility: Pick<Document, 'hidden' | 'addEventListener' | 'removeEventListener'> = document,
) {
	let stopped = false;
	let active: AbortController | null = null;
	let timer: ReturnType<typeof setTimeout> | undefined;
	let retryDelay = 1000;

	function schedule(delay: number) {
		clearTimeout(timer);
		if (!stopped && !visibility.hidden) timer = setTimeout(run, delay);
	}

	function stop() {
		stopped = true;
		clearTimeout(timer);
		active?.abort();
		visibility.removeEventListener('visibilitychange', onVisibility);
	}

	function onVisibility() {
		clearTimeout(timer);
		if (visibility.hidden) active?.abort();
		else schedule(0);
	}

	function failed(error: unknown) {
		if (stopped) return 0;
		const status = (error as { status?: number } | null)?.status;
		if (status === 404 || status === 501) {
			stop();
			receive(null);
		}
		const delay = retryDelay;
		retryDelay = Math.min(retryDelay * 2, 15000);
		return delay;
	}

	function accept(data: DisplayFrame, signal: AbortSignal) {
		if (stopped || signal.aborted) return 0;
		receive(validateFrame(data));
		retryDelay = 1000;
		return 1000;
	}

	async function run() {
		if (stopped || visibility.hidden || active) return;
		const controller = new AbortController();
		active = controller;
		let timedOut = false;
		let delay = 0;
		const timeout = setTimeout(() => {
			timedOut = true;
			controller.abort();
		}, 5000);
		try {
			delay = accept(await request(controller.signal), controller.signal);
		} catch (error) {
			if (!controller.signal.aborted || timedOut) delay = failed(error);
		} finally {
			clearTimeout(timeout);
			active = null;
			schedule(delay);
		}
	}

	visibility.addEventListener('visibilitychange', onVisibility);
	run();
	return stop;
}
