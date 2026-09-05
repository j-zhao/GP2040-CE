import { useEffect, useState, useRef } from 'react';
import {
	Alert,
	Button,
	Modal,
	Row,
	Col,
	ProgressBar,
	Form,
	Spinner,
	Table,
} from 'react-bootstrap';
import { useTranslation } from 'react-i18next';

import FormControl from '../Components/FormControl';
import FormCheck from '../Components/FormCheck';
import FormSelect from '../Components/FormSelect';
import WebApi from '../Services/WebApi';
import useHETriggerStore, {
	Trigger,
	TRAVEL_MAX,
	formatTravel,
} from '../Store/useHETriggerStore';
import {
	tenthsToWholePercent,
	wholePercentToTenths,
} from '../Services/Utilities';

import './HECalibration.scss';

import { BUTTON_ACTIONS } from '../Data/Pins';
import invert from 'lodash/invert';

const ADC_MAX = 4096;

// Smallest idle-to-pressed span worth scaling, matching HETRIGGER_MIN_SPAN in
// headers/addons/he_trigger.h
const MIN_SPAN = 16;

// A span this close to MIN_SPAN still counts as "moved", but is too small to
// trust for calibration, so the sweep flags it rather than silently saving it.
const SUSPICIOUS_SPAN = MIN_SPAN * 4;

// Firmware sweep polling interval. Fast enough to catch a quick tap without
// hammering the device, since the firmware samples several times per request.
const SWEEP_POLL_MS = 250;

type SweepChannel = {
	raw: number;
	baseline: number;
	min: number;
	max: number;
	moved: boolean;
};

const RT_OFF = 0;
const RT_NORMAL = 1;
const RT_CONTINUOUS = 2;

type HECalibrationProps = {
	calibrateAllLoop: boolean;
	calibrationTarget: number;
	muxChannels: number;
	setShowModal: (show: boolean) => void;
	showModal: boolean;
	triggers: Trigger[];
	values: any;
};

const getOption = (e, actionId) => {
	return {
		label: invert(BUTTON_ACTIONS)[actionId],
		value: actionId,
	};
};

// Raw ADC counts to tenths of a percent of calibrated travel. The span carries
// its own direction, so a sensor that reads high at rest needs no special case.
const travelFromRaw = (raw: number, idle: number, pressed: number) => {
	const span = pressed - idle;
	if (Math.abs(span) < MIN_SPAN) return 0;
	const travel = Math.round(((raw - idle) * TRAVEL_MAX) / span);
	return Math.min(TRAVEL_MAX, Math.max(0, travel));
};

