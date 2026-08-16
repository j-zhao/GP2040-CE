import { create } from 'zustand';

import WebApi from '../Services/WebApi';
import { PinActionValues } from '../Data/Pins';

// Travel values are tenths of a percent of the calibrated idle-to-pressed
// span, 0-1000, matching the firmware. idle and pressed stay in raw ADC counts
// because they are what defines that span.
export type Trigger = {
	action: PinActionValues;
	idle: number;
	pressed: number;
	actuationPoint: number;
	// 0 mirrors actuationPoint
	deactuationPoint: number;
	rtMode: number;
	rtPressSensitivity: number;
	// 0 mirrors rtPressSensitivity
	rtReleaseSensitivity: number;
	// 0 is no partner, otherwise the partner channel index plus one
	socdPartner: number;
};

type State = {
	triggers: Trigger[];
	loadingTriggers: boolean;
};

type Actions = {
	fetchHETriggers: () => void;
	setHETrigger: (trigger: Trigger & { id: number }) => void;
	setAllHETriggers: (trigger: Partial<Trigger>) => void;
	saveHETriggers: () => Promise<object>;
};

export const TRAVEL_MAX = 1000;

// Travel is stored as tenths of a percent, so 450 reads as 45.0%
export const formatTravel = (value: number) => `${(value / 10).toFixed(1)}%`;

const DEFAULT_TRIGGER: Trigger = {
	action: -10,
	idle: 150,
	pressed: 3500,
	actuationPoint: 450,
	deactuationPoint: 0,
	rtMode: 0,
	rtPressSensitivity: 30,
	rtReleaseSensitivity: 0,
	socdPartner: 0,
};

const INITIAL_STATE: State = {
	// Array(32).map leaves the holes untouched, so the defaults never appear
	triggers: Array.from({ length: 32 }, () => ({ ...DEFAULT_TRIGGER })),
	loadingTriggers: false,
};

const useHETriggerStore = create<State & Actions>()((set, get) => ({
	...INITIAL_STATE,
	fetchHETriggers: async () => {
		set({ loadingTriggers: true });
		const triggers = await WebApi.getHETriggerCalibrations();
		set((state) => ({
			...state,
			...triggers,
			loadingTriggers: false,
		}));
	},
	setHETrigger: ({ id, ...trigger}) => {
		set((state) => {
			const newTriggers = [...state.triggers];
			if (newTriggers[id]) {
				newTriggers[id] = trigger;
			}

			return {
				...state,
				triggers: newTriggers,
			};
		});
	},
	setAllHETriggers: (triggerValues) => {
		set((state) => ({
			...state,
			triggers: state.triggers.map((trigger) => ({
				...trigger,
				...triggerValues,
			})),
		}));
	},

	saveHETriggers: async () => WebApi.setHETriggerCalibrations(get()),
}));

export default useHETriggerStore;
