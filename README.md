# Earbud Controls for Windows

Native Win + W controls for Sony WF-C710N: batteries, noise cancelling, ambient
sound, presets, custom EQ and Clear Bass. Independent project; not affiliated
with Sony or Microsoft. Project source is MIT licensed.

## Install the prerelease

Download **EarbudControls-0.3.3-windows-x64.zip** from
[Releases](https://github.com/KanuTomer/Sound-Connect/releases), extract the entire
ZIP and run **Install.cmd**. No compiler or source checkout is needed.

Requirements: Windows 11 x64 (build 22621+), Windows Web Experience Pack and
Developer Mode enabled in Windows Settings > System > Advanced > For developers
(or Privacy & security > For developers on older Windows). Other models and ARM64
are unverified. Pair and connect WF-C710N manually in Windows Bluetooth settings.

The free prerelease is signed with a local certificate. Setup requests administrator
permission only to trust its public certificate; installation runs as the original
user. It includes the Microsoft-signed x64 runtime dependency for machines missing
Microsoft.WindowsAppRuntime.2 version 2.5.1.0 or newer. It does not disable SmartScreen,
change global PowerShell policy, pair devices or change earbud settings on startup.
Windows download/reputation prompts may still appear. The certificate expires
17 October 2027; renewal will require a future trust update.

Add **WF-C710N** manually using Win + W > Add widgets. Open **Earbud Controls**
from Start for companion controls. If the picker is stale after enabling Developer
Mode, close and reopen the board; a Windows sign-out or restart may be needed.

- Small: filled battery indicators and three direct icon-only mode buttons; no EQ customization.
- Medium: modes, ambient level, selected-band EQ and Clear Bass editing.
- Large: curve summary and editing for all five EQ bands plus Clear Bass.
- Presets have their own view; Preferences uses the widget header's Customize action.
- The Windows host controls hover, focus and typography. Graphs are readouts;
  edits use stepped controls rather than graph dragging.

Missing battery readings remain unknown. Cached values are labelled. Startup and
reconnect read the device and never apply demo or cached EQ values. Logging is
opt-in. There is no installed service or startup entry.

## Updates and uninstall

Download and run a newer release's Install.cmd. The package identity stays the same,
so preferences are retained. There is no automatic updater. Uninstall through
Settings > Apps > Installed apps > Earbud Controls. This does not remove Bluetooth
pairing or shared Microsoft runtimes. Certificate-trust removal is described in
the ZIP's README.txt; remove only this project's exact public certificate.

## Build and test

Windows x64, Git, Visual Studio 2022 C++ Build Tools, Windows SDK 10.0.26100 and
CMake 3.31+ are required. In Windows PowerShell from the repository:

```powershell
./scripts/build.ps1 -Check
./tests/install_tests.ps1
```

Bootstrap fetches pinned SonyHeadphonesClient revision
`b98309446e2aff7538959eb6518ab694c5264cbe`, applies the WF-C710N EQ-notification
compatibility patch idempotently, downloads pinned Widgets/C++/WinRT packages and
generates build-time projections. Existing incompatible dependencies are left
untouched. CI uses windows-2025 and publishes unsigned debugging builds only.

The production manifest is the authoritative version. Maintainers sign locally;
no private key is uploaded to GitHub or CI. To package an explicitly approved UI:

```powershell
# Build the demo provider and prepare review files without registering an app.
./scripts/register-ui-review.ps1 -PrepareOnly
./scripts/prepare-release.ps1 -UserApprovedUi
```

Use `-UserApprovedUi` only after actual human approval of the current UI. It records
that approval separately from automated native visual checks. The default packaging
gate still requires native review evidence. The development certificate is local
and non-exportable; a new maintainer cannot sign releases with the original key.
The installer pins the original public certificate thumbprint.

For a read-only setup check, run the extracted release helper with `-PackagePath`
and `-CheckOnly`. Existing `-Version` and `-TrustCertificate` options remain available
in the source installer. `scripts/reset-local-install.ps1` is a maintainer-only,
version-specific cleanup tool: it defaults to preview, requires a rollback backup
to execute, and refuses unexpected identities and reparse-point traversal.

## Testing limits and privacy

Noise control, presets, EQ writes and restoration were historically verified on
physical WF-C710N firmware 1.0.5. Passing CI does not establish UI acceptance,
Bluetooth reliability, gaming performance or a fresh-machine installation pass.
This release remains a prerelease until the friend-machine flow is verified.

Runtime state may include the selected Bluetooth address, firmware, cached readings
and preferences. It stays in the app's local data folder and is excluded from the
repository and release. Do not attach raw state or diagnostic logs to public issues.
No telemetry or cloud account is required by this app.

## Third-party software

See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and the full license copies
included in the release. Microsoft's redistributable packages retain their own
license terms; this project's MIT license does not replace them.
