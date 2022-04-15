process.umask(0o077);

const fs = require('fs');
const crypto = require('crypto');
const https = require('https');
const util = require('util');
const readFile = util.promisify(fs.readFile);
const fileStat = util.promisify(fs.stat);


if (process.argv.length < 6) {
  console.error(
    'Usage node lic_complete.js <ssl_verify> <build_type> <artifact> <vars_header_file> <complete_file> [license_request_file]'
  );
  process.exit(1);
}

const sslVerify = parseInt(process.argv[2]);
const buildType = process.argv[3];
const artifactFile = process.argv[4];
const varsFile = process.argv[5];
const completeFile = process.argv[6];
const requestFile = process.argv[7];

console.log(`License complete ${requestFile} ${completeFile}`);

if (requestFile == null) {
  console.warn('No license request file');
  // Create complete file then exit
  fs.writeFileSync(completeFile, `${new Date().valueOf()}`, {
    flag: 'w',
  });
  process.exit(0);
}

fs.accessSync(requestFile, fs.constants.R_OK);
fs.accessSync(varsFile, fs.constants.R_OK);

async function getArtifactSha256Hash() {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const input = fs.createReadStream(artifactFile);
    input.on('error', reject);
    input.on('data', (chunk) => hash.update(chunk));
    input.on('end', () => {
      resolve(hash.digest('hex'));
    });
  });
}

async function getLicenseId() {
  const data = (await readFile(varsFile)).toString('UTF-8');
  const m = /^#define\s+LICENSE_ID\s+"([^"]+)"$/m.exec(data);
  if (m == null || m[1] == null || m[1] == '') {
    throw new Error(`Failed to fetch LICENSE_ID from ${varsFile}`);
  }
  return m[1];
}

async function getLicenseData() {
  const json = JSON.parse(await readFile(requestFile));
  if (json.build_template) {
    return {};
  }
  if (typeof json.accessToken !== 'string' || typeof json.server !== 'string') {
    throw new Error(`Invalid license request file ${requestFile}`);
  }
  return { ...json, id: await getLicenseId() };
}

async function run() {
  const { id, accessToken, server } = await getLicenseData();
  if (!id) {
    return;
  }
  const hash = await getArtifactSha256Hash();
  const resp = await new Promise((resolve, reject) => {
    const buf = [];
    const req = https.request(
      `${server}/registration/complete`,
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
          reject(`Received invalid status code ${r.statusCode}`);
          return;
        }
        r.on('data', (data) => buf.push(data));
        r.on('end', () => resolve(Buffer.from(buf.join(), 'base64')));
      }
    );
    req.on('error', reject);
    req.end(
      JSON.stringify({
        id,
        hash,
      })
    );
  });
  if (resp.length < 1 || resp.length > 1022) {
    throw new Error(`Invalid registration/complete response received`);
  }
  const blob = Buffer.alloc(1024);
  blob.writeInt16LE(resp.length);
  resp.copy(blob, 2);

  let { size: offset } = await fileStat(artifactFile);

  const fd = fs.openSync(artifactFile, 'r+');
  try {
    fs.writeSync(fd, blob, 0, blob.length, offset);
  } finally {
    fs.closeSync(fd);
  }
  // Commit status of operation
  fs.writeFileSync(completeFile, `${new Date().valueOf()}`, {
    flag: 'w',
  });
}

run()
  .then(() => {
    console.log('License blob has been incorporated into binary artifact');
  })
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });