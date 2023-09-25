/*
Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.
Licensed under the WiPhone Public License v.1.0 (the "License"); you
may not use this file except in compliance with the License. You may
obtain a copy of the License at
https://wiphone.io/WiPhone_Public_License_v1.0.txt.

Unless required by applicable law or agreed to in writing, software,
hardware or documentation distributed under the License is distributed
on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

/*
 * Audio.h
 *
 *  Class to handle I2S peripheral of ESP32, hardware audio codec, amplifier IC, microphone
 *  data, audio encoding/decoding, audio RTP streams, etc.
*/

// TODO:
// - migrate to dr_mp3
// - add WAV capability (dr_wav)

#ifndef __AUDIO_H_
#define __AUDIO_H_

#include "Arduino.h"
#include "FS.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "driver/i2s.h"
#include "config.h"
#include "Hardware.h"
#include "Networks.h"
#include "helpers.h"
#include "RTPacket.h"

#define AUDIO_INLINE inline __attribute__((always_inline))

#define DR_WAV_NO_CONVERSION_API
#define DR_WAV_NO_STDIO
#include "src/audio/dr_wav.h"

// These are used in WiPhone.ino
#include "src/audio/g722_encoder.h"
#include "src/audio/g722_decoder.h"
#include "src/audio/g711.h"

extern AUDIO_CODEC_CLASS  codec;

#define LOUDSPEAKER 1
#define EARSPEAKER 0

#define STP_SILENT_PERIOD 60000  // to detect rtp silent
extern uint8_t    rtpSilentPeriod;  // for detection of other party rtp stream silent
#define RTP_SILENT_ON     0x02
#define RTP_SILENT_OFF    0x00
/* Description
 *     used for profiling the audio loop
 */
struct CycleInfo {
  uint32_t time[7];
  uint32_t samples[2];

  CycleInfo() {
    memset(time, 0, sizeof(time));
    memset(samples, 0, sizeof(samples));
  }

  void show() {
    char buf[100];
    char* p = buf;
    int last = 0;
    for (int i=1; i<sizeof(time)/sizeof(time[0]); i++) {
      if (time[i]!=0) {
        p += sprintf(p, "%d ", time[i]-time[last]);
        last = i;
      } else {
        p += sprintf(p, "- ");
      }
    }
    p += sprintf(p, "/ ");
    for (int i=0; i<sizeof(samples)/sizeof(samples[0]); i++) {
      p += sprintf(p, "%d ", samples[i]);
    }
    log_d("%s", buf);
  }
};
typedef struct CycleInfo CycleInfo_t;


class Audio  {

public:
  Audio(bool stereoOut, int BCLK, int LRC, int DOUT, int DIN);
  ~Audio();

  // Configuring
  void configureI2S();
  bool setSampleRate(int hz);         // TODO: which or these purely configuring, and which reset the configuration?
  bool setBitsPerSample(int bits);
  void setMonoOutput(bool mono);      // TODO: force Mono and not force mono

  // Actions
  void loop();
  void ceasePlayback();
  void report();
  bool start();
  void pause();
  void resume();
  bool shutdown();
  void setVolumes(int8_t speakerVol, int8_t headphonesVol, int8_t loudspeakerVol);
  void getVolumes(int8_t &speakerVol, int8_t &headphonesVol, int8_t &loudspeakerVol);
  void setHeadphones(bool plugged);
  bool getHeadphones(void);
  void chooseSpeaker(bool loudspeaker);
  bool isLoudspeaker() {
    return this->loudspeaker;
  }
  bool error() {
    return this->err != WM8750_ERROR_OK;
  }

  bool playFile(fs::FS *fs, const char* path);
  bool playRecord();
  bool playRingtone(fs::FS *fs);
  bool rewind() {
    return this->playFile(this->playbackFS, this->playbackFilename.c_str());
  }

  // Actions related to RTP
  void newCall();
  void showAudioStats();

  enum : uint8_t {
    ULAW_RTP_PAYLOAD = 0,         // G.711, u-Law / PCMU
    ALAW_RTP_PAYLOAD = 8,         // G.711, A-Law / PCMA
    G722_RTP_PAYLOAD = 9          // G.722
  };
  uint16_t openRtpConnection(uint16_t rtpLocalPort);                         // the port that will be listened to AND from which RTP will be sent TODO: allows these two to be different
  bool playRtpStream(uint8_t payloadType, uint16_t rtpRemotePort = 0);       // remote port - play audio only from that port

