#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <FastLED.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include <Preferences.h>
#include <bt_con.h>
#include <bt_disc.h>

I2SStream out;
BluetoothA2DPSink btAudio(out);

#define led 2  // built-in LED pin on ESP32 DevKit v1
#define mute_Pin 14  //mute pin

//RGB WS2812B
#define rgb_pin 13  // RGB pin
#define NUM_LEDS 8

CRGB leds[NUM_LEDS];
volatile uint16_t audio_peak = 0;
uint16_t visualizer_level = 0;
unsigned long last_visualizer_update = 0;

void readAudioForVisualizer(const uint8_t *data, uint32_t length)
{
  const int16_t *samples = reinterpret_cast<const int16_t *>(data);
  uint16_t peak = 0;

  for (uint32_t index = 0; index < length / sizeof(int16_t); index++)
  {
    int32_t sample = samples[index];
    uint16_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > peak)
      peak = magnitude;
  }

  // Keep only the strongest value from each Bluetooth audio callback.
  if (peak > audio_peak)
    audio_peak = peak;
}

void updateVisualizer()
{
  if (millis() - last_visualizer_update < 25)
    return;

  last_visualizer_update = millis();
  uint16_t peak = audio_peak;
  audio_peak = 0;

  // A small gain makes normal music visible without making silence glow.
  uint16_t target = constrain((peak * 255UL) / 18000UL, 0, 255);
  if (target > visualizer_level)
    visualizer_level = target;
  else if (visualizer_level > 3)
    visualizer_level -= 3;
  else
    visualizer_level = 0;

  uint8_t lit_leds = map(visualizer_level, 0, 255, 0, NUM_LEDS);
  for (uint8_t index = 0; index < NUM_LEDS; index++)
  {
    if (index < lit_leds)
    {
      uint8_t hue = map(index, 0, NUM_LEDS - 1, 160, 0);
      leds[index] = CHSV(hue, 255, 220);
    }
    else
    {
      leds[index] = CRGB::Black;
    }
  }

  // Keep the top LED responsive even when the level is between two bars.
  if (visualizer_level > 0 && lit_leds < NUM_LEDS)
    leds[lit_leds] = CHSV(map(lit_leds, 0, NUM_LEDS - 1, 160, 0), 255,
                          map(visualizer_level % 32, 0, 31, 40, 180));

  FastLED.show();
}

// bt name
const char *bt_name = "MAAGO..MEEGO";

// led
unsigned long last_blink_time = 0;
const unsigned long blink_interval = 1000;

bool device_connected = false;
bool previous_device_connected = false;
bool connection_sound_pending = false;
unsigned long connection_sound_time = 0;

void prepareNotificationOutput()
{
  if (out.isActive())
    return;

  auto cfg = out.defaultConfig();
  cfg.pin_bck = 26;
  cfg.pin_ws = 27;
  cfg.pin_data = 25;
  out.begin(cfg);
}


void playBtConnectedSound()
{
  static bool in_progress = false;
  if (in_progress || !btAudio.is_connected() ||
      btAudio.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED)
    return;

  in_progress = true;

  prepareNotificationOutput();
  MemoryStream tone(bt_con_mp3, sizeof(bt_con_mp3));
  MP3DecoderHelix decoder;
  EncodedAudioStream sound(&out, &decoder);
  StreamCopy copier(sound, tone);

  if (sound.begin())
  {
    while (tone.available())
    {
      if (btAudio.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED)
        break;

      if (copier.copy() == 0)
        break;
    }
    sound.end();
  }

  in_progress = false;
}

void playBtDisconnectedSound()
{
  static bool in_progress = false;
  if (in_progress)
    return;

  in_progress = true;

  prepareNotificationOutput();
  MemoryStream tone(bt_disc_mp3, sizeof(bt_disc_mp3));
  MP3DecoderHelix decoder;
  EncodedAudioStream sound(&out, &decoder);
  StreamCopy copier(sound, tone);

  if (sound.begin())
  {
    while (tone.available())
    {
      if (copier.copy() == 0)
        break;
    }
    sound.end();
  }

  in_progress = false;
}


void loop_blink()
{
  if (device_connected)
  {
    unsigned long current_time = millis();
    if (current_time - last_blink_time >= blink_interval)
    {
      digitalWrite(led, !digitalRead(led)); // Toggle LED state
      last_blink_time = current_time;
    }
  }
  else
  {
    digitalWrite(led, HIGH); // Turn on LED when not connected
  }
}

void soundPlay()
{
  device_connected = btAudio.is_connected();

  if (device_connected != previous_device_connected)
  {
    if (device_connected)
    {
      Serial.println("Bluetooth connected");
      last_blink_time = millis();
      connection_sound_pending = true;
      connection_sound_time = millis() + 1000;
    }
    else
    {
      Serial.println("Bluetooth disconnected");
      digitalWrite(led, HIGH);
      playBtDisconnectedSound();
    }

    previous_device_connected = device_connected;
  }

  if (connection_sound_pending &&
      (long)(millis() - connection_sound_time) >= 0)
  {
    connection_sound_pending = false;

    if (btAudio.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED)
    {
      Serial.println("Music already started; connection sound skipped");
    }
    else
    {
      playBtConnectedSound();
    }
  }
}


void setup()
{
  Serial.begin(115200);
  pinMode(led, OUTPUT);
  pinMode(mute_Pin, OUTPUT);
  
  digitalWrite(mute_Pin, HIGH); // HIGH = mute, LOW = unmute
  Serial.println("Bluetooth Controller Starting");
  auto cfg = out.defaultConfig();
  cfg.pin_bck = 26;
  cfg.pin_ws = 27;
  cfg.pin_data = 25;
  out.begin(cfg);

  FastLED.addLeds<WS2812B, rgb_pin, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(80);
  FastLED.clear(true);

  // Analyze decoded PCM while retaining the normal I2S audio output.
  btAudio.set_stream_reader(readAudioForVisualizer, true);
  btAudio.start(bt_name);
  Serial.println("Bluetooth Controller Initialized");
  Serial.print("Device name: ");
  Serial.println(bt_name);
  digitalWrite(led, HIGH);
}



void loop()
{
  
  soundPlay();

  loop_blink();
  updateVisualizer();

}
