
// mid: 'AUDIO',
// codecs:
//   [
//     {
//       mimeType: 'audio/opus',
//       payloadType: 111,
//       clockRate: 48000,
//       channels: 2,
//       parameters:
//       {
//         useinbandfec: 1,
//         usedtx: 1,
//         foo: 222.222,
//         bar: '333'
//       }
//     }
//   ],
// headerExtensions:
//   [
//     {
//       uri: 'urn:ietf:params:rtp-hdrext:sdes:mid',
//       id: 10
//     },
//     {
//       uri: 'urn:ietf:params:rtp-hdrext:ssrc-audio-level',
//       id: 12
//     }
//   ],
// encodings: [{ ssrc: 11111111 }],
// rtcp:
// {
//   cname: 'FOOBAR'
// }


static char _data_producer_audio_rtp_params[] = {
  0x7b, 0x0a, 0x20, 0x20, 0x22, 0x6d, 0x69, 0x64, 0x22, 0x3a, 0x20, 0x22,
  0x41, 0x55, 0x44, 0x49, 0x4f, 0x22, 0x2c, 0x0a, 0x20, 0x20, 0x22, 0x63,
  0x6f, 0x64, 0x65, 0x63, 0x73, 0x22, 0x3a, 0x20, 0x5b, 0x0a, 0x20, 0x20,
  0x20, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x6d,
  0x69, 0x6d, 0x65, 0x54, 0x79, 0x70, 0x65, 0x22, 0x3a, 0x20, 0x22, 0x61,
  0x75, 0x64, 0x69, 0x6f, 0x2f, 0x6f, 0x70, 0x75, 0x73, 0x22, 0x2c, 0x0a,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x70, 0x61, 0x79, 0x6c, 0x6f,
  0x61, 0x64, 0x54, 0x79, 0x70, 0x65, 0x22, 0x3a, 0x20, 0x31, 0x31, 0x31,
  0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x63, 0x6c, 0x6f,
  0x63, 0x6b, 0x52, 0x61, 0x74, 0x65, 0x22, 0x3a, 0x20, 0x34, 0x38, 0x30,
  0x30, 0x30, 0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x63,
  0x68, 0x61, 0x6e, 0x6e, 0x65, 0x6c, 0x73, 0x22, 0x3a, 0x20, 0x32, 0x2c,
  0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x70, 0x61, 0x72, 0x61,
  0x6d, 0x65, 0x74, 0x65, 0x72, 0x73, 0x22, 0x3a, 0x20, 0x7b, 0x0a, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x75, 0x73, 0x65, 0x69,
  0x6e, 0x62, 0x61, 0x6e, 0x64, 0x66, 0x65, 0x63, 0x22, 0x3a, 0x20, 0x31,
  0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x75,
  0x73, 0x65, 0x64, 0x74, 0x78, 0x22, 0x3a, 0x20, 0x31, 0x2c, 0x0a, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x66, 0x6f, 0x6f, 0x22,
  0x3a, 0x20, 0x32, 0x32, 0x32, 0x2e, 0x32, 0x32, 0x32, 0x2c, 0x0a, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x62, 0x61, 0x72, 0x22,
  0x3a, 0x20, 0x22, 0x33, 0x33, 0x33, 0x22, 0x0a, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x7d, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x7d, 0x0a, 0x20, 0x20,
  0x5d, 0x2c, 0x0a, 0x20, 0x20, 0x22, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72,
  0x45, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x73, 0x22, 0x3a,
  0x20, 0x5b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x22, 0x75, 0x72, 0x69, 0x22, 0x3a, 0x20, 0x22, 0x75,
  0x72, 0x6e, 0x3a, 0x69, 0x65, 0x74, 0x66, 0x3a, 0x70, 0x61, 0x72, 0x61,
  0x6d, 0x73, 0x3a, 0x72, 0x74, 0x70, 0x2d, 0x68, 0x64, 0x72, 0x65, 0x78,
  0x74, 0x3a, 0x73, 0x64, 0x65, 0x73, 0x3a, 0x6d, 0x69, 0x64, 0x22, 0x2c,
  0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x69, 0x64, 0x22, 0x3a,
  0x20, 0x31, 0x30, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x7d, 0x2c, 0x0a, 0x20,
  0x20, 0x20, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22,
  0x75, 0x72, 0x69, 0x22, 0x3a, 0x20, 0x22, 0x75, 0x72, 0x6e, 0x3a, 0x69,
  0x65, 0x74, 0x66, 0x3a, 0x70, 0x61, 0x72, 0x61, 0x6d, 0x73, 0x3a, 0x72,
  0x74, 0x70, 0x2d, 0x68, 0x64, 0x72, 0x65, 0x78, 0x74, 0x3a, 0x73, 0x73,
  0x72, 0x63, 0x2d, 0x61, 0x75, 0x64, 0x69, 0x6f, 0x2d, 0x6c, 0x65, 0x76,
  0x65, 0x6c, 0x22, 0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22,
  0x69, 0x64, 0x22, 0x3a, 0x20, 0x31, 0x32, 0x0a, 0x20, 0x20, 0x20, 0x20,
  0x7d, 0x0a, 0x20, 0x20, 0x5d, 0x2c, 0x0a, 0x20, 0x20, 0x22, 0x65, 0x6e,
  0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x73, 0x22, 0x3a, 0x20, 0x5b, 0x0a,
  0x20, 0x20, 0x20, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x22, 0x73, 0x73, 0x72, 0x63, 0x22, 0x3a, 0x20, 0x31, 0x31, 0x31, 0x31,
  0x31, 0x31, 0x31, 0x31, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x7d, 0x0a, 0x20,
  0x20, 0x5d, 0x2c, 0x0a, 0x20, 0x20, 0x22, 0x72, 0x74, 0x63, 0x70, 0x22,
  0x3a, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x22, 0x63, 0x6e, 0x61,
  0x6d, 0x65, 0x22, 0x3a, 0x20, 0x22, 0x46, 0x4f, 0x4f, 0x42, 0x41, 0x52,
  0x22, 0x0a, 0x20, 0x20, 0x7d, 0x0a, 0x7d, 0x0a, 0x00
};

//        id  : 13
//      }
//    ],
//    encodings :
//    [
//      { ssrc: 22222222, rtx: { ssrc: 22222223 } },
//      { ssrc: 22222224, rtx: { ssrc: 22222225 } },
//      { ssrc: 22222226, rtx: { ssrc: 22222227 } },
//      { ssrc: 22222228, rtx: { ssrc: 22222229 } }
//    ],
//    rtcp :
//    {
//      cname : 'FOOBAR'
//    }