  // Actions related to microphone
  // TODO: first open port, than feed that port to TinySIP for SDP
  // TODO: currently the mic configuration is the same as the playback configuration, which might be not desirable (at 48 kHz sample rate, especially)
  bool turnMicOn();      // turn on mic, calculate average intensity, but otherwise don't do anything with the data     TODO: check whether it needs to be called before start() and whether it's used properly
  bool sendRtpStreamFromMic(uint8_t payloadType, IPAddress rtpRemoteIP, uint16_t rtpRemotePort);
  bool recordFromMic();
  bool isRecordingFinished() {
    return this->recordFinished;
  }
  bool saveWavRecord(fs::FS *fs, const char* pathName);
  void ceaseRecording();
  void setMicAvg(uint32_t mic);
  uint32_t getMicAvg();

  // TODO
  void preserve();        // remember current configs to restore playback later
  void restore();         // restore preserved state

  // Properties
  const char* getTitle() {
    return this->title.length()>0 ? this->title.c_str() : this->playbackBasename.length() ? this->playbackBasename.c_str() : "";
  }
  const char* getArtist() {
    return this->artist.length()>0 ? this->artist.c_str() : "";
  }

  uint32_t getFileSize();
  uint32_t getFilePos();
  bool isOn() {
    return this->audioOn;
  }
  bool isEof() {
    return this->playbackEof;
  }
  int  getBps() {
    return this->bps;
  };
  int packetSizeSamples(int duration);

  static const i2s_port_t i2s_num = I2S_NUM_0;

  // Volume range in the audio codec chip
  static const int8_t MaxVolume = 6;
  static const int8_t MuteVolume = -69;

  // Software limit for the loudspeaker (otherwise can burn)
  static const int8_t MaxLoudspeakerVolume = 0;

  // Profiling
  //LinearArray<CycleInfo_t, false> profile;

  bool playSampleChunk();

protected:
  bool turnOn();                            // enable the audio systems and main loop if not enabled already
  bool playFile();
  bool setDataChannels(int channels);
  bool setFilePos(uint32_t pos);
  bool playChunk();
  AUDIO_INLINE bool playSample();
  void codecReconfig();

  // Specific to MP3
  void readID3Metadata();
  int  decodeMp3Bytes(uint8_t *data, size_t len);

protected:

  // What to play in DAC (speaker & headphones)?
  enum class Playback { Nothing, RtpStream, LocalMp3, Record, LocalPcm };

  bool        audioOn = false;              // I2S and audio codec are turned ON
  bool        audioLoop = true;             // do the audio processing if audio is ON?
  bool        microphoneOn = false;         // TODO: configure I2S and audio codec based on this value (currently microphone is ON whenever audio is ON)
  Playback    playback;                     // what are we currently feeding to DAC?
  bool        microphoneStreamOut;          // do we send microphone data in RTP stream?
  bool        microphoneRecord;             // do we record microphone data to a local file?
  int16_t     sample[2];
  bool        headphones = false;           // if headphones are plugged in, need to send output only to headphones (not earspeaker and/or loudspeaker)
  bool        loudspeaker = false;           // which speaker to use: loudspeaker (true) or earspeaker (false)?
  int8_t      earpieceVol = 6;            // small speaker connected directly to the audio codec IC
  int8_t      headphonesVol = 6;
  int8_t      loudspeakerVol = 0;         // big speaker connected to the amplifier

  int         sampleRate;                   // how many samples per second
  uint8_t     bps = 16;                     // bitsPerSample
  uint8_t     dataChannels = 2;             // number of channels in the MP3 file; used by playChunk
  bool        monoOut = false;              // does I2S driver expect one (left only) or two channels (right and left)?

  // Local playback file
  fs::FS*     playbackFS;                   // filesystem
  String      playbackFilename="";          // full path of the playback file in the filesystem
  String      playbackBasename="";          // basename (shor filename)
  File        playbackFile;                 // MP3 file
  bool        playbackEof = false;

  String      artist;
  String      title;

  // Record buffer (PCM)
  uint16_t*   recordRaw = NULL;             // temporary buffer in PSRAM where the audio data is being stored
  size_t      recordRawSizeSamples;
  int         recordRawR;
  int         recordRawW;
  bool        recordFinished;

  // Play buffers: encoded and decoded (PCM)
  uint8_t     playEnc[1600];                // undecoded audio (MP3) / receiving buffer for UDP packets
  uint16_t    playEncR=0;                   // read index
  uint16_t    playEncW=0;                   // write index

