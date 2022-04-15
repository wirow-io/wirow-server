process.umask(0o077);

const fs = require('fs');
const path = require('path');
const https = require('https');
const util = require('util');

const writeFile = util.promisify(fs.writeFile);
const readFile = util.promisify(fs.readFile);
const mkdir = util.promisify(fs.mkdir);

if (process.argv.length < 5) {
  console.error('Usage node lic.js <license_vars_header_file> <ssl_verify> <app_version> [license_request_file]');
  process.exit(1);
}

const varsFile = process.argv[2];
const sslVerify = parseInt(process.argv[3]);
const appVersion = process.argv[4];
const requestFile = process.argv[5];

if (requestFile != null) {
  fs.accessSync(requestFile, fs.constants.R_OK);
}

const vars = ['#pragma once\n'];

async function writeSpec() {
  vars.push('');
  console.log(`Generating ${varsFile}`);
  const data = vars.join('\n');
  const pp = path.parse(varsFile);
  if (pp.dir) {
    await mkdir(pp.dir, { recursive: true });
  }
  console.log(data);
  await writeFile(varsFile, data);
}

async function doAcquireLicense() {
  const json = JSON.parse(await readFile(requestFile));
  if (json.build_template === true) {
    // Template for future builds for clients
    return;
  }
  const server = json.server;
  const accessToken = json.accessToken;
  if (typeof accessToken !== 'string') {
    throw new Error('No access token (accessToken) specified');
  }
  let escaped = { ...json };
  ['server', 'owner', 'email', 'address', 'terms', 'login', 'product'].forEach((k) => {
    if (typeof escaped[k] !== 'string') {
      throw new Error(`Missing ${k} in license request file: ${requestFile}`);
    }
    escaped[k] = escapeCString(escaped[k]);
  });

  const resp = JSON.parse(
    await new Promise((resolve, reject) => {
      const buf = [];
      const req = https.request(
        `${server}/registration`,
        {
          ciphers: 'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384',
          agent: https.Agent({
            rejectUnauthorized: sslVerify > 0 ? true : false,
          }),
          headers: {
            'X-Access-Token': accessToken,
          },
          method: 'PUT',
        },
        (r) => {
          if (r.statusCode !== 200) {
            reject(`Recieved invalid status code ${r.statusCode}`);
            return;
          }
          r.on('data', (data) => buf.push(data));
          r.on('end', () => resolve(buf.join()));
        }
      );
      req.on('error', reject);
      req.end(
        JSON.stringify({
          owner: json.owner,
          email: json.email,
          login: json.login,
          person: json.person,
          lang: json.lang,
          address: json.address,
          terms: json.terms,
          product: json.product,
          version: appVersion
        })
      );
    })
  );

  ['id', 'owner', 'ck', 'pwh', 'ipw'].forEach((k) => {
    if (typeof resp[k] !== 'string') {
      throw new Error(`Invalid server response, missing key: ${k}`);
    }
    escaped[k] = escapeCString(resp[k]);
  });

  ['pws'].forEach((k) => {
    if (typeof resp[k] !== 'number') {
      throw new Error(`Invalid server response, missing key: ${k}`);
    }
    escaped[k] = resp[k];
  });

  // Save one time password
  fs.writeFileSync('./.ipw', resp['ipw']);
  // Save license ID
  fs.writeFileSync('./.id', resp['id']);

  vars.push(`#define LICENSE_SERVER "${escaped.server}"`);
  vars.push(`#define LICENSE_ID "${escaped.id}"`);
  vars.push(`#define LICENSE_CK "${escaped.ck}"`);
  vars.push(`#define LICENSE_OWNER "${escaped.owner}"`);
  vars.push(`#define LICENSE_LOGIN "${escaped.login}"`);
  vars.push(`#define LICENSE_EMAIL "${escaped.email}"`);
  vars.push(`#define LICENSE_TERMS "${escaped.terms}"`);
  vars.push(`#define LICENSE_ADM_PWS ${escaped.pws}`);
  vars.push(`#define LICENSE_ADM_PWH "${escaped.pwh}"`);
  vars.push(`#define LICENSE_PRODUCT "${escaped.product}"`);
}

async function run() {
  if (requestFile != null) {
    await doAcquireLicense();
  }
  return writeSpec();
}

function escapeCString(data) {
  const table = {
    0: '\\x00',
    1: '\\x01',
    2: '\\x02',
    3: '\\x03',
    4: '\\x04',
    5: '\\x05',
    6: '\\x06',
    7: '\\a',
    8: '\\b',
    9: '\\t',
    10: '\\n',
    11: '\\v',
    12: '\\f',
    13: '\\r',
    14: '\\x0e',
    15: '\\x0f',
    16: '\\x10',
    17: '\\x11',
    18: '\\x12',
    19: '\\x13',
    20: '\\x14',
    21: '\\x15',
    22: '\\x16',
    23: '\\x17',
    24: '\\x18',
    25: '\\x19',
    26: '\\x1a',
    27: '\\x1b',
    28: '\\x1c',
    29: '\\x1d',
    30: '\\x1e',
    31: '\\x1f',
    0x27: "\\'",
    0x22: '\\"',
    0x3f: '\\?',
    0x5c: '\\\\',
    0x7f: '\\x7f',
    0xa0: '\\xa0',
  };

  const res = [];
  Buffer.from(data).forEach((b) => {
    let e = table[b];
    if (e === undefined) {
      if (b > 127 && b <= 255) {
        e = '\\x' + b.toString(16);
      } else {
        e = String.fromCharCode(b);
      }
    }
    res.push(e);
  });

  return res.join('');
}

run()
  .then(() => {
    console.log('License info was generated successfully');
  })
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });
