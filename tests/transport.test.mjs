import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createTransport } from '../app/transport.js';

const serviceUUID = '12345678-1234-5678-1234-56789abcdef0';
const charUUID = 'abcdef01-1234-5678-1234-56789abcdef0';

function setup(overrides = {}) {
  const calls = [];
  let dropped, notified;
  const ble = {
    initialize: async () => calls.push('initialize'),
    isEnabled: async () => true,
    requestDevice: async options => { calls.push(options); return { deviceId: 'rx', name: 'openRow_RX' }; },
    connect: async (id, callback) => { dropped = callback; calls.push(['connect', id]); },
    startNotifications: async (id, service, characteristic, callback) => {
      notified = callback; calls.push(['subscribe', id, service, characteristic]);
    },
    stopNotifications: async () => calls.push('stop'),
    disconnect: async () => calls.push('disconnect'),
    ...overrides
  };
  const transport = createTransport(ble);
  const values = [];
  let disconnects = 0;
  const options = { serviceUUID, charUUID, onData: v => values.push(v), onDisconnect: () => disconnects++ };
  return { transport, calls, options, values,
    drop: () => dropped(), notify: value => notified(value), get disconnects() { return disconnects; } };
}

test('receiver notifications retain the firmware payload and selected UUIDs', async () => {
  const t = setup();
  assert.equal(await t.transport.connect(t.options), 'openRow_RX');
  assert.equal(t.transport.connected, true);
  assert.deepEqual(t.calls[1], { services: [serviceUUID] });
  assert.deepEqual(t.calls[3], ['subscribe', 'rx', serviceUUID, charUUID]);
  const value = new DataView(new TextEncoder().encode('1234,-2.35,0001').buffer);
  t.notify(value);
  assert.equal(t.values[0], value);
  await t.transport.disconnect();
  assert.equal(t.transport.connected, false);
  assert.equal(t.disconnects, 1);
});

test('out-of-range disconnect clears state and ignores late telemetry', async () => {
  const t = setup();
  await t.transport.connect(t.options);
  t.drop(); t.drop(); t.notify(new DataView(new ArrayBuffer(1)));
  assert.equal(t.transport.connected, false);
  assert.equal(t.disconnects, 1);
  assert.equal(t.values.length, 0);
  await t.transport.connect(t.options);
  assert.equal(t.calls.filter(c => c === 'initialize').length, 1);
});

test('subscription failures release the receiver and allow retry', async () => {
  let fail = true;
  const t = setup({ startNotifications: async () => { if (fail) throw new Error('Missing characteristic'); } });
  await assert.rejects(t.transport.connect(t.options), /Missing characteristic/);
  assert.equal(t.transport.connected, false);
  assert.ok(t.calls.includes('disconnect'));
  fail = false;
  await t.transport.connect(t.options);
  assert.equal(t.transport.connected, true);
});

test('invalid UUIDs and disabled Bluetooth never start a scan', async () => {
  const invalid = setup();
  await assert.rejects(invalid.transport.connect({ ...invalid.options, charUUID: 'wrong' }), /UUID/);
  assert.deepEqual(invalid.calls, []);
  const disabled = setup({ isEnabled: async () => false });
  await assert.rejects(disabled.transport.connect(disabled.options), /Turn on Bluetooth/);
  assert.deepEqual(disabled.calls, ['initialize']);
});

test('cancelling device selection leaves Connect usable', async () => {
  const t = setup({ requestDevice: async () => { throw new Error('Cancelled'); } });
  await assert.rejects(t.transport.connect(t.options), /Cancelled/);
  await assert.rejects(t.transport.connect(t.options), /Cancelled/);
  assert.equal(t.transport.connected, false);
});

test('receiver loss during subscription does not report a successful connection', async () => {
  let drop;
  const t = setup({ connect: async (_, callback) => { drop = callback; }, startNotifications: async () => drop() });
  await assert.rejects(t.transport.connect(t.options), /disconnected while subscribing/);
  assert.equal(t.transport.connected, false);
});

test('notification cleanup failure still disconnects the receiver', async () => {
  const t = setup({ stopNotifications: async () => { throw new Error('Receiver gone'); } });
  await t.transport.connect(t.options);
  await t.transport.disconnect();
  assert.equal(t.transport.connected, false);
  assert.ok(t.calls.includes('disconnect'));
});
