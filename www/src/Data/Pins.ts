import { GpioAction } from '@proto/enums';
import { createEnumRecord } from '../Services/Utilities';

export const BUTTON_ACTIONS = createEnumRecord(GpioAction)

// Actions only the hall effect add-on can drive. They are proportional, so they
// do nothing on a digital pin and are kept out of the GPIO mapping list.
export const HE_ONLY_ACTIONS = [
	BUTTON_ACTIONS.ANALOG_TRIGGER_LT,
	BUTTON_ACTIONS.ANALOG_TRIGGER_RT,
];

export const PIN_DIRECTIONS = {
	DIRECTION_INPUT: 0,
	DIRECTION_OUTPUT: 1,
} as const;

export type PinActionKeys = keyof typeof BUTTON_ACTIONS;
export type PinActionValues = (typeof BUTTON_ACTIONS)[PinActionKeys];

type PinDirectionKeys = keyof typeof PIN_DIRECTIONS;
export type PinDirectionValues = (typeof PIN_DIRECTIONS)[PinDirectionKeys];
