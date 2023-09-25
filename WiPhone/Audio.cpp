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
 *
 *  MP3 decoding logic borrowed from Wolle (schreibfaul1).
 *  Source: https://github.com/schreibfaul1/ESP32-audioI2S
 *  It was later licenced under GPL-3.0.
 */

// TODO:
// - use i2s_write for entire batches instead of "playSample()", introduce an additional interleaving output buffer for that
// - force mono (for enforced mono in MP3 player), otherwise - allow monoOut to be set according to dataChannels

#include "Audio.h"
#include "config.h"

//#define TAG "audio" // log tags don't seem to work in Arduino

uint8_t    rtpSilentPeriod = 0x0;
uint32_t   rtpSilentScan = 0x0;

AUDIO_CODEC_CLASS  codec(AUDIO_CODEC_I2C_ADDR, I2C_SDA_PIN, I2C_SCK_PIN);

const uint16_t Audio::audio_sample[] = {
  // change every 32 bytes (500 Hz sound for 16000 Hz mono)
  //0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
  // change every 16 bytes (1000 Hz sound for 16000 Hz mono), means period of 16 samples, 16000 sample rate / 16 = 1000 KHz
  // 128 samples here
  0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
  //0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x80FF, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00, 0x7F00,
  // change every 8 bytes (2000 Hz sound for 16000 Hz mono)
  //0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0x0101, 0x0101,
  // change every 4 bytes (4000 Hz sound for 16000 Hz mono)
  //0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101, 0xFEFE, 0xFEFE, 0x0101, 0x0101,
};

Audio::Audio(bool stereoOut, int BCLK, int LRC, int DOUT, int DIN) : playbackFS(&SPIFFS) {

  log_d("Audio::Audio: %d", ESP.getFreeHeap());
  // Initialize variables
  this->audioOn = false;
  this->audioLoop = false;
  this->playback = Playback::Nothing;
  this->microphoneStreamOut = false;
  this->microphoneRecord = false;

  // Configure I2S interface
  this->bps = 16;
  this->sampleRate = 16000;
  this->monoOut = !stereoOut;
  this->dataChannels = this->monoOut ? 1 : 2;     // provisional
  this->voipPacketSize = this->packetSizeSamples(VOIP_PACKET_DURATION_MS);
  log_d("Audio::Audio: voip %d", ESP.getFreeHeap());
  this->configureI2S();

  log_d("Audio::Audio: i2s %d", ESP.getFreeHeap());

  // Set pinout
  i2s_pin_config_t pins = {
    .bck_io_num   = BCLK,
    .ws_io_num    = LRC,              //  wclk,
    .data_out_num = DOUT,
    .data_in_num  = DIN
  };

  i2s_set_pin((i2s_port_t) i2s_num, &pins);

  log_d("Audio::Audio: pins %d", ESP.getFreeHeap());

  // Initialize audio codec
  err = codec.powerUp(stereoOut, 32000, POWER_ALL, AUDIO_MCLK_CRYSTAL_KHZ);
  codec.shutDown();

  log_d("Audio::Audio: codec %d", ESP.getFreeHeap());

  // Populate sequence ID, SSRC and timestamp
  rtpSend.newSession(true);

  log_d("Audio::Audio: rtp %d", ESP.getFreeHeap());

  // G.722 decoder & encoder
  g722Decoder = g722_decoder_new(64000, 0);       // TODO: check if it doesn't take a lot of memory (otherwise initialize only when needed)
  g722Encoder = g722_encoder_new(64000, 0);

  log_d("Audio::Audio: end %d", ESP.getFreeHeap());
}

/* Description:
 *     configures I2S according to the internal values:
 *       - this->bps
 *       - this->sampleRate
 *       - this->monoOut
 */
