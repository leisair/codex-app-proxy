import fs from 'node:fs';

const [guidePath, configPath] = process.argv.slice(2);
if (!guidePath || !configPath) {
  throw new Error('Usage: node verify-start-here.mjs <START-HERE.html> <default_config.json>');
}

const html = fs.readFileSync(guidePath, 'utf8');
const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));

const required = [
  'id="preset"', 'id="proxyType"', 'id="proxyHost"', 'id="proxyPort"',
  'id="disableQuic"', 'id="bypassList"', 'id="save"', 'id="resetAll"',
  'id="jsonOutput"', 'id="commandOutput"', 'V2RayN', 'Clash Verge Rev',
  'Clash / Mihomo', '通用 SOCKS5', '出厂配置', '恢复全部出厂值',
  'START-HERE.html', 'CodexProxyLauncher.exe'
];
for (const marker of required) {
  if (!html.includes(marker)) throw new Error(`START-HERE missing marker: ${marker}`);
}

if (/<script\b[^>]*\bsrc\s*=/i.test(html) || /<link\b[^>]*\bhref\s*=/i.test(html)) {
  throw new Error('START-HERE must not load external scripts or stylesheets');
}

const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/gi)].map(match => match[1]);
if (scripts.length !== 1) throw new Error(`Expected one inline script, found ${scripts.length}`);
new Function(scripts[0]);

if (config.proxy.type !== 'http' || config.proxy.host !== '127.0.0.1' || config.proxy.port !== 10808) {
  throw new Error('default_config.json no longer matches the documented factory proxy');
}
if (config.disable_quic !== true || !Array.isArray(config.bypass_list) || config.bypass_list.length < 4) {
  throw new Error('default_config.json is missing required safe defaults');
}
for (const value of ['http', '127.0.0.1', '10808']) {
  if (!html.includes(value)) throw new Error(`START-HERE does not expose factory value: ${value}`);
}

console.log('START-HERE verification passed');
