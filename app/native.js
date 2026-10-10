import { Capacitor } from '@capacitor/core';
import { BleClient } from '@capacitor-community/bluetooth-le';
import { Filesystem, Directory, Encoding } from '@capacitor/filesystem';
import { Share } from '@capacitor/share';
import { createTransport } from './transport.js';

const transport = createTransport(BleClient);
window.OpenRowNative = {
  available: Capacitor.isNativePlatform(),
  get connected() { return transport.connected; },
  connect: options => transport.connect(options),
  disconnect: () => transport.disconnect(),
  async exportFile(filename, content) {
    const result = await Filesystem.writeFile({
      path: `Sessions/${filename}`,
      data: content,
      directory: Directory.Documents,
      encoding: Encoding.UTF8,
      recursive: true
    });
    // Save before sharing: cancelling the sheet never loses the exported file.
    try {
      await Share.share({ title: filename, files: [result.uri] });
    } catch (_) {
      // It remains available in Files → On My iPad → openRow → Sessions.
    }
    return result.uri;
  }
};
