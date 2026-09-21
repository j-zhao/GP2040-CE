import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import WebApi from '../Services/WebApi';

import { pollDisplayFrames, type DisplayFrame } from '../Services/pollDisplayFrames';

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

const decodeDisplayFrame = (bytes: string, width: number, height: number, invert: boolean) => {
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
	return new ImageData(rgba, width, height);
};

const DisplayPreview = () => {
	const { t } = useTranslation('');
	const canvasRef = useRef<HTMLCanvasElement>(null);
	const [frame, setFrame] = useState<DisplayFrame | null>(null);

	useEffect(() => pollDisplayFrames(WebApi.getDisplayFrame, setFrame), []);

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

		const invert = frame.invert;

		ctx.putImageData(decodeDisplayFrame(bytes, width, height, invert), 0, 0);
	}, [frame]);

	// Stay out of the page until a frame arrives. The device reports zero
	// dimensions when it has no display to mirror.
	if (!frame || !frame.width) return null;

	const isButtonLayout = frame.isButtonLayout;
	const width = frame.width;
	const height = frame.height || DEFAULT_HEIGHT;

	return (
		<div>
			<h1>{t('DisplayConfig:section.display-preview-header')}</h1>
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
					// The panel applies the flip after drawing.
					transform: FLIP_TRANSFORMS[frame.flip],
				}}
			/>
		</div>
	);
};

export default DisplayPreview;
