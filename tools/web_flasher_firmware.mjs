// Put a release's firmware next to the browser flasher, for the GitHub Pages build (.github/workflows/pages.yml).
// Browsers can't download GitHub release assets directly (no CORS headers), so the flasher loads the
// firmware from its own site: companion/flash/firmware/latest.json describes it.
//
//   node tools/web_flasher_firmware.mjs RELEASE_JSON ASSETS_DIR OUT_DIR   (a release from the GitHub API, and its downloaded .bin and .elf)
//   node tools/web_flasher_firmware.mjs --none OUT_DIR                    (no release yet)
//   node tools/web_flasher_firmware.mjs --local BIN OUT_DIR               (a local build, for trying the page; reads the .elf next to it)

import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";
import { settingsSizeInElf } from "../companion/flash/linnstrument.js";
import { checkImage } from "../companion/flash/samba.js";

function publish(binPath, elfPath, outDir, info) {
  const image = new Uint8Array(readFileSync(binPath));
  checkImage(image);
  const settingsSize = elfPath && existsSync(elfPath) ? settingsSizeInElf(new Uint8Array(readFileSync(elfPath))) : null;
  mkdirSync(outDir, { recursive: true });
  const file = basename(binPath);
  copyFileSync(binPath, join(outDir, file));
  const latest = { ...info, file, size: image.length, sha256: createHash("sha256").update(image).digest("hex"), settingsSize };
  writeFileSync(join(outDir, "latest.json"), JSON.stringify(latest, null, 2) + "\n");
  console.log(`${file}: ${image.length} bytes, settings ${settingsSize ?? "unknown"}, ${info.tag}`);
}

const [mode, ...rest] = process.argv.slice(2);
if (mode === "--none") {
  mkdirSync(rest[0], { recursive: true });
  writeFileSync(join(rest[0], "latest.json"), JSON.stringify({ file: null }) + "\n");
  console.log("no release");
} else if (mode === "--local") {
  const [bin, outDir] = rest;
  publish(bin, bin.replace(/\.bin$/, ".elf"), outDir, { tag: "local build", local: true, prerelease: true, url: "#", published: new Date().toISOString() });
} else {
  const [releaseJson, assetsDir, outDir] = [mode, ...rest];
  const release = JSON.parse(readFileSync(releaseJson, "utf8"));
  const files = readdirSync(assetsDir);
  const bin = files.find((f) => f.endsWith(".bin"));
  const elf = files.find((f) => f.endsWith(".elf"));
  if (!bin) throw new Error(`no .bin in ${assetsDir}`);
  publish(join(assetsDir, bin), elf && join(assetsDir, elf), outDir, {
    tag: release.tag_name, prerelease: !!release.prerelease, url: release.html_url, published: release.published_at,
  });
}