  int16_t     playDec[2400];                // decoded audio (PCM): 1-channel: mono (max. 2400 samples);  2-channel: interleaved L/R (maximum 1152 frames, 2*1200 = 2400 samples)
  // NOTE: this is sufficient for 150 ms of 16000 Hz mono audio (e.g. decoded G.722)
  uint16_t    playDecFramesLeft = 0;
  uint16_t    playDecCurFrame;
  bool        playDecEvenSample = 1;        // if true, sample is swapped with the next in mono playback

  // Mic buffers: raw (PCM) and encoded
  uint16_t    micRaw[2049];
  uint16_t    micRawW;
  uint16_t    micRawR;                      // micRawR < micRawW, if equal -> empty
  uint8_t     micEnc[1600];

  uint32_t    micAvg[4];
  uint16_t    micAvgNext = 0;

  bool        calcMicIntensity;             // Do we need to calculate microphone average input?

  // Specific to MP3
  int         id3Size=0;                    // length id3 tag
  int         nextSync=0;
  int         bytesLeft=0;
  int         bitrate=0;                    // TODO: what is this?
  uint8_t     rev=0;                        // revision
  bool        f_podcast = false;            // set if found ID3Header in stream
  bool        f_extHead = false;            // ID3 extended header
  bool        f_mp3 = false;                // indicates mp3
  bool        mp3Playing = false;           // valid mp3 stream recognized
  uint32_t    lastRate;                     // TODO: what is this?

  // Incoming RTP audio stream
  WiFiUDP     rtp;
  IPAddress   rtpRemoteIP;
  uint16_t    rtpRemotePort = 0;
  uint8_t     rtpPayloadType;
  RTPacket    rtpSend;                      // this one is initialized with parameters from
  RTPacket    rtpRecv;
  bool        firstPacket;                  // is the next incoming packet will the first in audio stream?
  uint16_t    lastSequenceNum;              // last RTP sequence num
  //uint32_t    pos;                          // position in playback     TODO
  uint16_t    rtpPort;
  uint16_t    rtcpPort;
  uint16_t    voipPacketSize;

  // Call quality of service (QoS)
  uint32_t    rtcpPacketsReceived;
  uint32_t    packetsReceived;              // total UDP packets received during all
  uint32_t    packetsGood;                  // audio packets count that have no issues
  uint32_t    packetsWrongPayload;          // audio format does not match negotiated one
  uint32_t    packetsMissed;                // packets not played (either completely missing or out of order)
  uint32_t    packetsUnord;                 // packets out of order arriving now (temporary)

  uint32_t    packetsSent;                  // total UDP packets attempted to send
  uint32_t    packetsSendingFailed;         // total packets failed to send

  // Codecs
  G722_DEC_CTX* g722Decoder;
  G722_ENC_CTX* g722Encoder;

  // Debug
  uint32_t    loopCnt = 0;
  uint32_t    runCnt = 0;
  uint32_t    rtpCnt = 0;
  int         sampleX = 0;

  static const uint16_t audio_sample[];
  static const uint16_t VOIP_PACKET_DURATION_MS = 20;     // maximum anticipated packet duration (we always leave this length in the output buffer in anticipation of such packet)
  static const uint32_t PACKET_PCM_WSIZE_8KHZ = 160;      // number of samples for 20ms PCM 16-bit/8kHz, 1-chanel
  static const uint32_t PACKET_PCM_WSIZE_16KHZ = 320;     // number of samples for 20ms PCM 16-bit/16kHz, 1-chanel
  static const uint32_t RECORDING_SIZE_SAMPLES = 1<<20;   // 1 MB

  // Power masks
  static const uint16_t POWER_ALL = 0;
  static const uint16_t DAC_HEADPHONES  = WM8750_POWER2_DAC | WM8750_POWER2_OUT1;
  static const uint16_t DAC_EARSPEAKER  = WM8750_POWER2_DAC | WM8750_POWER2_OUT3 | WM8750_POWER2_LOUT1;
  static const uint16_t DAC_LOUDSPEAKER = WM8750_POWER2_DAC | WM8750_POWER2_OUT2;

  enum : int { APLL_AUTO = -1, APLL_ENABLE = 1, APLL_DISABLE = 0 };

  wm8750_err_t err;

  float m_amplitude;
  float m_frequency;
  float m_phase;
  float m_time;
  float m_deltaTime;
};

#endif /* __AUDIO_H_ */
