// Dispatch to Scripts/install-<platform>.mjs for the current platform.
import { main, platformScript, runNode } from './lib.mjs';

await main(() => runNode(platformScript('install'), process.argv.slice(2)));