const HECalibration = ({
	calibrationTarget,
	calibrateAllLoop,
	muxChannels,
	setShowModal,
	showModal,
	triggers,
	values,
}: HECalibrationProps) => {
	const { t } = useTranslation('');
	const setHETrigger = useHETriggerStore((state) => state.setHETrigger);
	const setAllHETriggers = useHETriggerStore((state) => state.setAllHETriggers);
	const fetchHETriggers = useHETriggerStore((state) => state.fetchHETriggers);
	const timerId = useRef<number>();
	const [title, setTitle] = useState('');
	const target = useRef(-1);
	const previousStep = useRef(0);
	const [calibrationStep, setCalibrationStep] = useState(0);
	const [voltage, setVoltage] = useState(0);
	const [activationState, setActivationState] = useState(false);
	const [voltageIdle, setVoltageIdle] = useState(150);
	const [voltagePressed, setVoltagePressed] = useState(3500);
	const [actuationPoint, setActuationPoint] = useState(450);
	const [deactuationPoint, setDeactuationPoint] = useState(0);
	const [rtMode, setRtMode] = useState(RT_OFF);
	const [pressSensitivity, setPressSensitivity] = useState(30);
	const [releaseSensitivity, setReleaseSensitivity] = useState(0);
	const [socdPartner, setSocdPartner] = useState(0);

	// Calibrate-all sweep session state. sweepChannels holds one entry per
	// channel, in channel order, as last reported by the firmware.
	const sweepTimerId = useRef<number>();
	const sweepSessionActive = useRef(false);
	const sweepSessionId = useRef<number>();
	const sweepStartId = useRef(0);
	const sweepPollPending = useRef(false);
	const [sweepChannels, setSweepChannels] = useState<SweepChannel[]>([]);

	// Thresholds applied to every channel on commit, in whole percent. Seeded
	// from the first assigned channel so the sweep starts from what is already
	// on the device rather than an arbitrary default.
	const [sweepActuationPoint, setSweepActuationPoint] = useState(0);
	const [sweepDeactuationPoint, setSweepDeactuationPoint] = useState(0);

	// Only the channels within the active mux layout correspond to real
	// hardware, mirroring the count TriggerActionsForm renders.
	const sweepChannelCount =
		Math.min(4, Math.floor(32 / muxChannels)) * muxChannels;
	const sweepAssigned = Array.from(
		{ length: sweepChannelCount },
		(_, i) => i,
	).filter((i) => triggers[i].action !== -10);
	const sweepUnseen = sweepAssigned.filter((i) => !sweepChannels[i]?.moved);
	const sweepSuspicious = sweepAssigned.filter((i) => {
		const channel = sweepChannels[i];
		if (!channel || !channel.moved) return false;
		return channel.max - channel.min < SUSPICIOUS_SPAN;
	});
	const sweepSeenCount = sweepAssigned.length - sweepUnseen.length;

	// Peak and valley of the live preview, mirroring the firmware state machine
	const peak = useRef(0);
	const valley = useRef(0);

	const travel = travelFromRaw(voltage, voltageIdle, voltagePressed);

	// Resolved exactly the way HETriggerAddon::setup does, so the preview cannot
	// promise behaviour the firmware will not reproduce. A zero value means
	// "mirror the other one"; the noise floor is the lower bound on both
	// sensitivities; a deactuation point deeper than actuation would never
	// release, so it is capped.
	const noiseFloor = values['heTriggerNoiseFloor'] || 0;
	const effectiveActuation = actuationPoint || 1;
	const effectiveDeactuation = Math.min(
		deactuationPoint || effectiveActuation,
		effectiveActuation,
	);
	const effectivePressSensitivity = Math.max(pressSensitivity, noiseFloor);
	const effectiveReleaseSensitivity = Math.max(
		releaseSensitivity || pressSensitivity,
		noiseFloor,
	);
	// Normal mode holds the deactuation point as its release floor. Continuous
	// mode drops that gate, so its floor is "the key is all the way up", which
	// keeps a large release sensitivity from latching the key down.
	const effectiveReleaseFloor =
		rtMode === RT_CONTINUOUS ? noiseFloor || 1 : effectiveDeactuation;

	const RT_MODE_SELECT = {
		[RT_OFF]: t('HETrigger:rapid-trigger-off'),
		[RT_NORMAL]: t('HETrigger:rapid-trigger-normal'),
		[RT_CONTINUOUS]: t('HETrigger:rapid-trigger-continuous'),
	};

	// Preview of what the firmware will do, kept deliberately in step with
	// HETriggerAddon::updateChannel.
	useEffect(() => {
		if (rtMode === RT_OFF) {
			setActivationState(
				activationState
					? travel >= effectiveDeactuation
					: travel >= effectiveActuation,
			);
			return;
		}

		if (activationState) {
			if (travel > peak.current) peak.current = travel;

			if (travel + effectiveReleaseSensitivity <= peak.current) {
				setActivationState(false);
				valley.current = travel;
			} else if (travel < effectiveReleaseFloor) {
				setActivationState(false);
				valley.current = travel;
			}
		} else {
			if (travel < valley.current) valley.current = travel;

			const armed = rtMode === RT_CONTINUOUS || travel >= effectiveActuation;
			if (armed && travel >= valley.current + effectivePressSensitivity) {
				setActivationState(true);
				peak.current = travel;
			}
		}
		// Re-evaluated on every reading and on every threshold edit, the way the
		// firmware re-evaluates every frame.
	}, [
		voltage,
		travel,
		rtMode,
		effectiveActuation,
		effectiveDeactuation,
		effectivePressSensitivity,
		effectiveReleaseSensitivity,
		effectiveReleaseFloor,
		activationState,
	]);

	const loadTarget = () => {
		const trigger = triggers[target.current];
		setVoltageIdle(trigger.idle);
		setVoltagePressed(trigger.pressed);
		setActuationPoint(trigger.actuationPoint);
		setDeactuationPoint(trigger.deactuationPoint);
		setRtMode(trigger.rtMode);
		setPressSensitivity(trigger.rtPressSensitivity);
		setReleaseSensitivity(trigger.rtReleaseSensitivity);
		setSocdPartner(trigger.socdPartner);
		peak.current = 0;
		valley.current = TRAVEL_MAX;
		setActivationState(false);
	};

	const currentSettings = () => ({
		idle: voltageIdle,
		pressed: voltagePressed,
		actuationPoint,
		deactuationPoint,
		rtMode,
		rtPressSensitivity: pressSensitivity,
		rtReleaseSensitivity: releaseSensitivity,
		socdPartner,
	});

	const saveCalibration = () => {
		setHETrigger({
			id: target.current,
			action: triggers[target.current].action,
			...currentSettings(),
		});
		stopCalibration();
		setShowModal(false);
	};

	const updateTitle = () => {
		if (target.current !== -1) {
			// set title
			const option = getOption(
				triggers[target.current],
				triggers[target.current].action,
			);
			const actionTitle = t(`PinMapping:actions.${option.label}`);
			if (muxChannels > 1) {
				const muxNum = Math.floor(target.current / muxChannels);
				const channelNum = target.current % muxChannels;
				setTitle(`${actionTitle} - Mux ${muxNum} - Channel ${channelNum}`);
			} else {
				setTitle(
					`${actionTitle} - Direct - ADC ${values[`muxADCPin${target.current}` as keyof typeof values]}`,
				);
			}
		}
	};

	const overwriteAllCalibration = () => {
		setAllHETriggers(currentSettings());
		closeModal();
	};

	const stopCalibration = async () => {
		setCalibrationStep(0);
		target.current = -1;
		if (timerId) clearInterval(timerId.current);
	};

	// Stops the poll but leaves the firmware session alone; callers decide
	// whether to commit or cancel it.
	const stopSweepPolling = () => {
		if (sweepTimerId.current) clearInterval(sweepTimerId.current);
		sweepTimerId.current = undefined;
	};

	const pollSweep = async () => {
		// Pausing while the tab is hidden avoids piling up requests the user
		// cannot see the results of anyway.
		if (document.hidden) return;
		if (sweepPollPending.current) return;
		if (sweepSessionId.current === undefined) return;
		const sessionId = sweepSessionId.current;
		sweepPollPending.current = true;
		const result = await WebApi.getHETriggerSweep();
		sweepPollPending.current = false;
		if (sweepSessionId.current !== sessionId) return;
		if (!result || !result.active || result.sessionId !== sessionId) {
			sweepSessionActive.current = false;
			sweepSessionId.current = undefined;
			stopSweepPolling();
			return;
		}
		setSweepChannels(result.channels || []);
	};

	const startSweep = async () => {
		const startId = ++sweepStartId.current;
		setSweepChannels([]);
		// Seed the apply-to-all inputs from the first assigned channel, so they
		// reflect what is already on the device rather than appearing empty.
		const seedIndex = sweepAssigned[0];
		if (seedIndex !== undefined) {
			setSweepActuationPoint(
				tenthsToWholePercent(triggers[seedIndex].actuationPoint),
			);
			setSweepDeactuationPoint(
				tenthsToWholePercent(triggers[seedIndex].deactuationPoint),
			);
		}
		await WebApi.setHETriggerOptions({
			muxChannels: values['muxChannels'],
			muxSelectPin0: values['muxSelectPin0'],
			muxSelectPin1: values['muxSelectPin1'],
			muxSelectPin2: values['muxSelectPin2'],
			muxSelectPin3: values['muxSelectPin3'],
			muxADCPin0: values['muxADCPin0'],
			muxADCPin1: values['muxADCPin1'],
			muxADCPin2: values['muxADCPin2'],
			muxADCPin3: values['muxADCPin3'],
			heTriggerMuxSettleMicros: values['heTriggerMuxSettleMicros'],
		});
		if (startId !== sweepStartId.current) return;
		const result = await WebApi.startHETriggerSweep();
		if (startId !== sweepStartId.current) {
			if (result?.active)
				await WebApi.cancelHETriggerSweep(result.sessionId);
			return;
		}
		sweepSessionActive.current = Boolean(result?.active);
		sweepSessionId.current = result?.sessionId;
		if (!sweepSessionActive.current || sweepSessionId.current === undefined)
			return;
		pollSweep();
		stopSweepPolling();
		sweepTimerId.current = setInterval(pollSweep, SWEEP_POLL_MS);
	};

	// Discards the running sweep, if any, without saving. Safe to call more
	// than once or when no sweep was started.
	const cancelSweepSession = async () => {
		sweepStartId.current++;
		stopSweepPolling();
		if (sweepSessionActive.current) {
			const sessionId = sweepSessionId.current;
			sweepSessionActive.current = false;
			await WebApi.cancelHETriggerSweep(sessionId);
			if (sweepSessionId.current === sessionId)
				sweepSessionId.current = undefined;
		}
	};

	const commitSweep = async () => {
		if (
			(sweepUnseen.length > 0 || sweepSuspicious.length > 0) &&
			!window.confirm(t('HETrigger:sweep-save-confirm'))
		) {
			return;
		}
		sweepStartId.current++;
		stopSweepPolling();
		const sessionId = sweepSessionId.current;
		const result = await WebApi.commitHETriggerSweep(sessionId, {
			actuationPoint: wholePercentToTenths(sweepActuationPoint),
			deactuationPoint: wholePercentToTenths(sweepDeactuationPoint),
		});
		if (!result?.saved) return;
		sweepSessionActive.current = false;
		sweepSessionId.current = undefined;
		await fetchHETriggers();
		setShowModal(false);
	};

	const closeModal = async () => {
		if (calibrateAllLoop) {
			await cancelSweepSession();
		} else {
			stopCalibration();
		}
		setShowModal(false);
	};

	const startReadingCalibrationLoop = async () => {
		if (showModal) {
			// Set the Hall Effect configuration pins
			await WebApi.setHETriggerOptions({
				muxChannels: values['muxChannels'],
				muxSelectPin0: values['muxSelectPin0'],
				muxSelectPin1: values['muxSelectPin1'],
				muxSelectPin2: values['muxSelectPin2'],
				muxSelectPin3: values['muxSelectPin3'],
				muxADCPin0: values['muxADCPin0'],
				muxADCPin1: values['muxADCPin1'],
				muxADCPin2: values['muxADCPin2'],
				muxADCPin3: values['muxADCPin3'],
				heTriggerSmoothing: values['heTriggerSmoothing'],
				heTriggerSmoothingFactor: values['heTriggerSmoothingFactor'],
			});
			updateCalibrationRead(0);
		}
	};

	const updateCalibrationRead = (step: number) => {
		setCalibrationStep(step);
		// Begin reading
		if (timerId.current) clearInterval(timerId.current);
		const intervalId = setInterval(() => {
			readHallEffect(step);
		}, 50);
		timerId.current = intervalId;
	};

	// Start Capturing on Modal Show
	const startCalibration = async () => {
		target.current = calibrationTarget;
		loadTarget();
	};

	const restartCalibration = () => {
		previousStep.current = 0;
		updateCalibrationRead(0);
		loadTarget();
	};

	const calculateVoltagePercentage = () => {
		return voltage / (ADC_MAX / 100.0);
	};

	const travelPercentage = () => travel / (TRAVEL_MAX / 100.0);

	const readHallEffect = async (calibrationStep: number) => {
		const result = await WebApi.getHETriggerVoltage({
			targetId: target.current,
		});

		if (!result || !result.data) {
			console.error('Could not get hall-effect trigger calibration!');
			return;
		}

		const data = result.data;

		// For Web-Testing Debug Only
		if (data.debug && data.debug === true) {
			if (calibrationStep === 0) {
				setVoltage(150); // min we'll set to 20
			} else if (calibrationStep === 1) {
				setVoltage(3500); // max we'll set to 3500
			} else if (calibrationStep === 2 || calibrationStep === 3) {
				let time = new Date().getTime();
				const V =
					150 +
					Math.floor(
						(Math.cos((((time / 10) % 365) * Math.PI) / 180) + 1.0) * 1500,
					);
				setVoltage(V);
			}
		} else {
			// set the read voltage from real HE
			setVoltage(data.voltage);
		}
	};

	// Shared live readout: a travel bar with the actuation point marked.
	const travelReadout = () => (
		<>
			<Col xs={12} className="mb-3">
				{t(`HETrigger:activation-reading-text`)}
			</Col>
			<Col xs={12} className="mb-3 text-center">
				<ProgressBar>
					<ProgressBar
						variant={activationState ? 'success' : 'warning'}
						now={travelPercentage()}
						key={1}
					/>
				</ProgressBar>
			</Col>
			<Col xs={12} className="mb-3">
				{formatTravel(travel)} ({voltage}){' '}
				{activationState ? t('HETrigger:pressed-text') : ''}
			</Col>
		</>
	);

	const actuationControls = () => (
		<>
			<Col xs={6} className="mb-3">
				<FormControl
					type="number"
					label={t(`HETrigger:actuation-input-text`)}
					name="actuationPoint"
					className="form-select-sm"
					value={tenthsToWholePercent(actuationPoint)}
					onChange={(e) => {
						setActuationPoint(
							wholePercentToTenths(
								parseInt((e.target as HTMLInputElement).value),
							),
						);
					}}
					min={1}
					max={100}
				/>
			</Col>
			<Col xs={6} className="mb-3">
				<FormCheck
					label={t('HETrigger:separate-deactuation-label')}
					type="switch"
					name="separateDeactuation"
					id="HETriggerSeparateDeactuation"
					isInvalid={false}
					checked={deactuationPoint !== 0}
					onChange={(e) => {
						// Clearing the toggle stores 0, which is how both the
						// firmware and the config say "mirror the actuation point".
						setDeactuationPoint(e.target.checked ? actuationPoint : 0);
					}}
				/>
			</Col>
			{deactuationPoint !== 0 && (
				<Col xs={6} className="mb-3">
					<FormControl
						type="number"
						label={t(`HETrigger:deactuation-input-text`)}
						name="deactuationPoint"
						className="form-select-sm"
						value={tenthsToWholePercent(deactuationPoint)}
						onChange={(e) => {
							setDeactuationPoint(
								wholePercentToTenths(
									parseInt((e.target as HTMLInputElement).value),
								),
							);
						}}
						min={1}
						max={tenthsToWholePercent(actuationPoint)}
					/>
				</Col>
			)}
			<Col xs={12} className="mb-3">
				<Form.Range
					min={1}
					max={TRAVEL_MAX}
					step={1}
					value={actuationPoint}
					onChange={(e) => {
						setActuationPoint(parseInt(e.target.value));
					}}
				></Form.Range>
			</Col>
		</>
	);

	const rapidTriggerControls = () => (
		<>
			<Col xs={6} className="mb-3">
				<FormSelect
					label={t('HETrigger:rapid-trigger-mode-label')}
					name="rtMode"
					className="form-select-sm"
					value={rtMode}
					onChange={(e) => {
						setRtMode(parseInt((e.target as HTMLSelectElement).value));
					}}
				>
					{Object.entries(RT_MODE_SELECT).map(([value, label], i) => (
						<option key={`rt-mode-option-${i}`} value={value}>
							{label}
						</option>
					))}
				</FormSelect>
			</Col>
			{rtMode !== RT_OFF && (
				<>
					<Col xs={6} className="mb-3">
						<FormControl
							type="number"
							label={t(`HETrigger:press-sensitivity-input-text`)}
							name="rtPressSensitivity"
							className="form-select-sm"
							value={tenthsToWholePercent(pressSensitivity)}
							onChange={(e) => {
								setPressSensitivity(
									wholePercentToTenths(
										parseInt((e.target as HTMLInputElement).value),
									),
								);
							}}
							min={1}
							max={100}
						/>
					</Col>
					<Col xs={6} className="mb-3">
						<FormCheck
							label={t('HETrigger:separate-sensitivity-label')}
							type="switch"
							name="separateSensitivity"
							id="HETriggerSeparateSensitivity"
							isInvalid={false}
							checked={releaseSensitivity !== 0}
							onChange={(e) => {
								setReleaseSensitivity(e.target.checked ? pressSensitivity : 0);
							}}
						/>
					</Col>
					{releaseSensitivity !== 0 && (
						<Col xs={6} className="mb-3">
							<FormControl
								type="number"
								label={t(`HETrigger:release-sensitivity-input-text`)}
								name="rtReleaseSensitivity"
								className="form-select-sm"
								value={tenthsToWholePercent(releaseSensitivity)}
								onChange={(e) => {
									setReleaseSensitivity(
										wholePercentToTenths(
											parseInt((e.target as HTMLInputElement).value),
										),
									);
								}}
								min={1}
								max={100}
							/>
						</Col>
					)}
				</>
			)}
		</>
	);

	const firstStep = () => {
		return (
			<Row className="mb-3" hidden={calibrationStep !== 0}>
				<Col xs={12} className="mb-3">
					{t(`HETrigger:calibration-first-step`)}
				</Col>
				<Col xs={12} className="mb-3"></Col>
				<Col xs={12} className="mb-3">
					{t(`HETrigger:calibration-idle-text`)}
				</Col>
				<Col xs={12} className="mb-3 text-center">
					<ProgressBar>
						<ProgressBar
							variant="info"
							now={calculateVoltagePercentage()}
							key={1}
						/>
					</ProgressBar>
				</Col>
				<Col xs={12} className="mb-3">
					<h3>{voltage}</h3>
				</Col>
			</Row>
		);
	};

	const secondStep = () => {
		return (
			<Row className="mb-3" hidden={calibrationStep !== 1}>
				<span className="col-sm-12">
					{t(`HETrigger:calibration-second-step`)}
				</span>
				<Col xs={12} className="mb-3"></Col>
				<Col xs={12} className="mb-3">
					{t(`HETrigger:calibration-pressed-text`)}
				</Col>
				<Col xs={12} className="mb-3 text-center">
					<ProgressBar>
						<ProgressBar
							variant="info"
							now={calculateVoltagePercentage()}
							key={1}
						/>
					</ProgressBar>
				</Col>
				<Col xs={12} className="mb-3">
					<h3>{voltage}</h3>
				</Col>
				<Col xs={3} className="mb-3">
					<Button
						onClick={() => {
							restartCalibration();
						}}
						variant="danger"
					>
						{t(`HETrigger:restart-text`)}
					</Button>
				</Col>
			</Row>
		);
	};

	const thirdStep = () => {
		return (
			<Row className="mb-3" hidden={calibrationStep !== 2}>
				<span className="col-sm-12">
					{t(`HETrigger:calibration-third-step`)}
				</span>
				<Col xs={12} className="mb-3"></Col>
				{actuationControls()}
				{travelReadout()}
				<Col xs={3} className="mb-3">
					<Button onClick={() => restartCalibration()} variant="danger">
						{t(`HETrigger:restart-text`)}
					</Button>
				</Col>
			</Row>
		);
	};

	const manualAdjustments = () => {
		return (
			<Row className="mb-3" hidden={calibrationStep !== 3}>
				<Col xs={12} className="mb-3">
					{t(`HETrigger:calibration-manual-step`)}
				</Col>
				<Col xs={6} className="mb-3">
					<FormControl
						type="number"
						label={t(`HETrigger:idle-input-text`)}
						name="voltageIdle"
						className="form-select-sm"
						value={voltageIdle}
						onChange={(e) => {
							setVoltageIdle(parseInt((e.target as HTMLInputElement).value));
						}}
						min={0}
						max={ADC_MAX}
					/>
				</Col>
				<Col xs={6} className="mb-3">
					<FormControl
						type="number"
						label={t(`HETrigger:pressed-input-text`)}
						name="voltagePressed"
						className="form-select-sm"
						value={voltagePressed}
						onChange={(e) => {
							setVoltagePressed(parseInt((e.target as HTMLInputElement).value));
						}}
						min={0}
						max={ADC_MAX}
					/>
				</Col>
				{actuationControls()}
				{rapidTriggerControls()}
				<Col xs={6} className="mb-3">
					<FormControl
						type="number"
						label={t(`HETrigger:socd-partner-input-text`)}
						name="socdPartner"
						className="form-select-sm"
						// stored as the partner index plus one, so that 0 means none, but
						// shown as the channel number the trigger table uses
						value={socdPartner === 0 ? '' : socdPartner - 1}
						onChange={(e) => {
							const channel = parseInt((e.target as HTMLInputElement).value);
							setSocdPartner(isNaN(channel) ? 0 : channel + 1);
						}}
						min={0}
						max={sweepChannelCount - 1}
					/>
				</Col>
				{travelReadout()}
				<Col xs={12} className="mb-3" />
				<Col xs={12} className="mb-3 text-center">
					<Button
						variant="danger"
						onClick={() => {
							if (window.confirm(t(`HETrigger:overwrite-confirm`))) {
								overwriteAllCalibration();
							}
						}}
						className="col-sm-4"
					>
						{t(`HETrigger:overwrite-all-warning`)}
					</Button>
				</Col>
			</Row>
		);
	};

	// Compact per-channel row for the calibrate-all sweep: channel number,
	// assigned action if any, a live travel bar, the captured span, and a
	// seen / not-seen indicator driven by `moved`.
	const sweepRow = (i: number) => {
		const channel = sweepChannels[i];
		const assigned = triggers[i].action !== -10;
		const option = getOption(triggers[i], triggers[i].action);
		const actionLabel = assigned
			? t(`PinMapping:actions.${option.label}`)
			: t('HETrigger:sweep-unassigned-label');
		const span = channel ? channel.max - channel.min : 0;
		const fromMin = channel ? channel.baseline - channel.min : 0;
		const fromMax = channel ? channel.max - channel.baseline : 0;
		const pressSpan = Math.max(fromMin, fromMax);
		const pressTravel = channel
			? fromMax >= fromMin
				? channel.raw - channel.baseline
				: channel.baseline - channel.raw
			: 0;
		const rowTravel =
			pressSpan > 0
				? Math.max(0, Math.min(100, Math.round((pressTravel / pressSpan) * 100)))
				: 0;
		const moved = Boolean(channel?.moved);
		const suspicious = assigned && moved && span < SUSPICIOUS_SPAN;

		return (
			<tr
				key={`sweep-row-${i}`}
				className={!assigned ? 'text-muted' : undefined}
			>
				<td>{t('HETrigger:sweep-channel-label', { channel: i })}</td>
				<td>{actionLabel}</td>
				<td style={{ minWidth: '10rem' }}>
					<ProgressBar
						now={rowTravel}
						variant={moved ? 'success' : 'warning'}
					/>
				</td>
				<td>{t('HETrigger:sweep-span-label', { span })}</td>
				<td>
					{moved
						? t('HETrigger:sweep-seen-label')
						: t('HETrigger:sweep-not-seen-label')}
					{suspicious && ' ⚠️'}
				</td>
			</tr>
		);
	};

	const sweepView = () => (
		<Row className="mb-3">
			<Col xs={12} className="mb-3">
				{t('HETrigger:sweep-instructions-text')}
			</Col>
			<Col xs={12} className="mb-3">
				<strong>
					{t('HETrigger:sweep-progress-text', {
						seen: sweepSeenCount,
						total: sweepAssigned.length,
					})}
				</strong>
			</Col>
			<Col xs={6} className="mb-3">
				<FormControl
					type="number"
					label={t('HETrigger:sweep-actuation-input-text')}
					name="sweepActuationPoint"
					className="form-select-sm"
					value={sweepActuationPoint}
					onChange={(e) => {
						setSweepActuationPoint(
							parseInt((e.target as HTMLInputElement).value),
						);
					}}
					min={0}
					max={100}
				/>
			</Col>
			<Col xs={6} className="mb-3">
				<FormControl
					type="number"
					label={t('HETrigger:sweep-deactuation-input-text')}
					name="sweepDeactuationPoint"
					className="form-select-sm"
					value={sweepDeactuationPoint}
					onChange={(e) => {
						setSweepDeactuationPoint(
							parseInt((e.target as HTMLInputElement).value),
						);
					}}
					min={0}
					max={sweepActuationPoint}
				/>
			</Col>
			{sweepUnseen.length > 0 && (
				<Col xs={12} className="mb-3">
					<Alert variant="warning" className="mb-0">
						{t('HETrigger:sweep-inert-warning')}
					</Alert>
				</Col>
			)}
			{sweepSuspicious.length > 0 && (
				<Col xs={12} className="mb-3">
					<Alert variant="warning" className="mb-0">
						{t('HETrigger:sweep-suspicious-warning', {
							channels: sweepSuspicious.join(', '),
						})}
					</Alert>
				</Col>
			)}
			<Col xs={12}>
				<Table bordered size="sm">
					<thead>
						<tr>
							<th>{t('HETrigger:channel-label')}</th>
							<th>{t('HETrigger:sweep-action-label')}</th>
							<th>{t('HETrigger:activation-reading-text')}</th>
							<th></th>
							<th></th>
						</tr>
					</thead>
					<tbody>
						{Array.from({ length: sweepChannelCount }, (_, i) => sweepRow(i))}
					</tbody>
				</Table>
			</Col>
		</Row>
	);

	useEffect(() => {
		if (showModal === true) {
			if (calibrateAllLoop) {
				startSweep();
			} else {
				startCalibration();
				startReadingCalibrationLoop();
				updateTitle();
			}
		}
	}, [showModal]);

	// Safety net: if the modal component itself unmounts (e.g. navigating
	// away) while a sweep is running, do not leave the session active on
	// the device.
	useEffect(() => {
		return () => {
			sweepStartId.current++;
			stopSweepPolling();
			if (sweepSessionActive.current) {
				const sessionId = sweepSessionId.current;
				WebApi.cancelHETriggerSweep(sessionId);
			}
		};
	}, []);

	return (
		<>
			<Modal
				className="modal-lg"
				contentClassName="he-modal"
				centered
				show={showModal}
				onClose={() => closeModal()}
				onHide={() => closeModal()}
			>
				<Modal.Header closeButton>
					<Modal.Title className="me-auto">
						{calibrateAllLoop
							? t('HETrigger:sweep-header-text')
							: `${t('HETrigger:calibration-header-text')} - ${title}`}
					</Modal.Title>
				</Modal.Header>
				<Modal.Body>
					{calibrateAllLoop ? (
						sweepView()
					) : (
						<>
							{firstStep()}
							{secondStep()}
							{thirdStep()}
							{manualAdjustments()}
						</>
					)}
				</Modal.Body>
				<Modal.Footer>
					{calibrateAllLoop ? (
						<>
							<Button variant="secondary" onClick={() => closeModal()}>
								{t('HETrigger:sweep-cancel-button')}
							</Button>
							<Button variant="success" onClick={() => commitSweep()}>
								{t('HETrigger:sweep-save-button')}
							</Button>
						</>
					) : (
						<>
							<Button
								onClick={() => {
									setVoltageIdle(voltage);
									updateCalibrationRead(1);
								}}
								hidden={calibrationStep !== 0}
							>
								<Spinner
									as="span"
									animation="grow"
									size="sm"
									role="status"
									aria-hidden="true"
								/>{' '}
								{t(`HETrigger:calibrate-idle-button`)}
							</Button>
							<Button
								onClick={() => {
									// Recording the pressed reading as measured is what
									// encodes direction, so a sensor that reads high at rest
									// needs no polarity flag.
									setVoltagePressed(voltage);
									setActuationPoint(450);
									setDeactuationPoint(0);
									setPressSensitivity(30);
									setReleaseSensitivity(0);
									setRtMode(RT_OFF);
									updateCalibrationRead(2);
								}}
								hidden={calibrationStep !== 1}
							>
								<Spinner
									as="span"
									animation="grow"
									size="sm"
									role="status"
									aria-hidden="true"
									variant="success"
								/>{' '}
								{t(`HETrigger:calibrate-pressed-button`)}
							</Button>
							<Button
								variant="success"
								onClick={() => saveCalibration()}
								hidden={calibrationStep < 2}
							>
								{t(`HETrigger:finish-calibration-text`)}
							</Button>
							<Button
								onClick={() => {
									updateCalibrationRead(previousStep.current);
								}}
								hidden={calibrationStep !== 3}
							>
								{t(`HETrigger:calibration-back-button`)}
							</Button>
							<Button
								onClick={() => {
									previousStep.current = calibrationStep;
									updateCalibrationRead(3);
								}}
								hidden={calibrationStep === 3}
							>
								{t(`HETrigger:manual-text`)}
							</Button>
						</>
					)}
				</Modal.Footer>
			</Modal>
		</>
	);
};

export default HECalibration;
