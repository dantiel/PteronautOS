import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import CoffeeScript from 'coffeescript';

// Exercise the real panel methods without a browser or a connected receiver.
const source = readFileSync(new URL('../src/pages/flight-profiles-panel.coffee', import.meta.url), 'utf8')
    .replace(/^import .*$/gm, '')
    .replace('export default FlightProfilesPanel', 'globalThis.Panel = FlightProfilesPanel');
const compiled = CoffeeScript.compile(source, {bare: true});
const fields = {
    strokeFerocity: 'stroke_ferocity', returnFerocity: 'return_ferocity',
    glideAngleDeg: 'glide_angle_deg', flappingAngleDeg: 'flapping_angle_deg',
    aileronScale: 'aileron_scale', elevatorScale: 'elevator_scale',
    rudderFerocityRange: 'rudder_ferocity_range',
    rudderAmplitudeDifferential: 'rudder_amplitude_differential',
    elevatorFerocityMix: 'elevator_ferocity_mix', throttleFerocityMix: 'throttle_ferocity_mix',
    throttleFrequencyMix: 'throttle_frequency_mix', ferocityShapeMix: 'ferocity_shape_mix'
};
function setup() {
    const requests = [];
    const context = vm.createContext({
        PteroElement: class { _t(key) { return key; } },
        renderFn() {}, Fmt: {f0: String}, i18n: {},
        customElements: {define() {}}, URLSearchParams,
        setTimeout() { return 1; }, clearTimeout() {},
        fetch: async (url, options) => {
            requests.push({url, body: new URLSearchParams(options.body)});
            return {ok: true, status: 200, json: async () => ({saved: true})};
        }
    });
    vm.runInContext(compiled, context);
    const panel = new context.Panel();
    const config = {ornithopter: {flight_profiles: panel.flightProfiles.map(p =>
        Object.fromEntries(Object.entries(fields).map(([prop, key]) => [key, p[prop]]))
    )}};
    return {panel, context, config, requests};
}

test('telemetry alone cannot unlock saving default profiles', async () => {
    const {panel, requests} = setup();
    panel.stateLoaded = true;
    assert.equal(panel._uiLocked(), true);
    await panel._saveConfig();
    assert.equal(requests.length, 0);
});

test('older saved profiles default new profile mixes to legacy modes', () => {
    const {panel, config} = setup();
    for (const profile of config.ornithopter.flight_profiles) {
        delete profile.throttle_frequency_mix;
        delete profile.ferocity_shape_mix;
    }
    panel._applyConfig(config);
    assert.equal(panel.configLoaded, true);
    assert.deepEqual(Array.from(panel.flightProfiles, p => p.throttleFrequencyMix), [0, 0, 0]);
    assert.deepEqual(Array.from(panel.flightProfiles, p => p.ferocityShapeMix), [0, 0, 0]);
});

test('save and switch back preserves all fields in each slot', async () => {
    const {panel, config, requests} = setup();
    panel._applyConfig(config);
    for (const slot of [0, 2, 1]) {
        panel._onEditProfile({target: {value: String(slot)}});
        panel.strokeFerocity = 21 + slot;
        panel.elevatorFerocityMix = 81 + slot;
        panel.throttleFrequencyMix = 61 + slot;
        panel.ferocityShapeMix = 41 + slot;
        await panel._saveConfig();
        assert.equal(requests.at(-1).body.get('flight_profile'), String(slot));
        assert.equal(requests.at(-1).body.get('throttle_frequency_mix'), String(61 + slot));
        assert.equal(requests.at(-1).body.get('ferocity_shape_mix'), String(41 + slot));
        assert.equal(panel.saveState, 'saved');
    }
    for (const slot of [2, 0, 1]) {
        panel._onEditProfile({target: {value: String(slot)}});
        assert.equal(panel.strokeFerocity, 21 + slot);
        assert.equal(panel.elevatorFerocityMix, 81 + slot);
        assert.equal(panel.throttleFrequencyMix, 61 + slot);
        assert.equal(panel.ferocityShapeMix, 41 + slot);
    }
});

test('failure and unconfirmed success never advance the saved cache', async () => {
    const {panel, context, config} = setup();
    panel._applyConfig(config);
    const old = panel.flightProfiles[1].strokeFerocity;
    panel.strokeFerocity = 99;
    for (const response of [
        {ok: false, status: 500, json: async () => ({saved: false, error: 'Flash full'})},
        {ok: true, status: 200, json: async () => ({ok: true})},
        {ok: true, status: 200, json: async () => { throw new Error('Invalid JSON'); }}
    ]) {
        context.fetch = async () => response;
        await panel._saveConfig();
        assert.equal(panel.saveState, 'error');
        assert.equal(panel.flightProfiles[1].strokeFerocity, old);
        assert.equal(panel.strokeFerocity, 99);
        assert.equal(panel._saveInFlight, false);
    }
    panel._onEditProfile({target: {value: '0'}});
    panel._onEditProfile({target: {value: '1'}});
    assert.equal(panel.strokeFerocity, 99, 'failed draft survives switching slots');
    assert.equal(panel.saveState, 'error');
    context.fetch = async () => ({ok: true, json: async () => ({saved: true})});
    await panel._saveConfig();
    assert.equal(panel.flightProfiles[1].strokeFerocity, 99);
    assert.equal(panel._unsavedProfiles[1], undefined);
});

test('queued edit saves after the first immutable request snapshot', async () => {
    const {panel, context, config} = setup();
    panel._applyConfig(config);
    const pending = [];
    context.fetch = (url, options) => new Promise(resolve => pending.push({
        body: new URLSearchParams(options.body), resolve
    }));
    panel.strokeFerocity = 20;
    const first = panel._saveConfig();
    panel._onEditProfile({target: {value: '2'}});
    assert.equal(panel.editProfile, 1, 'slot cannot switch during a save');
    panel.strokeFerocity = 40;
    await panel._saveConfig();
    assert.equal(pending.length, 1);
    pending[0].resolve({ok: true, json: async () => ({saved: true})});
    await first;
    assert.equal(pending.length, 2);
    assert.equal(pending[0].body.get('stroke_ferocity'), '20');
    assert.equal(pending[1].body.get('stroke_ferocity'), '40');
    assert.equal(panel.flightProfiles[1].strokeFerocity, 20);
    pending[1].resolve({ok: true, json: async () => ({saved: true})});
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(panel.flightProfiles[1].strokeFerocity, 40);
});

test('virtual-stick changes cannot update the saved profile cache', async () => {
    const {panel, config} = setup();
    panel._applyConfig(config);
    panel.strokeFerocity = 99;
    await panel._sendStickChannelNow(6, 1811);
    assert.equal(panel.flightProfiles[1].strokeFerocity, 50);
});

test('invalid values are not sent', async () => {
    const {panel, config, requests} = setup();
    panel._applyConfig(config);
    for (const value of [NaN, Infinity, -1, 101, 0.5]) {
        panel.strokeFerocity = value;
        await panel._saveConfig();
        assert.equal(panel.saveState, 'error');
    }
    assert.equal(requests.length, 0);
});
