# Install openRow on your iPad

This is a native iPad/iPhone app containing the existing Mantis dashboard and saved-session viewer. Native Bluetooth connects directly to `openRow_RX`; the dashboard assets are bundled and do not need internet. The existing GitHub Pages dashboard still works as before in supported browsers. No receiver/transmitter firmware changes are needed.

## What you need

- A Mac with **Xcode 26 or newer** (and a macOS version compatible with that Xcode).
- **Node.js 22 or newer**, with npm.
- An iPad running **iPadOS 15 or newer**, and a cable to connect it to the Mac for initial installation.
- An Apple Account. A **free Personal Team works**, but its development provisioning expires after **7 days**: rebuild and run from Xcode to renew. A paid Apple Developer Program membership is needed for TestFlight/App Store distribution.

Apple signing is required. This repository does not contain a signed `.ipa`; GitHub's unsigned build check cannot produce an app you can install by tapping a link.

## Build and install

Run in Terminal on your Mac:

```sh
git clone https://github.com/zmalkani/openRow-mantis.git
cd openRow-mantis
npm ci
npm run ios:sync
npm run ios:open
```

If you already cloned the repo, update that checkout with `git pull` instead of cloning again. The iOS project is already included; do not run `cap add ios` again. This project uses Swift Package Manager, so CocoaPods is not required.

In Xcode:

1. Add your Apple Account under **Xcode → Settings → Accounts**.
2. Connect and unlock your iPad, and accept **Trust This Computer** if prompted.
3. Select the **App project**, then the **App target → Signing & Capabilities**. Leave **Automatically manage signing** enabled and select your **Personal Team** (or paid developer team).
4. If Xcode says the bundle identifier is unavailable, change `ca.zmalkani.openrow.mantis` to your own unique identifier in the target's signing settings.
5. Select your **physical iPad** in the run destination picker. Click **Run ▶**.
6. If prompted on iPadOS 16+, enable **Settings → Privacy & Security → Developer Mode**, restart, and confirm. If the iPad requests developer trust, use **Settings → General → VPN & Device Management** to trust your developer profile.
7. Open **openRow** on the iPad. Power on TX and RX, tap **Connect**, allow Bluetooth, and choose **openRow_RX**.

The app icon stays on your Home Screen. No internet is needed during a session.

## Recording and exporting

- Keep the app visible during live recording. It keeps the screen awake while active; background recording is not implemented.
- **Export CSV** / **Export TXT** save in **Files → On My iPad → openRow → Sessions**, then open the system share sheet. Cancelling sharing keeps the saved export.
- Session data is held in memory until exported. Export before closing the app or leaving the live dashboard.
- Use **Back to Version Selector → Data Viewer** to open a saved CSV/TXT via the Files picker. The original V1 hardware dashboard is not packaged in this app.
- If Bluetooth access was denied, enable **openRow** under **Settings → Privacy & Security → Bluetooth**.
- Connect from only one app/device at a time. Disconnect the desktop browser before using the iPad.
- The iOS simulator cannot test Bluetooth. Verify the connection and exports on a physical iPad with the RX powered on.

## Updating

```sh
git pull
npm ci
npm run ios:sync
npm run ios:open
```

Click Run in Xcode again. If you used your own bundle identifier, keep it the same when updating so existing exported files remain in the same app container.

## Verification

`npm test` exercises notification delivery, disconnection, scan cancellation, failed subscriptions, UUID validation, Bluetooth-off handling, and reconnect cleanup using a mocked BLE client. `npm run ios:sync` builds the bundled web assets and synchronizes native dependencies. The **iPad app** GitHub Actions workflow compiles the iOS app without signing on a Mac runner. Neither check replaces a real iPad/RX Bluetooth test.

References: [Capacitor iOS requirements](https://capacitorjs.com/docs/ios), [Apple Personal Team limits](https://developer.apple.com/help/account/basics/about-your-developer-account), [Bluetooth plugin](https://github.com/capacitor-community/bluetooth-le).
