const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");
const { spawnSync } = require("node:child_process");
const ts = require("typescript");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, name), "utf8");
const calibration = ts.createSourceFile(
	"HECalibration.tsx",
	read("www/src/Components/HECalibration.tsx"),
	ts.ScriptTarget.Latest,
	true,
	ts.ScriptKind.TSX,
);
const compile = (source) =>
	ts.transpileModule(source, {
		compilerOptions: {
			target: ts.ScriptTarget.ES2022,
			module: ts.ModuleKind.CommonJS,
			esModuleInterop: true,
		},
	}).outputText;

function functions(names, bindings) {
	const declarations = [];
	const effects = [];
	function visit(node) {
		if (
			ts.isVariableDeclaration(node) &&
			names.includes(node.name.getText(calibration))
		) {
			declarations.push(
				`const ${node.name.getText(calibration)} = ${node.initializer.getText(calibration)};`,
			);
		}
		if (
			ts.isCallExpression(node) &&
			node.expression.getText(calibration) === "useEffect"
		)
			effects.push(node);
		ts.forEachChild(node, visit);
	}
	visit(calibration);
	if (names.includes("unmount"))
		declarations.push(
			`const unmount = ${effects.at(-1).arguments[0].getText(calibration)};`,
		);
	assert.equal(declarations.length, names.length);
	return vm.runInNewContext(
		`${compile(declarations.join("\n"))}\n({${names.join(",")}})`,
		bindings,
	);
}

function fixture() {
	let timer = 0;
	const timers = new Map();
	const cancelled = [];
	const state = { closed: false, error: "", busy: false };
	const bindings = {
		sweepAssigned: [],
		triggers: [],
		sweepUnseen: [],
		sweepSuspicious: [],
		canSaveSweep: () => true,
		sweepTimerId: { current: undefined },
		sweepSessionId: { current: undefined },
		sweepGeneration: { current: 0 },
		sweepCommitted: { current: false },
		sweepActuationPoint: 45,
		sweepDeactuationPoint: 0,
		SWEEP_POLL_MS: 250,
		document: { hidden: false },
		values: { muxChannels: 1, muxADCPin0: 26 },
		console,
		t: (key) => key,
		wholePercentToTenths: (value) => value * 10,
		setSweepChannels() {},
		setSweepActuationPoint() {},
		setSweepDeactuationPoint() {},
		setSweepBusy: (value) => {
			state.busy = value;
		},
		setSweepError: (value) => {
			state.error = value;
		},
		setShowModal: (show) => {
			state.closed = !show;
		},
		fetchHETriggers: async () => {},
		window: {
			setTimeout: (callback) => {
				timers.set(++timer, callback);
				return timer;
			},
			confirm: () => true,
		},
		clearTimeout: (id) => timers.delete(id),
		WebApi: {
			startHETriggerSweep: async () => ({ active: true, sessionId: 1 }),
			getHETriggerSweep: async () => ({
				active: true,
				sessionId: 1,
				channels: [],
			}),
			cancelHETriggerSweep: async (id) => {
				cancelled.push(id);
			},
			commitHETriggerSweep: async () => ({ saved: true }),
		},
	};
	const api = functions(
		[
			"requestSweep",
			"startSweep",
			"pollSweep",
			"stopSweepPolling",
			"cancelSweepSession",
			"saveSweep",
			"commitSweep",
			"unmount",
		],
		bindings,
	);
	return { api, bindings, state, timers, cancelled };
}

