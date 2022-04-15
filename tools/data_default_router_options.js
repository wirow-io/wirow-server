const data = {
  "codecs": [
    {
      "kind": "audio",
      "mimeType": "audio/opus",
      "clockRate": 48000,
      "channels": 2,
      "parameters": {
        "minptime": 10,
        "usedtx": 1,
        "useinbandfec": 1
      }
    },
    {
      "kind": "video",
      "mimeType": "video/VP8",
      "clockRate": 90000,
      "parameters": {
        "x-google-start-bitrate": 1000
      }
    },
    {
      "kind": "video",
      "mimeType": "video/h264",
      "clockRate": 90000,
      "parameters": {
        "packetization-mode": 1,
        "profile-level-id": "4d0032",
        "level-asymmetry-allowed": 1,
        "x-google-start-bitrate": 1000
      }
    },
    {
      "kind": "video",
      "mimeType": "video/h264",
      "clockRate": 90000,
      "parameters": {
        "packetization-mode": 1,
        "profile-level-id": "42e01f",
        "level-asymmetry-allowed": 1,
        "x-google-start-bitrate": 1000
      }
    }
  ]
};

process.stdout.write(JSON.stringify(data, null, 2));
process.stdout.write('\n');