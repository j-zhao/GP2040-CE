import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import WebApi from '../Services/WebApi';
import { GPElement, GPShape_Type } from '@proto/enums';

// Element parameters, as written by the firmware into the JSON layout
// payload (see config.pb GPElement / GPShape_Type).
interface GPElementParameters {
	x1?: number;
	y1?: number;
	x2?: number;
	y2?: number;
	stroke?: number;
	fill?: number;
	shape?: number;
	angleStart?: number;
	angleEnd?: number;
	closed?: number;
}

interface GPElementData {
	elementType?: number;
	parameters?: GPElementParameters;
}

// The endpoint keys elements by stringified index, not an array.
type GPLayout = Record<string, GPElementData>;

interface DisplayLayouts {
	buttonLayout?: GPLayout;
	buttonLayoutRight?: GPLayout;
}

interface ButtonLayoutPreviewProps {
	values: {
		buttonLayout?: number | string;
		buttonLayoutRight?: number | string;
		buttonLayoutOrientation?: number | string;
		buttonLayoutCustomOptions?: unknown;
		invertDisplay?: number | string;
		inputHistoryEnabled?: number | string;
	};
}

// Nominal OLED resolution the layout geometry is expressed in.
const SCREEN_WIDTH = 128;
const SCREEN_HEIGHT = 64;

// The canvas is rendered at 3x the nominal resolution for crisper pixels.
const PREVIEW_SCALE = 3;

// Hide the preview only after this many consecutive fetch failures, so a
// single dropped request doesn't hide it. A later successful fetch brings it
// back, because a busy device is not the same as a device without the endpoint.
const MAX_CONSECUTIVE_FAILURES = 3;

interface Viewport {
	scaleX: number;
	scaleY: number;
	offsetX: number;
	offsetY: number;
}

// Mirrors the viewport scale and offset math in GPButton::draw()
// (src/display/ui/elements/GPButton.cpp). The viewport is the full screen
// normally, and top=8/bottom=56 when input history is enabled
// (src/display/ui/screens/ButtonLayoutScreen.cpp:26).
const getViewport = (inputHistoryEnabled: boolean): Viewport => {
	const left = 0;
	const right = SCREEN_WIDTH;
	const top = inputHistoryEnabled ? 8 : 0;
	const bottom = inputHistoryEnabled ? 56 : SCREEN_HEIGHT;

	let scaleX = (right - left) / SCREEN_WIDTH;
	let scaleY = (bottom - top) / SCREEN_HEIGHT;

	// Firmware makes the scales proportionate if either is 0 or 1 (GPButton.cpp:14-19).
	if (scaleX > 0 && (scaleY === 0 || scaleY === 1)) scaleY = scaleX;
	else if ((scaleX === 0 || scaleX === 1) && scaleY > 0) scaleX = scaleY;

	const offsetX = Math.max(0, (right - left - SCREEN_WIDTH * scaleX) / 2);
	const offsetY =
		top + Math.max(0, (bottom - top - SCREEN_HEIGHT * scaleY) / 2);

	return { scaleX, scaleY, offsetX, offsetY };
};

const vx = (vp: Viewport, x: number) => x * vp.scaleX + vp.offsetX;
const vy = (vp: Viewport, y: number) => y * vp.scaleY + vp.offsetY;

// Shared rotation boilerplate for shapes rotated about their own centre
// (drawRectangle()/drawPill()'s rotationAngle is in degrees, tiny_ssd1306.cpp).
// `path` builds the shape in centre-relative coordinates.
const drawRotated = (
	ctx: CanvasRenderingContext2D,
	p: GPElementParameters,
	x1: number,
	y1: number,
	x2: number,
	y2: number,
	path: (w: number, h: number) => void,
) => {
	const cx = (x1 + x2) / 2;
	const cy = (y1 + y2) / 2;
	const angle = ((p.angleStart ?? 0) * Math.PI) / 180;
	ctx.save();
	ctx.translate(cx, cy);
	ctx.rotate(angle);
	ctx.beginPath();
	path(x2 - x1, y2 - y1);
	if (p.fill) ctx.fill();
	if (p.stroke) ctx.stroke();
	ctx.restore();
};

const drawShape = (
	ctx: CanvasRenderingContext2D,
	p: GPElementParameters,
	vp: Viewport,
) => {
	const x1 = vx(vp, p.x1 ?? 0);
	const y1 = vy(vp, p.y1 ?? 0);
	const x2 = vx(vp, p.x2 ?? 0);
	const y2 = vy(vp, p.y2 ?? 0);
	// Radii scale by scaleX, mirroring GPButton's radius scaling.
	const radius = (p.x2 ?? 0) * vp.scaleX;

	ctx.beginPath();

	switch (p.shape) {
		case GPShape_Type.GP_SHAPE_ELLIPSE:
			ctx.arc(x1, y1, radius, 0, 2 * Math.PI);
			break;
		case GPShape_Type.GP_SHAPE_SQUARE:
			// drawRectangle()'s rotationAngle is in degrees (tiny_ssd1306.cpp:310).
			drawRotated(ctx, p, x1, y1, x2, y2, (w, h) => {
				ctx.rect(-w / 2, -h / 2, w, h);
			});
			return;
		case GPShape_Type.GP_SHAPE_LINE:
			ctx.moveTo(x1, y1);
			ctx.lineTo(x2, y2);
			break;
		case GPShape_Type.GP_SHAPE_POLYGON: {
			const sides = p.y2 ?? 0;
			for (let i = 0; i < sides; i++) {
				// angleStart is used as-is, in radians, matching
				// drawPolygon() (tiny_ssd1306.cpp:369).
				const angle = i * ((2 * Math.PI) / sides) + (p.angleStart ?? 0);
				const px = x1 + radius * Math.cos(angle);
				const py = y1 + radius * Math.sin(angle);
				if (i === 0) ctx.moveTo(px, py);
				else ctx.lineTo(px, py);
			}
			ctx.closePath();
			break;
		}
		case GPShape_Type.GP_SHAPE_ARC: {
			// angleStart/angleEnd are in degrees, matching drawArc() (tiny_ssd1306.cpp:218-219).
			const startRad = ((p.angleStart ?? 0) * Math.PI) / 180;
			const endRad = ((p.angleEnd ?? 0) * Math.PI) / 180;

			if (p.fill) {
				// A filled arc fans lines from the center across the whole
				// sweep, i.e. a pie, regardless of `closed`
				// (tiny_ssd1306.cpp:241-254).
				ctx.beginPath();
				ctx.moveTo(x1, y1);
				ctx.arc(x1, y1, radius, startRad, endRad);
				ctx.closePath();
				ctx.fill();
			}

			ctx.beginPath();
			ctx.arc(x1, y1, radius, startRad, endRad);
			if (p.closed) {
				ctx.lineTo(x1, y1);
				ctx.closePath();
			}
			if (p.stroke) ctx.stroke();
			return;
		}
		case GPShape_Type.GP_SHAPE_PILL:
			// drawPill()'s rotationAngle is in degrees (tiny_ssd1306.cpp:438).
			drawRotated(ctx, p, x1, y1, x2, y2, (w, h) => {
				ctx.roundRect(-w / 2, -h / 2, w, h, Math.min(w, h) / 2);
			});
			return;
		default:
			return;
	}

	if (p.fill) ctx.fill();
	if (p.stroke) ctx.stroke();
};

const drawLever = (
	ctx: CanvasRenderingContext2D,
	p: GPElementParameters,
	vp: Viewport,
) => {
	const cx = vx(vp, p.x1 ?? 0);
	const cy = vy(vp, p.y1 ?? 0);
	const radius = (p.x2 ?? 0) * vp.scaleX;

	// The outer circle is always unfilled, regardless of `fill` (GPLever.cpp:102).
	ctx.beginPath();
	ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
	if (p.stroke) ctx.stroke();

	// Neutral knob, filled at 0.75x the outer radius (GPLever.cpp:41).
	ctx.beginPath();
	ctx.arc(cx, cy, radius * 0.75, 0, 2 * Math.PI);
	ctx.fill();
};

const drawLayout = (
	ctx: CanvasRenderingContext2D,
	layout: GPLayout | undefined,
	vp: Viewport,
) => {
	if (!layout) return;

	Object.values(layout).forEach((element) => {
		if (!element?.parameters) return;

		const p = element.parameters;

		switch (element.elementType) {
			case GPElement.GP_ELEMENT_BTN_BUTTON:
			case GPElement.GP_ELEMENT_DIR_BUTTON:
			case GPElement.GP_ELEMENT_PIN_BUTTON:
				// A button is filled only while it is pressed, so the layout's
				// own fill is ignored and an idle button is an outline
				// (GPButton.cpp:134).
				drawShape(ctx, { ...p, fill: 0 }, vp);
				break;
			case GPElement.GP_ELEMENT_SHAPE:
				drawShape(ctx, p, vp);
				break;
			case GPElement.GP_ELEMENT_LEVER:
				drawLever(ctx, p, vp);
				break;
			default:
				break;
		}
	});
};

const ButtonLayoutPreview = ({ values }: ButtonLayoutPreviewProps) => {
	const { t } = useTranslation('');
	const [layouts, setLayouts] = useState<DisplayLayouts | null>(null);
	const [hidden, setHidden] = useState(false);
	const failureCountRef = useRef(0);
	const seqRef = useRef(0);
	const canvasRef = useRef<HTMLCanvasElement>(null);

	// invertDisplay/inputHistoryEnabled only affect client-side drawing, not
	// the geometry returned by the endpoint, so they are omitted here and
	// only refetch for values that change the device-side layout.
	useEffect(() => {
		const timeout = setTimeout(async () => {
			const seq = ++seqRef.current;

			const data = await WebApi.getButtonLayouts();

			if (seq !== seqRef.current) return;

			if (!data?.displayLayouts) {
				failureCountRef.current += 1;
				if (failureCountRef.current >= MAX_CONSECUTIVE_FAILURES) {
					setHidden(true);
					setLayouts(null);
				}
				return;
			}

			failureCountRef.current = 0;
			setHidden(false);
			setLayouts(data.displayLayouts);
		}, 250);

		return () => clearTimeout(timeout);
	}, [
		values.buttonLayout,
		values.buttonLayoutRight,
		values.buttonLayoutOrientation,
		JSON.stringify(values.buttonLayoutCustomOptions),
	]);

	useEffect(() => {
		if (!layouts || !canvasRef.current) return;

		const ctx = canvasRef.current.getContext('2d');
		if (!ctx) return;

		ctx.setTransform(PREVIEW_SCALE, 0, 0, PREVIEW_SCALE, 0, 0);
		ctx.imageSmoothingEnabled = false;
		ctx.lineWidth = 1;

		const fg = Number(values.invertDisplay) ? '#000000' : '#ffffff';
		const bg = Number(values.invertDisplay) ? '#ffffff' : '#000000';

		ctx.fillStyle = bg;
		ctx.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

		ctx.fillStyle = fg;
		ctx.strokeStyle = fg;

		const vp = getViewport(Boolean(Number(values.inputHistoryEnabled)));

		// flipDisplay is a hardware segment-remap applied by the display
		// driver, not a layout coordinate transform, so it is intentionally
		// not represented here.
		drawLayout(ctx, layouts.buttonLayout, vp);
		drawLayout(ctx, layouts.buttonLayoutRight, vp);
	}, [layouts, values.invertDisplay, values.inputHistoryEnabled]);

	if (hidden || !layouts) return null;

	return (
		<div>
			<h1>{t('DisplayConfig:section.preview-header')}</h1>
			<canvas
				ref={canvasRef}
				width={SCREEN_WIDTH * PREVIEW_SCALE}
				height={SCREEN_HEIGHT * PREVIEW_SCALE}
				style={{ background: 'black' }}
			/>
		</div>
	);
};

export default ButtonLayoutPreview;
