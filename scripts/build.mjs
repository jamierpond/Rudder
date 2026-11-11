// Dispatch to scripts/build-<platform>.mjs for the current platform.
import { main, platformScript, runNode } from './lib.mjs';

await main(() => runNode(platformScript('build'), process.argv.slice(2)));
