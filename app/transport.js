// Native BLE lifecycle kept independent of the DOM so failed subscriptions and
// unexpected receiver disconnects can be tested without physical hardware.
export function createTransport(ble) {
  let session = null;
  let busy = false;
  let initialized = false;
  const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

  function finish(current) {
    if (session !== current) return;
    session = null;
    current.onDisconnect();
  }

  return {
    get connected() { return session?.ready === true; },
    async connect({ serviceUUID, charUUID, onData, onDisconnect }) {
      if (busy || session) throw new Error('A connection is already active.');
      if (!uuid.test(serviceUUID) || !uuid.test(charUUID)) {
        throw new Error('Enter valid 128-bit Service and Characteristic UUIDs.');
      }
      busy = true;
      let current;
      try {
        if (!initialized) {
          await ble.initialize();
          initialized = true;
        }
        if (!await ble.isEnabled()) throw new Error('Turn on Bluetooth in iPad Settings, then try again.');
        const device = await ble.requestDevice({ services: [serviceUUID] });
        current = { deviceId: device.deviceId, serviceUUID, charUUID, onDisconnect, ready: false };
        session = current;
        await ble.connect(current.deviceId, () => finish(current));
        if (session !== current) throw new Error('The receiver disconnected while connecting.');
        await ble.startNotifications(current.deviceId, serviceUUID, charUUID, value => {
          if (session === current) onData(value);
        });
        if (session !== current) throw new Error('The receiver disconnected while subscribing.');
        current.ready = true;
        return device.name || 'openRow_RX';
      } catch (error) {
        // Release the peripheral if service discovery/subscription failed.
        if (current) {
          if (session === current) session = null;
          try { await ble.disconnect(current.deviceId); } catch (_) { /* Already gone. */ }
        }
        throw error;
      } finally {
        busy = false;
      }
    },
    async disconnect() {
      const current = session;
      if (!current) return;
      try {
        await ble.stopNotifications(current.deviceId, current.serviceUUID, current.charUUID);
      } catch (_) { /* Still disconnect if the receiver went out of range. */ }
      // Leave the session intact if disconnect fails so the user can retry.
      await ble.disconnect(current.deviceId);
      finish(current);
    }
  };
}