void Audio::configureI2S() {
  // TODO: does it create pop noise? if so - reduce number of calls
  //i2s configuration
  i2s_driver_uninstall(i2s_num);
  i2s_config_t i2s_config = {
    .mode = static_cast<i2s_mode_t> (I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX ),
    .sample_rate = this->sampleRate,
    .bits_per_sample = (this->bps == 16 ? I2S_BITS_PER_SAMPLE_16BIT : I2S_BITS_PER_SAMPLE_8BIT ),
    .channel_format = (this->monoOut ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_RIGHT_LEFT),
    .communication_format = static_cast<i2s_comm_format_t> (I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll=APLL_ENABLE,
    .tx_desc_auto_clear=true,  // new in V1.0.1
    .fixed_mclk=-1
  };
  log_d("Audio::Audio: before driver install %d", ESP.getFreeHeap());
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  log_d("Audio::Audio: after driver install %d", ESP.getFreeHeap());
  this->report();
}

void Audio::report() {
  log_d("Audio configs:");
  log_d(" - SR:   %d", this->sampleRate);
  log_d(" - bps:  %d", this->bps);
  log_d(" - ch:   %d", this->dataChannels);
  log_d(" - mono: %d", (int) this->monoOut);
  log_d(" - headphones: %d", (int) this->headphones);
  log_d(" - speaker: %d", (int) this->loudspeaker);
}

bool Audio::start() {
  bool succ = true;

  // Turn on the audio codec IC
  log_v("turning ON audio codec");
  // TODO: feed result into succ
  uint16_t powerMask = this->headphones ? DAC_HEADPHONES : (this->loudspeaker ? DAC_LOUDSPEAKER : DAC_EARSPEAKER);
  codec.powerUp(!this->monoOut, 32000, powerMask, AUDIO_MCLK_CRYSTAL_KHZ);
  codec.setVolume(MuteVolume, MuteVolume);     // mute: avoid sudden pop

  // Turn on amplifier (separate IC) if needed
#ifdef WIPHONE_INTEGRATED
  if (!this->headphones && this->loudspeaker) {
    log_v("turning ON amplifier");
    amplifierEnable(4);
  }
  bool amped = true;
#endif

  // Turn on I2S peripheral
  log_v("turning ON I2S");
  if (i2s_start(i2s_num)!=ESP_OK) {
    succ = false;
  }

  // Turn on the volume
  codec.setVolume(this->loudspeakerVol, this->headphones ? this->headphonesVol : this->earpieceVol);

  this->audioOn = succ;

  return succ && amped;
}

void Audio::setHeadphones(bool plugged) {
  if (this->headphones != plugged) {
    this->headphones = plugged;
    if (this->audioOn) {
      this->codecReconfig();
    }
  }
}

bool Audio::getHeadphones(void) {
  return this->headphones;
}

void Audio::chooseSpeaker(bool loudspeaker) {
  if (this->loudspeaker != loudspeaker) {
    this->loudspeaker = loudspeaker;
    if (this->audioOn) {
      this->codecReconfig();
    }
  }
}

void Audio::codecReconfig() {
  // Turn off the audio codec IC
  log_v("turning audio codec OFF");
  codec.mute();         // to minimize pop noise
  codec.shutDown();     // TODO: feed the result into succ

  log_v("turning audio codec ON");
  uint16_t powerMask = this->headphones ? DAC_HEADPHONES : (this->loudspeaker ? DAC_LOUDSPEAKER : DAC_EARSPEAKER);
  codec.powerUp(!this->monoOut, 32000, powerMask, AUDIO_MCLK_CRYSTAL_KHZ);
  codec.setVolume(MuteVolume, MuteVolume);     // mute: avoid sudden pop

  // Switch amplifier (separate IC) if needed
#ifdef WIPHONE_INTEGRATED
  if (!this->headphones && this->loudspeaker) {
    log_v("turning amplifier ON");
    amplifierEnable(4);
  } else {
    log_v("turning amplifier OFF");
    amplifierEnable(0);
  }
#endif

  // Turn on the volume
  codec.setVolume(this->loudspeakerVol, this->headphones ? this->headphonesVol : this->earpieceVol);
}

void Audio::pause() {
  // Stop processing audio buffers (the main audio loop)
  this->audioLoop = false;

  // Clear only immediate audio playback (DMA) buffer to stop the sound
  i2s_zero_dma_buffer((i2s_port_t)i2s_num);
}

void Audio::resume() {
  this->audioLoop = true;
}

bool Audio::shutdown() {
  bool succ = true;

  // Turn off the audio codec IC
  codec.mute();         // to minimize pop noise
  codec.shutDown();     // TODO: feed the result into succ

  // Clear the buffers, close the file
  this->ceaseRecording();
  this->ceasePlayback();

  // Tun off the amp
  //if (!allDigitalWrite(AMPLIFIER_SHUTDOWN, LOW)) succ = false;
  amplifierEnable(0);

  // Turn off I2S peripheral
  if (i2s_stop(i2s_num)!=ESP_OK) {
    succ = false;
  }

  this->audioOn = false;

  return succ;
}

Audio::~Audio() {
  this->shutdown();
}

bool Audio::turnOn() {
  // Start audio systems
  if (!this->audioOn && !this->start()) {
    return false;
  }

  // Let the audio processing run
  this->audioLoop = true;

  return true;
}

void Audio::setVolumes(int8_t earpieceVol, int8_t headphonesVol, int8_t loudspeakerVol) {
  if (earpieceVol > MaxVolume) {
    earpieceVol = MaxVolume;
  }
  if (earpieceVol < MuteVolume) {
    earpieceVol = MuteVolume;
  }
  if (headphonesVol > MaxVolume) {
    headphonesVol = MaxVolume;
  }
  if (headphonesVol < MuteVolume) {
    headphonesVol = MuteVolume;
  }
  if (loudspeakerVol > MaxLoudspeakerVolume) {
    loudspeakerVol = MaxLoudspeakerVolume;
  }
  if (loudspeakerVol < MuteVolume) {
    loudspeakerVol = MuteVolume;
  }
  this->earpieceVol = earpieceVol;
  this->headphonesVol = headphonesVol;
  this->loudspeakerVol = loudspeakerVol;
  codec.setVolume(loudspeakerVol, this->headphones ? headphonesVol : earpieceVol);
}

void Audio::getVolumes(int8_t &speakerVol, int8_t &headphonesVol, int8_t &loudspeakerVol) {
  speakerVol = this->earpieceVol;
  headphonesVol = this->headphonesVol;
  loudspeakerVol = this->loudspeakerVol;
}

bool Audio::playFile(fs::FS *fs, const char* path) {
  this->ceasePlayback();
  this->playbackFS = fs;
  this->title = "";
  this->artist = "";
  this->playbackFilename = path;
  if(!this->playbackFilename.startsWith("/")) {
    this->playbackFilename="/"+this->playbackFilename;
  }
  this->playbackBasename = this->playbackFilename.substring(this->playbackFilename.lastIndexOf('/') + 1, this->playbackFilename.length());
  return this->playFile();
}

bool Audio::playRecord() {
  if (this->recordRawW == 0) {
    return false;
  }
  this->ceasePlayback();
  this->playback = Playback::Record;
  this->recordRawR = 0;
  this->setDataChannels(1);
  return true;
}

bool Audio::playFile() {
  log_d("Reading file: %s", this->playbackFilename.c_str());
  this->playbackFile = this->playbackFS->open(this->playbackFilename.c_str());
  if (!this->playbackFile) {
    log_d("Failed to open file for reading");
    return false;
  }

  // Start the audio systems (if not started)
  if (!this->turnOn()) {
    return false;
  }

  uint16_t i=0, s=0;

  // Reset buffers
  this->playEncW=0;
  this->playEncR=0;
  this->playDecFramesLeft = 0;
  memset(this->playDec, 0, sizeof(this->playDec));      // not necessary

  this->playback = Playback::LocalPcm;
  this->playbackEof = false;

  return true;
}

void Audio::ceasePlayback() {
  if (this->playback == Playback::LocalMp3) {
    playbackFile.close();
  }
  this->playback = Playback::Nothing;
  i2s_zero_dma_buffer((i2s_port_t)i2s_num);
  memset(this->playDec, 0, sizeof(this->playDec));
  this->playDecFramesLeft = 0;
  this->playEncW = 0;
}

/* Description:
 *     push out decoded samples from `playDec` into I2S DMA buffer
 * Return:
 *     false if could not push out a sample at some point
 */
bool Audio::playChunk() {
//    // Play at most 20ms worth of samples here
//    uint32_t packet = this->sampleRate / 50;
//    uint32_t cutoff = this->playDecFramesLeft > packet ? this->playDecFramesLeft - packet : 0;
  const uint32_t cutoff = 0;
  if (this->monoOut) {
    if (this->dataChannels==1) {
      // Should have been simple case: direct copying, but swapping the neighboring samples (ESP32 bug workaround)
      while (this->playDecFramesLeft > cutoff) {
        if (this->playDecEvenSample) {
          if (this->playDecFramesLeft > 1) {
            this->sample[0] = this->playDec[this->playDecCurFrame + 1];
          } else {
            this->sample[0] = this->playDec[this->playDecCurFrame];
          }
        } else {
          if (this->playDecCurFrame > 0) {
            this->sample[0] = this->playDec[this->playDecCurFrame - 1];
          } else {
            this->sample[0] = this->playDec[this->playDecCurFrame];
          }
        }
        if (!this->playSample()) {
          return false;
        }
        this->playDecFramesLeft--;
        this->playDecCurFrame++;
        this->playDecEvenSample = !this->playDecEvenSample;
      }
    } else if (this->dataChannels==2) {
      // Complex case: average of two channels (TODO: is this a correct way to mux two channels?)
      while (this->playDecFramesLeft > cutoff) {
        this->sample[0] = (this->playDec[this->playDecCurFrame * 2] >> 1) + (this->playDec[this->playDecCurFrame * 2 + 1] >> 1);
        if (!this->playSample()) {
          return false;
        }
        this->playDecFramesLeft--;
        this->playDecCurFrame++;
      }
    }
  } else {
    if (this->dataChannels==1) {
      // Complex case: duplication of a single channel (inefficient and should never happen - if there is only one channel, need to switch to mono output)
      while (this->playDecFramesLeft > cutoff) {
        this->sample[0] = this->sample[1] = this->playDec[this->playDecCurFrame];
        if (!this->playSample()) {
          return false;
        }
        this->playDecFramesLeft--;
        this->playDecCurFrame++;
      }
    } else if (this->dataChannels==2) {
      // Simple case: direct copying      TODO: maybe could use i2s_write directly
      while (this->playDecFramesLeft > cutoff) {
        this->sample[0] = this->playDec[this->playDecCurFrame * 2];
        this->sample[1] = this->playDec[this->playDecCurFrame * 2 + 1];
        if (!this->playSample()) {
          return false;
        }
        this->playDecFramesLeft--;
        this->playDecCurFrame++;
      }
    }
  }
  return true;
}

/* Description:
 *     same as playChunk, but plays sample
 *     (that is tries to fill output DMA buffer with audio_sample)
 */
bool Audio::playSampleChunk() {
  uint32_t i = (uint32_t) -1;
  uint32_t cnt = 0;
  if (this->monoOut) {
    if (this->dataChannels==1) {
      while (++cnt) {
        this->sample[0] = audio_sample[ i = (++i<sizeof(audio_sample)/sizeof(audio_sample[0]) ? i : 0) ];
        if (!this->playSample()) {
          goto ret;
        }
      }
    } else if (this->dataChannels==2) {
      while (++cnt) {
        // Simplification
        this->sample[0] = audio_sample[ i = (++i<sizeof(audio_sample)/sizeof(audio_sample[0]) ? i : 0) ];
        if (!this->playSample()) {
          goto ret;
        }
      }
    }
  } else {
    if (this->dataChannels==1) {
      while (++cnt) {
        this->sample[0] = this->sample[1] = audio_sample[ i = (++i<sizeof(audio_sample)/sizeof(audio_sample[0]) ? i : 0) ];
        if (!this->playSample()) {
          goto ret;
        }
      }
    } else if (this->dataChannels==2) {
      while (++cnt) {
        this->sample[0] = audio_sample[ i = (++i<sizeof(audio_sample)/sizeof(audio_sample[0]) ? i : 0) ];
        this->sample[1] = audio_sample[ i = (++i<sizeof(audio_sample)/sizeof(audio_sample[0]) ? i : 0) ];
        if (!this->playSample()) {
          goto ret;
        }
      }
    }
  }
ret:
  log_d("samples written: %u", --cnt);
  return cnt > 0;
}

bool Audio::playRingtone(fs::FS *fs) {
  this->ceasePlayback();
  this->playback = Playback::LocalPcm;
  this->setDataChannels(1);
  this->setBitsPerSample(16);
  this->setSampleRate(8000);
  this->setMonoOutput(true);

  if (!this->turnOn()) {
    return false;
  }
  return this->playFile(fs, "/ringtone.pcm");
}

/* Description:
 *     decode chunk of audio and play it
 */
void Audio::loop() {
//  this->loopCnt++;    // remove
//  if (this->loopCnt % 1000 == 0) {
//    log_d("%d / run=%d / rtp=%d", this->loopCnt, this->runCnt, this->rtpCnt);
//  }
  if (!this->audioLoop || !this->audioOn) {
    return;  // don't do anything if audio systems (I2S peripheral and audio codec) are turned off
  }
//  this->runCnt++;   // remove

  // PLAY PART: play readily available decoded data

//  // Profiling
//  CycleInfo_t info;
//  info.time[0] = micros();

  //uint32_t oldSmp = this->playDecFramesLeft;
  if (this->playDecFramesLeft>0) {
    this->playChunk();
  }
  //info.time[1] = micros();
  //info.samples[0] = oldSmp - this->playDecFramesLeft;

  // MICROPHONE PART: process/encode/send microphone data

  if (this->microphoneOn && this->bps==16) {      // TODO: different bits-per-sample are not supported for simplicity

    // Ensure the microphone data starts at the beginning
    if (this->micRawR > 0) {
      memmove(this->micRaw, this->micRaw + this->micRawR, sizeof(this->micRaw[0]) * (this->micRawW - this->micRawR));
      this->micRawW -= this->micRawR;
      this->micRawR = 0;
    }
    //info.time[2] = micros();

    // Read microphone data
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(i2s_num,  (char*) (this->micRaw + this->micRawW),  sizeof(this->micRaw) - this->micRawW * sizeof(this->micRaw[0]),  &bytesRead,  0);
    //info.time[3] = micros();
    if (err == ESP_OK) {

      // BPS == 16 is assumed below

      // Swap neighboring samples (ESP32 bug, see here: https://esp32.com/viewtopic.php?t=11023)
      uint16_t* start = this->micRaw + ((this->micRawW / 2) * 2);                       // first sample pair
      const uint16_t samplesRead = bytesRead/2;
      uint16_t* p = this->micRaw + (((this->micRawW + samplesRead) / 2) * 2);           // past last sample pair
      while (p > start) {
        p -= 2;
        uint16_t x = *(p+1);
        *(p+1) = *p;
        *p = x;
      }

      this->micRawW += bytesRead / 2;           // bps = 16 assumed

      size_t packetSizeWords = this->packetSizeSamples(20);       // 20 ms packet
      if (this->micRawW >= packetSizeWords) {
        // At least 20 ms of microphone data collected

        // Calculate microphone input intensity
        if (this->calcMicIntensity) {             // avoid doing it during the call to save a bit of compute power
          uint32_t micSum = 0;
          if (this->bps==16) {
            for (int j = 0; j < packetSizeWords; j++) {
              micSum += abs((int16_t) this->micRaw[j]);
            }
            this->setMicAvg(micSum / packetSizeWords);
          } else if (this->bps==8) {
            // ...should never happen (8-bit not fully implemented yet)
            for (int j = packetSizeWords*2; j > 0;) {
              uint32_t temp =  *((int8_t*)this->micRaw + --j); // temp is necessary due to some weirdness in the Arduino abs() implementation. See: https://www.arduino.cc/reference/en/language/functions/math/abs/
              temp = abs(temp);
              micSum += temp  << 8;
              //micSum += abs( *((int8_t*)this->micRaw + --j) ) << 8;

            }
            this->setMicAvg(micSum / packetSizeWords / 2);
          }
        }

        // Output the microphone data: send via network and/or save to a file

        if (this->microphoneStreamOut && rtpRemotePort) {

//          // DEBUG: replace all the microphone data with audio sample
//          this->micRawW -= bytesRead / 2;
//          for (int j=0; j<bytesRead / 2; j++) {
//            this->micRaw[this->micRawW++] = audio_sample[sampleX++];
//            if (sampleX >= sizeof(audio_sample)/sizeof(audio_sample[0])) sampleX = 0;
//          }

          // Compress PCM to G.722 (640 bytes to 160 bytes) or to G.711 (320 bytes to 160 bytes)
          int bytes = 0;
          if (rtpPayloadType == Audio::G722_RTP_PAYLOAD) {
            bytes = g722_encode(g722Encoder, (const int16_t*) this->micRaw, packetSizeWords, (uint8_t*) this->micEnc);
          } else if (rtpPayloadType == Audio::ALAW_RTP_PAYLOAD) {
            alaw_compress(packetSizeWords, (const int16_t*) this->micRaw, (uint8_t*) this->micEnc);
            bytes = packetSizeWords;
          } else if (rtpPayloadType == Audio::ULAW_RTP_PAYLOAD) {
            ulaw_compress(packetSizeWords, (const int16_t*) this->micRaw, (uint8_t*) this->micEnc);
            bytes = packetSizeWords;
          }

          if (bytes > 0) {
            // Create RTP packet

            RTPacketHeader *rtpHeader = rtpSend.generateHeader(bytes);

            // Send RTP packet
            rtp.beginPacket(rtpRemoteIP, rtpRemotePort);
            rtp.write((uint8_t*)rtpHeader, sizeof(RTPacketHeader));
            rtp.write(this->micEnc, bytes);         // TODO: this unnecesarily (and rather slowly) copies the buffer

            // TODO: leave 12 bytes in the head of micEnc free for the RTP header, implement and use udp.writeFast()
            if (!rtp.endPacket()) {
              this->packetsSendingFailed++;
            }
            this->packetsSent++;

          } else {
            log_d("enc fail");
          }

        }

        if (this->microphoneRecord && !this->recordFinished) {
//          // DEBUG: drop every other sample and send that way
//          uint16_t dummy[packetSizeWords/2];
//          for (int j=packetSizeWords/2; j>0;) {
//            j-=2;
//            dummy[j] = this->micRaw[j*2];
//          }
//          if (this->recordFile) {
//            this->recordFile.write((const uint8_t*) this->micRaw, packetSizeWords * 2);
//            log_d("w %d", packetSizeWords * 2);
//            this->recordFile.write((const uint8_t*) dummy, packetSizeWords);    // DEBUG
//            log_d("b %d", packetSizeWords);
//          }

          // Record raw audio to file
//          if (this->recordFile) {
//            this->recordFile.write((const uint8_t*) this->micRaw, packetSizeWords * 2);
//            //log_d("w %d", packetSizeWords * 2);
//          }

          // Copy audio to recording buffer
          if (this->recordRawW + packetSizeWords <= this->recordRawSizeSamples) {
            memcpy(this->recordRaw + this->recordRawW, this->micRaw, packetSizeWords * 2);
            this->recordRawW += packetSizeWords;
          } else {
            this->recordFinished = true;
          }
        }

        // Discard microphone data
        this->micRawR = packetSizeWords;
      }
    }
  }

  // DECODING PART: decode current audio stream and place data into the output buffer

  //info.time[4] = micros();

  if (this->playback == Playback::LocalPcm) {
    if (this->playDecFramesLeft == 0) {
      static int pcm_offset = 0;

      if (!playbackFile.available()) {
        this->setFilePos(0);
      }

      int res = playbackFile.read((uint8_t*)this->playDec, sizeof(this->playDec));

      this->playDecCurFrame = 0;
      this->playDecFramesLeft = res / 2;

      this->playChunk();
    }


  } else if (this->playback == Playback::Record) {
    if (this->playDecFramesLeft <= 0 && this->recordRaw) {
      int sz = (this->recordRawW - this->recordRawR) * sizeof(this->recordRaw[0]);
      if (sz > 0 && this->recordRawR < this->recordRawSizeSamples) {
        if (sz > sizeof(this->playDec)) {
          sz = sizeof(this->playDec);
        }
        memcpy(this->playDec, this->recordRaw + this->recordRawR, sz);

        this->playDecCurFrame = 0;
        this->playDecFramesLeft = sz / sizeof(this->playDec[0]);     // 16-bit samples assumed implicitly; TODO: use current bits per sample
        this->recordRawR += sz / sizeof(this->recordRaw[0]);
        this->playChunk();
      }
    }

  } else if (this->playback == Playback::RtpStream) {

    if (this->playDecCurFrame > 0) {
      // Move the data to beginning of output buffer (because we are about to receive some more data)     // TODO: maybe do the same for MP3?
      memmove(this->playDec, this->playDec + this->playDecCurFrame, sizeof(this->playDec[0]) * this->playDecFramesLeft);
      this->playDecCurFrame = 0;
    }

    // Do not attempt to decode if the buffer doesn't have much free space
    uint16_t playDecFreeSpace = sizeof(this->playDec)/sizeof(this->playDec[0]) - this->playDecFramesLeft;
    if (wifiState.isConnected() &&
        (!this->playDecCurFrame || playDecFreeSpace >= this->voipPacketSize)) { // if the output buffer is empty or has enough space for a big voip packet (NOTE: former is not always part of latter)
//      // Debug
//      this->rtpCnt++;       // remove

      // RECEIVE AUDIO STREAM

      // Receive RTP packet
      if (rtp.available()) {
        // should never happen, but just to be safe
        log_d("RTP flushed");
        rtp.flush();
      };

      int32_t len = rtp.parsePacket();

      if (len == 0) {
        uint32_t tmpSilentAudio = millis();
        if ((tmpSilentAudio - rtpSilentScan) > STP_SILENT_PERIOD) {
          log_d("tmpSilentAudio is: %ld  and rtpSilentScan is: %ld", tmpSilentAudio, rtpSilentScan);
          log_d("NO RTP PACKETS FROM REMOTE PART");
          rtpSilentScan = tmpSilentAudio;
          rtpSilentPeriod = RTP_SILENT_ON;
        }
      }

      if (len > 0) {
        rtpSilentScan = millis();
        rtpSilentPeriod = RTP_SILENT_OFF;
        //log_d("RTP packet received: %d", len);

        // Stats
        this->packetsReceived++;
        uint16_t remotePort = rtp.remotePort();
        if (rtp.remotePort() % 2 == 0) {
          this->rtpPort = rtp.remotePort();
        } else {
          //this->rtcpPort = rtp.remotePort();
          //this->rtcpPacketsReceived++;
        }

        // Debug
        //      if (this->rtpCnt % 10 == 0) {
        //        rtp.beginPacket("192.168.1.15", remotePort+1);
        //        rtp.write((const uint8_t*) "ACK", 3);
        //        if (!rtp.endPacket()) log_d("sending fail");
        //      }

        // Parse packet

        len = rtp.read(playEnc, sizeof(playEnc) - 1);
        if (len > 12) {
          if (rtp.remotePort() == rtpRemotePort || !rtpRemotePort) {    // ensuring that the audio comes from the right port; TODO: ensure also that it comes from the right IP
            // Parse RTP packet
            //uint8_t payloadType = rtpRecv.decodeHeader(playEnc);

            rtpRecv.setHeader(playEnc);
            uint8_t payloadType = rtpRecv.getPayloadType();

            if (payloadType == rtpPayloadType) {
              // Did packets arrive in correct sequence?
              bool inSeq = false;


              uint16_t seqDiff = (rtpRecv.getSequenceNumber() >= this->lastSequenceNum) ?
                                 rtpRecv.getSequenceNumber() - this->lastSequenceNum :
                                 0xffffu - this->lastSequenceNum + rtpRecv.getSequenceNumber();




              /*
              uint16_t seqDiff = (rtpRecv.getSequenceNum() >= this->lastSequenceNum) ?
                                   rtpRecv.getSequenceNum() - this->lastSequenceNum :
                                   0xffffu - this->lastSequenceNum + rtpRecv.getSequenceNum();
                                   */
              if (this->firstPacket) {
                inSeq = true;
                this->firstPacket = false;
                log_i("Sound source (SSRC): %u", rtpRecv.getSSRC());
              }

              if (seqDiff > 0 && seqDiff <= 1000) {   // not more than 20 seconds apart (20ms packet)
                // Packet in order (maybe some packets missed)
                inSeq = true;
                if (seqDiff > 1) {
                  // Some packets were missed
                  log_d("miss %d", seqDiff - 1);
                  this->packetsMissed += seqDiff - 1;
                }
                // Show how many packets arrived not in order until this one got received
                if (this->packetsUnord > 0) {
                  log_d("unord %d", this->packetsUnord);
                  this->packetsUnord = 0;
                }
              } else if (seqDiff > 0) {
                // Packet not in order -> count packets arriving not in order, until one received that is in order
                this->packetsUnord++;
              } else {
                // This packet was already received before
                log_d("dup");
              }

              // Decode packet audio if in correct sequence
              if (inSeq) {
                this->packetsGood++;

                //info.time[5] = micros();

                // Decode packet. If packet is too big -> drop it;      TODO: decode and use packet partially

                const int32_t RTP_HEADER_SIZE = 12;                  // TODO: make RTP class tell the header size
                if (payloadType == G722_RTP_PAYLOAD) {
                  if ((len - RTP_HEADER_SIZE)*2 < playDecFreeSpace) {         // G.722 typically decodes 160 bytes into 320 samples (640 bytes)
                    int16_t samplesDecoded = g722_decode(g722Decoder, playEnc + RTP_HEADER_SIZE, len - RTP_HEADER_SIZE, playDec + playDecCurFrame);
                    if (samplesDecoded > 0) {
                      playDecFramesLeft += samplesDecoded;
                    }
                  }
                } else if (payloadType == ALAW_RTP_PAYLOAD) {
                  if (len - RTP_HEADER_SIZE < playDecFreeSpace) {             // G.711 typically decodes 160 bytes into 160 samples (320 bytes)
                    alaw_expand(len, playEnc + RTP_HEADER_SIZE, playDec + playDecCurFrame);
                    playDecFramesLeft += len - RTP_HEADER_SIZE;     // G.711 just turns each byte into two bytes (except for 12 bytes of the RTP header)
                  }
                } else if (payloadType == ULAW_RTP_PAYLOAD) {
                  if (len - RTP_HEADER_SIZE < playDecFreeSpace) {             // G.711 typically decodes 160 bytes into 160 samples (320 bytes)
                    ulaw_expand(len, playEnc + RTP_HEADER_SIZE, playDec + playDecCurFrame);
                    playDecFramesLeft += len - RTP_HEADER_SIZE;
                  }
                }

                // Remember sequence number
                // TODO: if this sequence is incorrect, entire call audio might be discarded; add resiliency
                //this->lastSequenceNum = rtpRecv.getSequenceNum();
                this->lastSequenceNum = rtpRecv.getSequenceNumber();
              }
            } else {
              this->packetsWrongPayload++;
              log_d("unknown fmt %d", payloadType);
            }
          } else {
            //log_d("audio from incorrect port");
          }
        } else if (len > 0) {
          log_d("packet too short");
        }
        //} else if (len < 0 && len!=-3) {
        //  // Debugging
        //  log_d("parse packet err=%d", len);
      }

      // TODO: figure out why RTCP doesn't work
//        // Receive RTCP packets
//        if (udpRtcp.available()) {
//          // should never happen, but just to be safe
//          DEBUG("RTCP flushed");
//          udpRtcp.flush();
//        };
//        if (udpRtcp.parsePacket()>0) {
//          // Stats
//          gui.state.rtcpPort = udpRtcp.remotePort();
//          gui.state.rtcpPacketsReceived++;
//
//          // Parse packet
//          l = udpRtcp.read(recv_buff, sizeof(recv_buff)-1);
//        }


      // Play right away, don't wait for the next loop
      //oldSmp = this->playDecFramesLeft;
      if (this->playDecFramesLeft>0) {
        this->playChunk();
      }
      //info.samples[1] = oldSmp - this->playDecFramesLeft;

    }
    if (wifiState.isConnected()) {
      // Not enough space in the receiving buffer for another packet
      // TODO: better to drop packet which was not yet decoded
      if (this->playDecFramesLeft > this->voipPacketSize) {
        this->playDecFramesLeft -= this->voipPacketSize;
        this->playDecCurFrame += this->voipPacketSize;
        log_d("decoded packet dropped");
      }
    }
  }

  //info.time[6] = micros();
  //if (info.time[6] - info.time[0] >= 6)
  //  profile.add(info);
}


uint32_t Audio::getFileSize() {
  if (!playbackFile) {
    return 0;
  }
  return playbackFile.size();
}

uint32_t Audio::getFilePos() {
  if (!playbackFile) {
    return 0;
  }
  return playbackFile.position();
}

bool Audio::setFilePos(uint32_t pos) {
  if (!playbackFile) {
    return false;
  }
  return playbackFile.seek(pos);
}

/* Description:
 *     calculate number of samples in an audio packet of given duration
 *     (e.g. 160 samples for 20 ms 8000 Hz audio, 320 samples for 20 ms of 16 KHz audio)
 * Paramters:
 *     duration in milliseconds
 * Return:
 *     number of samples (typically, number of 16-bit words for a given duration)
 */
int Audio::packetSizeSamples(int duration) {
  return this->dataChannels * this->sampleRate * duration / 1000;
}

bool Audio::setSampleRate(int freq) {
  log_d("SAMPLE RATE = %d", freq);
  this->sampleRate = freq;
  i2s_set_sample_rates((i2s_port_t) i2s_num, this->sampleRate);
  this->voipPacketSize = this->packetSizeSamples(VOIP_PACKET_DURATION_MS);
  return true;
}

bool Audio::setBitsPerSample(int bits) {
  if ( (bits != 16) && (bits != 8) ) {
    return false;
  }
  this->bps = bits;
  this->configureI2S();
  //i2s_set_clk((i2s_port_t) i2s_num, this->sampleRate, this->bps==16 ? I2S_BITS_PER_SAMPLE_16BIT : I2S_BITS_PER_SAMPLE_8BIT, this->monoOut ? I2S_CHANNEL_MONO : I2S_CHANNEL_STEREO );      // TODO: does it work?
  return true;
}

void Audio::setMonoOutput(bool mono) {
  // this->monoOut affects I2S interface
  log_d("monoOut = %s", mono ? "true" : "false");
  this->monoOut = mono;
  this->configureI2S();
  codec.setAudioPath(!mono);
};

bool Audio::setDataChannels(int ch) {
  // this->dataChannels shows how many channels are being decoded (from MP3 or another audio stream)
  if ( (ch < 1) || (ch > 2) ) {
    return false;
  }
  this->dataChannels = ch;
  this->voipPacketSize = this->packetSizeSamples(VOIP_PACKET_DURATION_MS);
  log_d("Channels=%i", this->dataChannels);
  return true;
}

AUDIO_INLINE bool Audio::playSample() {
  if (this->bps == 8) {
    // Upsample from unsigned 8 bits to signed 16 bits
    this->sample[0] = (((int16_t)(this->sample[0]&0xff)) - 128) << 8;
    this->sample[1] = (((int16_t)(this->sample[1]&0xff)) - 128) << 8;
  }

  size_t bytesWritten;
  if (this->monoOut) {
    esp_err_t err = i2s_write((i2s_port_t) i2s_num, ((const char*)this->sample), sizeof(this->sample[0]), &bytesWritten, 0);
    return (err==ESP_OK && bytesWritten==sizeof(this->sample[0]));
  } else {
    esp_err_t err = i2s_write((i2s_port_t) i2s_num, ((const char*)this->sample), sizeof(this->sample),    &bytesWritten, 0);
    return (err==ESP_OK && bytesWritten==sizeof(this->sample));
  }
}

void Audio::newCall() {
  this->firstPacket = true;
  this->lastSequenceNum = 0;

  this->rtpPort = 0;
  this->rtcpPort = 0;
  this->rtcpPacketsReceived = 0;

  // QoS stats
  this->packetsReceived = 0;
  this->packetsGood = 0;
  this->packetsWrongPayload = 0;
  this->packetsMissed = 0;
  this->packetsUnord = 0;

  this->packetsSent = 0;
  this->packetsSendingFailed = 0;
}

void Audio::showAudioStats() {
  log_d("Incoming audio packets:");
  log_d(" received:  %d", this->packetsReceived);
  log_d("     good:  %d", this->packetsGood);
  log_d("    wrong:  %d", this->packetsWrongPayload);
  log_d("     miss:  %d", this->packetsMissed);
  if (this->packetsGood > 0 && this->packetsMissed > 0) {
    log_d("good/(miss+good): %.2f%%", (float) this->packetsGood/(this->packetsGood + this->packetsMissed)*100);
  }
  log_d("    unord: %d", this->packetsUnord);

  log_d("Outgoing audio packets:");
  log_d("    total:  %d", this->packetsSent);
  log_d("   failed:  %d (%.2f%%)", this->packetsSendingFailed, (float) this->packetsSendingFailed/this->packetsSent*100);

  log_d("Total RTCP packets received: %d", this->rtcpPacketsReceived);
  log_d(" RTP port: %d", this->rtpPort);
  log_d("RTCP port: %d", this->rtcpPort);
}

uint16_t Audio::openRtpConnection(uint16_t rtpLocalPort) {
  rtp.begin(rtpLocalPort);          // TODO: check if successful, allow search for a free port (or next port) on its own
  return rtpLocalPort;
}

bool Audio::playRtpStream(uint8_t payloadType, uint16_t remotePort) {
  log_d("playing rtp");

  // Determine sample rate and initialize audio configs
  uint16_t sampleRate = (payloadType == ALAW_RTP_PAYLOAD || payloadType == ULAW_RTP_PAYLOAD) ? 8000 : 16000;      // default is 16000
  this->setSampleRate(sampleRate);
  this->setDataChannels(1);
  this->setMonoOutput(true);      // this should be called last (since it shows all the configs via Serial)   TODO

  // Start the audio systems (if not started)
  if (!this->turnOn()) {
    return false;
  }

  // Prepare to receive packets
  rtpRemotePort = remotePort;
  rtpPayloadType = payloadType;
  log_d("rtpPayloadType = %d", rtpPayloadType);

  // Reset QoS variables
  this->newCall();

  // Clear buffers
  this->playEncW=0;
  this->playEncR=0;
  this->playDecFramesLeft = 0;

  // Reset debugging
  this->loopCnt = this->runCnt = this->rtpCnt = 0;

  // Kickstart playback
  this->playback = Playback::RtpStream;

  return true;
}

bool Audio::sendRtpStreamFromMic(uint8_t payloadType, IPAddress remoteAddr, uint16_t remotePort) {

  // TODO: check correctness of the parameters
  this->rtpPayloadType = payloadType;
  this->rtpRemoteIP = remoteAddr;
  this->rtpRemotePort = remotePort;

  // Determine sample rate and initialize audio configs
  // Configuration is exactly the same as for playback
  uint16_t sampleRate = (payloadType == ALAW_RTP_PAYLOAD || payloadType == ULAW_RTP_PAYLOAD) ? 8000 : 16000;      // default is 16000
  this->setSampleRate(sampleRate);
  this->setDataChannels(1);
  this->setMonoOutput(true);      // this should be called last (since it shows all the configs via Serial)   TODO

  // Start the audio systems (if not started)
  if (!this->turnOn()) {
    return false;
  }

  if (!this->turnMicOn()) {
    return false;
  }
  this->calcMicIntensity = false;

  // Prepare RTP header for sending
  rtpSend.setPayloadType(payloadType);
  rtpSend.newSession();
  // Kickstart streaming
  this->microphoneStreamOut = true;
}

bool Audio::recordFromMic() {

  if (this->playback == Playback::Record) {
    this->ceasePlayback();
  }

  if (this->recordRaw == NULL) {
    this->recordRaw = (uint16_t*) extMalloc(RECORDING_SIZE_SAMPLES);
  }
  if (this->recordRaw == NULL) {
    log_d("failed allocating 1MB recording buffer");
    return false;
  }
  this->recordRawW = this->recordRawR = 0;
  this->recordRawSizeSamples = RECORDING_SIZE_SAMPLES/2;
  this->recordFinished = false;

  // Start the audio systems (if not started)
  if (!this->turnOn()) {
    return false;
  }

  if (!this->turnMicOn()) {
    return false;
  }

  // Kickstart recording
  this->microphoneRecord = true;

  return true;
}

bool Audio::saveWavRecord(fs::FS *fs, const char* pathName) {

  this->microphoneRecord = false;   // if we are saving, we automatically stop recording

  File recordFile = fs->open(pathName, FILE_WRITE);
  if (recordFile) {
    log_d("created file");
  } else {
    log_d("failed creating file");
    return false;
  }

  if (this->recordRawW > 0) {
    recordFile.write((const uint8_t*) this->recordRaw, this->recordRawW * 2);
    log_i("%d bytes written to audio file", this->recordRawW * 2);
  }
  recordFile.close();

  return true;
}

void Audio::ceaseRecording() {
  this->microphoneRecord = false;
  freeNull((void **) &this->recordRaw);
}

bool Audio::turnMicOn() {

  // Start the audio systems (if not started)
  // TODO: separate electrically switching microphone ON into this routine
  if (!this->turnOn()) {
    return false;
  }

  // Reset mic buffers
  this->micRawR = 0;
  this->micRawW = 0;
  memset(this->micAvg, 0, sizeof(this->micAvg));

  // Start microphone data processing (calculate average intensity)
  this->microphoneOn = true;
  this->calcMicIntensity = true;

  return true;
}

/* Description:
 *     save data point to the microphone volume averaging array
 */
void Audio::setMicAvg(uint32_t mic) {
  this->micAvg[this->micAvgNext++] = mic;
  if (this->micAvgNext >= sizeof(this->micAvg)/sizeof(this->micAvg[0])) {
    this->micAvgNext = 0;
  }
}

/* Description:
 *      get average microphone volume
 */
uint32_t Audio::getMicAvg() {
  uint32_t val = 0;
  for (int i = 0; i < sizeof(this->micAvg)/sizeof(this->micAvg[0]); i++) {
    val += this->micAvg[i];
  }
  return val / (sizeof(this->micAvg)/sizeof(this->micAvg[0]));
}
