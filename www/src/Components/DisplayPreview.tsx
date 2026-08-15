import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import WebApi from '../Services/WebApi';

interface DisplayFrame {
	width: number;
	height: number;
	isButtonLayout: boolean;
	frame: string;
}

interface DisplayPreviewValues {
	invertDisplay?: number | boolean;
	flipDisplay?: number | boolean;
}

interface DisplayPreviewProps {
	values: DisplayPreviewValues;
}

const POLL_INTERVAL_MS = 1000;
const MAX_CONSECUTIVE_FAILURES = 3;
const DEFAULT_WIDTH = 128;
const DEFAULT_HEIGHT = 64;
const PREVIEW_SCALE = 3;

// flipDisplay drives the panel's segment remap and COM scan direction, so each
// mode mirrors a different axis. See the command list in tiny_ssd1306.cpp.
const FLIP_TRANSFORMS: Record<number, string> = {
	1: 'scaleY(-1)',
	2: 'scaleX(-1)',
	3: 'scale(-1, -1)',
};

const DisplayPreview = ({ values }: DisplayPreviewProps) => {
	const { t } = useTranslation('');
	const canvasRef = useRef<HTMLCanvasElement>(null);
	const failureCountRef = useRef(0);
	const [frame, setFrame] = useState<DisplayFrame | null>(null);
	const [hidden, setHidden] = useState(false);

	// Poll the device for its actual framebuffer. The firmware is the only
	// renderer; this component just mirrors whatever it already drew.
	useEffect(() => {
		let cancelled = false;
		let intervalId: ReturnType<typeof setInterval> | undefined = undefined;

		const fetchFrame = async () => {
			if (document.hidden) return;

			const data = await WebApi.getDisplayFrame();

			if (cancelled) return;

			if (!data) {
				failureCountRef.current += 1;
				// Firmware without this endpoint never answers, so stop asking.
				if (failureCountRef.current >= MAX_CONSECUTIVE_FAILURES) {
					clearInterval(intervalId);
					setHidden(true);
				}
				return;
			}

			failureCountRef.current = 0;
			setHidden(false);
			setFrame(data);
		};

		fetchFrame();
		intervalId = setInterval(fetchFrame, POLL_INTERVAL_MS);

		return () => {
			cancelled = true;
			clearInterval(intervalId);
		};
	}, []);

	useEffect(() => {
		if (!frame || !frame.frame || !canvasRef.current) return;

		const width = frame.width || DEFAULT_WIDTH;
		const height = frame.height || DEFAULT_HEIGHT;
		const ctx = canvasRef.current.getContext('2d');

		if (!ctx) return;

		const bytes = window.atob(frame.frame);

		// The device packs one bit per pixel. Ignore a short frame rather than
		// drawing the missing pixels as blank.
		if (bytes.length < Math.ceil((width * height) / 8)) return;

		// invertDisplay swaps which pixel state is "on"; the firmware applies
		// it after drawing, so we swap the two colors here rather than the bits.
		// Formik keeps the select value as a string, so coerce before testing it.
		const invert = Number(values.invertDisplay) === 1;
		const onColor = invert ? 0 : 255;
		const offColor = invert ? 255 : 0;
		const rgba = new Uint8ClampedArray(width * height * 4);

		for (let y = 0; y < height; y++) {
			for (let x = 0; x < width; x++) {
				const byteIndex = (y * width + x) >> 3;
				const bit = 0x80 >> x % 8;
				const isOn = (bytes.charCodeAt(byteIndex) & bit) !== 0;
				const shade = isOn ? onColor : offColor;
				const offset = (y * width + x) * 4;
				rgba[offset] = shade;
				rgba[offset + 1] = shade;
				rgba[offset + 2] = shade;
				rgba[offset + 3] = 255;
			}
		}

		ctx.putImageData(new ImageData(rgba, width, height), 0, 0);
	}, [frame, values.invertDisplay]);

	// Stay out of the page until a frame arrives. The device reports zero
	// dimensions when it has no display to mirror.
	if (hidden || !frame || !frame.width) return null;

	const isButtonLayout = frame.isButtonLayout;
	const width = frame.width;
	const height = frame.height || DEFAULT_HEIGHT;

	return (
		<div>
			<h1>{t('DisplayConfig:section.preview-header')}</h1>
			{!isButtonLayout && <p>{t('DisplayConfig:section.preview-hint')}</p>}
			<canvas
				ref={canvasRef}
				width={width}
				height={height}
				style={{
					display: isButtonLayout ? 'block' : 'none',
					background: 'black',
					width: width * PREVIEW_SCALE,
					height: height * PREVIEW_SCALE,
					imageRendering: 'pixelated',
					// The firmware applies the flip in the panel hardware, after
					// drawing, so it is mirrored here in CSS instead of in the
					// pixel unpacking above.
					transform: FLIP_TRANSFORMS[Number(values.flipDisplay)],
				}}
			/>
		</div>
	);
};

export default DisplayPreview;