async function checkStore() {
	const trigger = {
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
	const remote = {
		triggers: Array.from({ length: 32 }, () => ({ ...trigger })),
	};
	const module = { exports: {} };
	vm.runInNewContext(compile(read("www/src/Store/useHETriggerStore.ts")), {
		module,
		exports: module.exports,
		require: (name) => {
			if (name === "../Services/WebApi")
				return {
					getHETriggerCalibrations: async () => structuredClone(remote),
				};
			if (name === "../Services/Utilities")
				return { tenthsToWholePercent: (value) => Math.round(value / 10) };
			return require(name);
		},
	});
	const store = module.exports.default;
	await store.getState().fetchHETriggers();
	store
		.getState()
		.setHETrigger({ id: 0, ...trigger, action: 5, rtMode: 2, socdPartner: 2 });
	remote.triggers[0] = {
		...trigger,
		idle: 300,
		pressed: 3000,
		actuationPoint: 600,
		deactuationPoint: 400,
	};
	const f = fixture();
	f.bindings.sweepSessionId.current = 1;
	f.bindings.fetchHETriggers = store.getState().fetchHETriggers;
	await f.api.commitSweep();
	const saved = store.getState().triggers[0];
	assert.equal(saved.action, 5);
	assert.equal(saved.rtMode, 2);
	assert.equal(saved.socdPartner, 2);
	assert.equal(saved.idle, 300);
	assert.equal(saved.actuationPoint, 600);
	assert.equal(f.state.closed, true);
}

async function checkSaveFailures() {
	for (const result of [undefined, { saved: false }, new Error("offline")]) {
		const f = fixture();
		f.bindings.sweepSessionId.current = 1;
		f.bindings.WebApi.commitHETriggerSweep = async () => {
			if (result instanceof Error) throw result;
			return result;
		};
		await f.api.commitSweep();
		assert.equal(f.state.closed, false);
		assert.equal(f.bindings.sweepSessionId.current, 1);
		assert.ok(f.state.error);
		assert.equal(f.state.busy, false);
	}
	const f = fixture();
	let commits = 0;
	f.bindings.sweepSessionId.current = 1;
	f.bindings.WebApi.commitHETriggerSweep = async () => {
		commits++;
		return { saved: true };
	};
	f.bindings.fetchHETriggers = async () => {
		throw new Error("offline");
	};
	await f.api.commitSweep();
	assert.equal(f.state.closed, false);
	f.bindings.fetchHETriggers = async () => {};
	await f.api.commitSweep();
	assert.equal(f.state.closed, true);
	assert.equal(commits, 1);
}

async function checkCancellation() {
	const f = fixture();
	let finish;
	f.bindings.WebApi.startHETriggerSweep = () =>
		new Promise((resolve) => {
			finish = resolve;
		});
	const pending = f.api.startSweep();
	await f.api.cancelSweepSession();
	finish({ active: true, sessionId: 7 });
	await pending;
	assert.deepEqual(f.cancelled, [7]);
	assert.equal(f.timers.size, 0);
	assert.equal(f.bindings.sweepSessionId.current, undefined);

	const active = fixture();
	await active.api.startSweep();
	assert.equal(active.timers.size, 1);
	active.api.unmount()();
	await Promise.resolve();
	assert.equal(active.timers.size, 0);
	assert.deepEqual(active.cancelled, [1]);

	const invalid = fixture();
	invalid.bindings.WebApi.startHETriggerSweep = async () => ({
		error: "mux channels incorrect",
	});
	await invalid.api.startSweep();
	assert.equal(invalid.state.error, "HETrigger:sweep-start-error");
	assert.equal(invalid.timers.size, 0);
}

async function checkLateResponses() {
	const polling = fixture();
	let finishPoll;
	polling.bindings.WebApi.getHETriggerSweep = () =>
		new Promise((resolve) => {
			finishPoll = resolve;
		});
	await polling.api.startSweep();
	await polling.api.cancelSweepSession();
	finishPoll({ active: true, sessionId: 1, channels: [] });
	await Promise.resolve();
	assert.equal(polling.timers.size, 0);

	const saving = fixture();
	let finishSave;
	saving.bindings.sweepSessionId.current = 1;
	saving.bindings.WebApi.commitHETriggerSweep = () =>
		new Promise((resolve) => {
			finishSave = resolve;
		});
	const pending = saving.api.commitSweep();
	await saving.api.cancelSweepSession();
	finishSave({ saved: true });
	await pending;
	assert.equal(saving.state.closed, false);
	assert.equal(saving.bindings.sweepCommitted.current, false);
}

function cppFunction(source, name) {
	const start = source.indexOf(name);
	assert.ok(start >= 0, name);
	let end = source.indexOf("{", start);
	let depth = 1;
	while (depth && ++end < source.length) {
		if (source[end] === "{") depth++;
		if (source[end] === "}") depth--;
	}
	return source.slice(start, end + 1);
}

function checkFirmware() {
	const cpp = read("src/webconfig.cpp");
	const header = read("headers/addons/he_trigger.h");
	const sweepState = cpp.slice(
		cpp.indexOf("#define HETRIGGER_SWEEP_SAMPLES"),
		cpp.indexOf("std::string startHETriggerSweep()"),
	);
	const source = `
#include <cassert>
#include <cstdint>
#include <string>
#include <ArduinoJson.h>
#include "config.pb.h"
#include "addons/he_trigger_math.h"
#define GPIO_OUT 1
using Pin_t = int;
static uint32_t now;
uint32_t time_us_32() { return now; }
void adc_gpio_init(int) {}
void gpio_init(int) {}
void gpio_set_dir(int, int) {}
void gpio_put(int, int) {}
void adc_select_input(int) {}
void busy_wait_us(uint32_t) {}
uint16_t adc_read() { return now >= 80000 && now <= 160000 ? 3500 : 150; }
struct Storage {
  AddonOptions options{};
  static Storage& getInstance() { static Storage instance; return instance; }
  AddonOptions& getAddonOptions() { return options; }
};
struct GPStorageSaveEvent { explicit GPStorageSaveEvent(bool) {} };
struct EventManager {
  static EventManager& getInstance() { static EventManager instance; return instance; }
  void triggerEvent(GPStorageSaveEvent* event) { delete event; }
};
static DynamicJsonDocument request(2048);
DynamicJsonDocument get_post_data() { return request; }
std::string serialize_json(const DynamicJsonDocument& doc) { std::string result; serializeJson(doc, result); return result; }
DynamicJsonDocument response(const std::string& text) { DynamicJsonDocument doc(12000); assert(!deserializeJson(doc, text)); return doc; }
template <typename T> void readTravel(T& value, JsonObject obj, const char* key) { value = obj[key].as<int>(); }
class HETriggerAddon {
public:
  ${cppFunction(header, "struct ScanEntry")};
  ${cppFunction(header, "struct ChannelState")};
  ChannelState channelState[HETRIGGER_COUNT]{};
  uint16_t emaFactorQ8 = 2;
  bool overrideActive = false;
  HEThresholds overrideThresholds{};
  void updateChannel(const ScanEntry&, uint16_t);
};
${cppFunction(read("src/addons/he_trigger.cpp"), "void HETriggerAddon::updateChannel(")}
${cpp.slice(cpp.indexOf("static uint32_t calibrationMuxChannels"), cpp.indexOf("// Get the HE Trigger Options"))}
${cppFunction(cpp, "std::string setHETriggerOptions()")}
${cppFunction(cpp, "static bool heInitPins")}
${cppFunction(cpp, "static void heScanChannels")}
${sweepState}
${cppFunction(cpp, "std::string startHETriggerSweep()")}
${cppFunction(cpp, "std::string getHETriggerSweep()")}
${cppFunction(cpp, "std::string commitHETriggerSweep()")}
${cppFunction(cpp, "std::string cancelHETriggerSweep()")}
void layout() {
  request.clear();
  request["muxChannels"] = 1;
  request["muxADCPin0"] = 26;
  for (int i = 1; i < 4; i++) request["muxADCPin" + std::to_string(i)] = -1;
  for (int i = 0; i < 4; i++) request["muxSelectPin" + std::to_string(i)] = -1;
  request["heTriggerMuxSettleMicros"] = 3;
  setHETriggerOptions();
}
void endpoints() {
  for (int span = 16; span <= 4095; span++) {
    const auto positive = heTravelScaleQ10(0, span);
    const auto negative = heTravelScaleQ10(span, 0);
    assert(heTravelFromRaw(0, 0, positive) == 0);
    assert(heTravelFromRaw(span, 0, positive) == 1000);
    assert(heTravelFromRaw(span, span, negative) == 0);
    assert(heTravelFromRaw(0, span, negative) == 1000);
  }
}
void smoothing() {
  for (int span : {16, 512, 1024, 2048, 3350}) {
    for (bool descending : {false, true}) {
      HETriggerAddon addon;
      HETriggerAddon::ScanEntry entry{};
      entry.idle = descending ? span : 0;
      const int pressed = descending ? 0 : span;
      entry.scaleQ10 = heTravelScaleQ10(entry.idle, pressed);
      entry.thresholds = {1000, 1000, 30, 30, 1000, 0};
      addon.channelState[0].smoothedQ8 = entry.idle << 8;
      for (int i = 0; i < 3000; i++) addon.updateChannel(entry, pressed);
      assert(addon.channelState[0].active);
    }
  }
}
void sampling() {
  auto& options = Storage::getInstance().options.heTriggerOptions;
  options.muxChannels = 1;
  options.muxADCPin0 = 26;
  options.muxADCPin1 = options.muxADCPin2 = options.muxADCPin3 = -1;
  options.selectPin0 = options.selectPin1 = options.selectPin2 = options.selectPin3 = -1;
  options.muxSettleMicros = 3;
  layout();
  request["muxChannels"] = 2;
  setHETriggerOptions();
  assert(response(startHETriggerSweep())["error"] == "mux channels incorrect");
  assert(!sweepActive);
  layout();
  request["muxADCPin0"] = 27;
  setHETriggerOptions();
  assert(response(startHETriggerSweep())["active"] == true);
  assert(sweepAdcPins[0] == 27);
  assert(options.muxADCPin0 == 26);
  calibrationADCPins[0] = 29;
  for (now = 1000; now <= 250000; now += 1000) processHETriggerSweep();
  assert(sweepAdcPins[0] == 27);
  const auto doc = response(getHETriggerSweep());
  assert(doc["channels"][0]["moved"] == true);
  assert(doc["channels"][0]["min"] == 150);
  assert(doc["channels"][0]["max"] == 3500);
}
void sessions() {
  auto& untouched = Storage::getInstance().options.heTriggerOptions.triggers[4];
  untouched.idle = 1234;
  const auto oldId = sweepSessionId;
  layout();
  startHETriggerSweep();
  request.clear();
  request["sessionId"] = oldId;
  cancelHETriggerSweep();
  assert(sweepActive);
  assert(response(commitHETriggerSweep())["saved"] == false);
  assert(sweepActive);
  request["sessionId"] = sweepSessionId;
  assert(response(commitHETriggerSweep())["saved"] == true);
  assert(!sweepActive);
  assert(untouched.idle == 1234);
  assert(!untouched.calibrated);
}
int main() { endpoints(); smoothing(); sampling(); sessions(); }
`;
	const directory = fs.mkdtempSync(path.join(os.tmpdir(), "he-regression-"));
	const executable = path.join(directory, "firmware-check");
	const compiler = process.env.CXX || "c++";
	const result = spawnSync(
		compiler,
		[
			"-std=c++17",
			"-O2",
			"-x",
			"c++",
			"-",
			"-o",
			executable,
			"-I" + path.join(root, "build/_deps/arduinojson-src/src"),
			"-I" + path.join(root, "build/proto"),
			"-I" + path.join(root, "lib/nanopb"),
			"-I" + path.join(root, "headers"),
		],
		{ input: source, encoding: "utf8" },
	);
	assert.equal(result.status, 0, result.error?.message || result.stderr);
	const run = spawnSync(executable, [], { encoding: "utf8" });
	assert.equal(run.status, 0, run.stderr);
	assert.match(
		cppFunction(
			read("src/drivers/net/NetDriver.cpp"),
			"bool NetDriver::process(",
		),
		/processHETriggerSweep\(\)/,
	);
}

async function main() {
	await checkStore();
	await checkSaveFailures();
	await checkCancellation();
	await checkLateResponses();
	const { travelFromRaw } = functions(["travelFromRaw"], {
		MIN_SPAN: 16,
		TRAVEL_MAX: 1000,
	});
	for (let span = 16; span <= 4095; span++) {
		assert.equal(travelFromRaw(span, 0, span), 1000);
		assert.equal(travelFromRaw(0, span, 0), 1000);
	}
	checkFirmware();
	console.log(
		"HE regression checks passed: pending edits, save failures, cancellation, layout, continuous sampling, session isolation, and endpoints.",
	);
}

main().catch((error) => {
	console.error(error);
	process.exitCode = 1;
});
