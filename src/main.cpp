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

#define led 2       // built-in LED pin on ESP32 DevKit v1
#define mute_Pin 14 // mute pin
#define mute_btn 21 // testing mute button

// RX TX pin to receive serial commands from a computer or other device
#define rxPin 16
#define txPin 17

// RGB WS2812B
#define rgb_pin_R 32 // RGB pin right
#define rgb_pin_L 33 // RGB pin left
int NUM_LEDS = 8; // default 8 LEDs per strip

// bt name
String bt_name = "MAAGO..MEEGO";

// led
unsigned long last_blink_time = 0;
const unsigned long blink_interval = 1000;

bool device_connected = false;
bool previous_device_connected = false;
bool connection_sound_pending = false;
unsigned long connection_sound_time = 0;
bool bt_on = true;
bool bt_con_sound_played = false;
bool bt_disc_sound_played = false;
bool mute_state = false; // false = unmuted, true = muted
String play_state = "stopped";

// current volume of the audio device (0..127)
uint8_t current_volume = 100;

CRGB ledsL[64];
CRGB ledsR[64];
volatile uint16_t audio_peak = 0;
uint16_t visualizer_level = 0;
unsigned long last_visualizer_update = 0;
bool onlyBass = true;

int bass_filter_strength = 4; // keyword title: 6 = bass, otherwise 4
int leds_delay = 10;          // 10 = fast, 25 = normal, 30 = slow, 40 = very slow
// LED brightness (0-100)
int leds_brightness = 70; // 0-100, default 70

void setLedBrightness()
{
  FastLED.setBrightness(constrain(leds_brightness, 0, 255));
}

bool containsKeywordIgnoreCase(const char *text, const char *keyword)
{
  size_t text_length = strlen(text);
  size_t keyword_length = strlen(keyword);

  if (keyword_length == 0 || keyword_length > text_length)
    return false;

  for (size_t index = 0; index <= text_length - keyword_length; index++)
  {
    bool matches = true;
    for (size_t offset = 0; offset < keyword_length; offset++)
    {
      if (tolower(text[index + offset]) != tolower(keyword[offset]))
      {
        matches = false;
        break;
      }
    }

    if (matches)
      return true;
  }

  return false;
}

int bassf = 7;
int bassL = 4;
int visualizer_pattern = 0; // 0 = classic bars

void loadVisualizerConfiguration()
{
  Preferences preferences;
  preferences.begin("visualizer", true);
  leds_brightness = preferences.getInt("brightness", 70);
  leds_delay = preferences.getInt("delay", 10);
  bassf = preferences.getInt("bassf", 7);
  bassL = preferences.getInt("bassl", 4);
  NUM_LEDS = preferences.getInt("leds", 8);
  visualizer_pattern = preferences.getInt("pattern", 0);
  preferences.end();

  leds_brightness = constrain(leds_brightness, 0, 100);
  leds_delay = constrain(leds_delay, 1, 50);
  bassf = constrain(bassf, 0, 8);
  bassL = constrain(bassL, 0, 8);
  visualizer_pattern = constrain(visualizer_pattern, 0, 3);
  Serial.print("config loaded");
}

void saveVisualizerConfiguration()
{
  Preferences preferences;
  preferences.begin("visualizer", false);
  preferences.putInt("brightness", leds_brightness);
  preferences.putInt("delay", leds_delay);
  preferences.putInt("bassf", bassf);
  preferences.putInt("bassl", bassL);
  preferences.putInt("leds", NUM_LEDS);
  preferences.putInt("pattern", visualizer_pattern);
  preferences.end();
}

void avrcMetadataCallback(uint8_t id, const uint8_t *text)
{
  if (id == ESP_AVRC_MD_ATTR_TITLE)
  {
    const char *title = reinterpret_cast<const char *>(text);
    bool bass_keyword = containsKeywordIgnoreCase(title, "dj") ||
                        containsKeywordIgnoreCase(title, "bass") ||
                        containsKeywordIgnoreCase(title, "party") ||
                        containsKeywordIgnoreCase(title, "remix") ||
                        containsKeywordIgnoreCase(title, "bassline") ||
                        containsKeywordIgnoreCase(title, "subwoofer") ||
                        containsKeywordIgnoreCase(title, "trap") ||
                        containsKeywordIgnoreCase(title, "bassdrop") ||
                        containsKeywordIgnoreCase(title, "bassboost") ||
                        containsKeywordIgnoreCase(title, "bassnation") ||
                        containsKeywordIgnoreCase(title, "jedag") ||
                        containsKeywordIgnoreCase(title, "jedug") ||
                        containsKeywordIgnoreCase(title, "basswave") ||
                        containsKeywordIgnoreCase(title, "basshead") ||
                        containsKeywordIgnoreCase(title, "bassline") ||
                        containsKeywordIgnoreCase(title, "basshouse") ||
                        containsKeywordIgnoreCase(title, "sentak");

    bass_filter_strength = bass_keyword ? bassf : bassL;
    Serial.printf("Judul lagu: %s\n", text);
    Serial.printf("Bass filter strength: %d\n", bass_filter_strength); // only for debug
  }
  else if (id == ESP_AVRC_MD_ATTR_ARTIST)
    Serial.printf("Artis: %s\n", text);
  else if (id == ESP_AVRC_MD_ATTR_ALBUM)
    Serial.printf("Album: %s\n", text);
}

void readAudioForVisualizer(const uint8_t *data, uint32_t length)
{
  const int16_t *samples = reinterpret_cast<const int16_t *>(data);
  uint16_t peak = 0;
  static int32_t bass_filter = 0;

  for (uint32_t index = 0; index < length / sizeof(int16_t); index++)
  {
    int32_t sample = samples[index];
    if (onlyBass)

      bass_filter += (sample - bass_filter) >> bass_filter_strength;

    int32_t filtered_sample = onlyBass ? bass_filter : sample;
    uint16_t magnitude = filtered_sample < 0 ? -filtered_sample : filtered_sample;
    if (magnitude > peak)
      peak = magnitude;
  }

  // Keep only the strongest value from each Bluetooth audio callback.
  if (peak > audio_peak)
    audio_peak = peak;
}

void updateVisualizer()
{
  if (millis() - last_visualizer_update < leds_delay)
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

  uint16_t lit_leds = map(visualizer_level, 0, 255, 0, NUM_LEDS);
  if (visualizer_pattern == 0)
  {
    for (uint16_t index = 0; index < NUM_LEDS; index++)
    {
      uint16_t left_index = NUM_LEDS - 1 - index;
      if (index < lit_leds)
      {
        uint8_t hue = map(index, 0, NUM_LEDS - 1, 160, 0);
        ledsR[index] = CHSV(hue, 255, 220);
        ledsL[left_index] = CHSV(hue, 255, 220);
      }
      else
      {
        ledsR[index] = CRGB::Black;
        ledsL[left_index] = CRGB::Black;
      }
    }

    // Keep the top LED responsive even when the level is between two bars.
    if (visualizer_level > 0 && lit_leds < NUM_LEDS)
    {
      uint8_t hue = map(lit_leds, 0, NUM_LEDS - 1, 160, 0);
      uint16_t left_index = NUM_LEDS - 1 - lit_leds;
      ledsR[lit_leds] = CHSV(hue, 255, map(visualizer_level % 32, 0, 31, 40, 180));
      ledsL[left_index] = CHSV(hue, 255, map(visualizer_level % 32, 0, 31, 40, 180));
    }
  }
  else if (visualizer_pattern == 1)
  {
    uint8_t pulse = map(visualizer_level, 0, 255, 25, 255);
    for (uint16_t index = 0; index < NUM_LEDS; index++)
    {
      uint8_t hue = (millis() / 10 + index * 5) & 0xFF;
      ledsR[index] = CHSV(hue, 230, pulse);
      ledsL[index] = CHSV(hue + 12, 230, pulse);
    }
  }
  else if (visualizer_pattern == 2)
  {
    static uint16_t comet_position = 0;
    uint16_t comet_speed = map(visualizer_level, 0, 255, 1, 12);
    comet_position = (comet_position + comet_speed) % NUM_LEDS;
    for (uint16_t index = 0; index < NUM_LEDS; index++)
    {
      uint16_t distance = abs((int)index - (int)comet_position);
      uint8_t tail_value = distance < 6 ? 255 - distance * 38 : 8;
      uint8_t value = scale8(tail_value, map(visualizer_level, 0, 255, 80, 255));
      uint8_t hue = (index * 255UL / NUM_LEDS + millis() / 18) & 0xFF;
      ledsR[index] = CHSV(hue, 255, value);
      ledsL[NUM_LEDS - 1 - index] = CHSV(hue + 18, 255, value);
    }
  }
  else
  {
    for (uint16_t index = 0; index < NUM_LEDS; index++)
    {
      uint8_t wave = sin8(index * 256UL / NUM_LEDS + millis() / 8);
      uint8_t value = scale8(wave, map(visualizer_level, 0, 255, 70, 255));
      uint8_t hue = (index * 180UL / NUM_LEDS + millis() / 20) & 0xFF;
      ledsR[index] = CHSV(hue, 220, value);
      ledsL[NUM_LEDS - 1 - index] = CHSV(hue + 24, 220, value);
    }
  }

  setLedBrightness();
  FastLED.show();
}

int loadSavedVolume()
{
  Preferences preferences;
  preferences.begin("btAudio", false);
  int savedVolume = preferences.getInt("volume", 64);
  preferences.end();
  return savedVolume;
}

void saveVolume(uint8_t volume)
{
  Preferences preferences;
  preferences.begin("btAudio", false);
  preferences.putInt("volume", volume);
  preferences.end();
}

String loadSavedBluetoothName()
{
  Preferences preferences;
  preferences.begin("btAudio", false);
  String savedName = preferences.getString("dev_name", "MAAGO..MEEGO");
  preferences.end();
  return savedName;
}

void saveBluetoothName(const String &name)
{
  Preferences preferences;
  preferences.begin("btAudio", false);
  preferences.putString("dev_name", name);
  preferences.end();
}

void avrc_rn_play_pos_callback(uint32_t play_pos)
{
  Serial.printf("Play position is %d (%d seconds)\n", play_pos, (int)round(play_pos / 1000.0));
}

void avrc_rn_playstatus_callback(esp_avrc_playback_stat_t playback)
{
  switch (playback)
  {
  case ESP_AVRC_PLAYBACK_STOPPED:
    Serial.println("Stopped.");
    play_state = "stopped";
    break;
  case ESP_AVRC_PLAYBACK_PLAYING:
    Serial.println("Playing.");
    play_state = "playing";
    break;
  case ESP_AVRC_PLAYBACK_PAUSED:
    Serial.println("Paused.");
    play_state = "paused";
    break;
  case ESP_AVRC_PLAYBACK_FWD_SEEK:
    Serial.println("Forward seek.");
    break;
  case ESP_AVRC_PLAYBACK_REV_SEEK:
    Serial.println("Reverse seek.");
    break;
  case ESP_AVRC_PLAYBACK_ERROR:
    Serial.println("Error.");
    play_state = "error";
    break;
  default:
    Serial.printf("Got unknown playback status %d\n", playback);
  }
}

void processSerialCommands()
{
  if (!Serial2.available())
    return;

  String command = Serial2.readStringUntil('#');

  if (command.equals("vol"))
  {
    int vol = Serial2.parseInt();
    vol = constrain(vol, 0, 127);
    Serial.println("Changing Volume to: " + String(vol));
    current_volume = (uint8_t)vol;
    btAudio.set_volume(current_volume);
    saveVolume(current_volume);
  }
  else if (command.equals("on"))
  {
    if (!bt_on)
    {
      btAudio.start(bt_name.c_str());
      btAudio.set_volume(current_volume);
      btAudio.set_auto_reconnect(true, 5);
      bt_on = true;
    }
    Serial2.println("state#ON");
    digitalWrite(led, HIGH);
  }
  else if (command.equals("off"))
  {
    if (bt_on)
    {
      btAudio.end();
      bt_on = false;
    }
    Serial2.println("state#OFF");
    digitalWrite(led, LOW);
  }
  else if (command.equals("mute"))
  {
    String mute_cmd = Serial2.readStringUntil('#');
    mute_cmd.trim();

    if (mute_cmd.equalsIgnoreCase("HIGH") || mute_cmd.equalsIgnoreCase("UNMUTE") || mute_cmd == "1")
    {
      digitalWrite(mute_Pin, HIGH);
      mute_state = false;
      Serial.println("Audio unmuted");
      Serial2.println("mute#HIGH");
    }
    else if (mute_cmd.equalsIgnoreCase("LOW") || mute_cmd.equalsIgnoreCase("MUTE") || mute_cmd == "0")
    {
      digitalWrite(mute_Pin, LOW);
      mute_state = true;
      Serial.println("Audio muted");
      Serial2.println("mute#LOW");
    }
    else
    {
      Serial.println("Invalid mute command");
    }
  }
  else if (command.equals("next"))
  {
    btAudio.next();
  }
  else if (command.equals("prev") || command.equals("previous"))
  {
    btAudio.previous();
  }
  else if (command.equals("pause"))
  {
    btAudio.pause();
  }
  else if (command.equals("play"))
  {
    btAudio.play();
  }
  else if (command.equals("stop"))
  {
    btAudio.stop();
  }
  else if (command.equals("name"))
  {
    String new_name = Serial2.readStringUntil('#');
    new_name.trim();

    if (new_name.length() == 0)
    {
      Serial.println("Name cannot be empty");
    }
    else
    {
      Serial.print("Changing name to: ");
      Serial.println(new_name);
      bt_name = new_name;
      saveBluetoothName(new_name);
      btAudio.end();
      btAudio = BluetoothA2DPSink(out);
      btAudio.start(bt_name.c_str());
      btAudio.set_volume(current_volume);
      btAudio.set_auto_reconnect(true, 5);
      bt_on = true;
      Serial2.println("state#ON");
    }
  }
  else if (command.equals("getStatus"))
  {
    Serial2.print("status#");
    Serial2.print(bt_on ? "ON" : "OFF");
    Serial2.print("#");
    Serial2.print(bt_name);
    Serial2.print("#");
    Serial2.print(current_volume);
    Serial2.println();
    Serial2.print("playState#");
    Serial2.println(play_state);
  }
  else if (command.equals("playState"))
  {
    Serial2.print("playState#");
    Serial2.println(play_state);
  }
}
// Function to handle serial commands for the visualizer from serial port 115200
void serialVizualierCTRL()
{
  if (!Serial.available())
    return;

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toLowerCase();

  if (command == "print")
  {
    Serial.println("Current configuration:");
    Serial.printf("brightness#%d\n", leds_brightness);
    Serial.printf("delay#%d\n", leds_delay);
    Serial.printf("bassf#%d\n", bassf);
    Serial.printf("bassl#%d\n", bassL);
    Serial.printf("leds#%d\n", NUM_LEDS);
    Serial.printf("pattern#%d\n", visualizer_pattern);
  }
  else if (command == "save")
  {
    saveVisualizerConfiguration();
    Serial.println("OK configuration saved");
  }
  else if (command == "next")
  {
    btAudio.next();
    Serial.println("OK next");
  }
  else if (command == "prev" || command == "previous")
  {
    btAudio.previous();
    Serial.println("OK prev");
  }
  else if (command == "play")
  {
    btAudio.play();
    Serial.println("OK play");
  }
  else if (command == "pause")
  {
    btAudio.pause();
    Serial.println("OK pause");
  }
  else if (command.startsWith("brightness"))
  {
    String value_text = command.substring(String("brightness").length());
    value_text.trim();

    if (value_text.startsWith("=") || value_text.startsWith("#"))
      value_text.remove(0, 1);
    value_text.trim();

    if (value_text.length() == 0)
    {
      Serial.printf("brightness#%d\n", leds_brightness);
      return;
    }

    int brightness = value_text.toInt();
    if (brightness < 0 || brightness > 100)
    {
      Serial.println("ERROR brightness must be 0-100");
      return;
    }

    leds_brightness = brightness;
    setLedBrightness();
    FastLED.show();
    Serial.printf("OK brightness#%d\n", leds_brightness);
  }
  else if (command.startsWith("delay"))
  {
    String value_text = command.substring(String("delay").length());
    value_text.trim();

    if (value_text.startsWith("=") || value_text.startsWith("#"))
      value_text.remove(0, 1);
    value_text.trim();

    if (value_text.length() == 0)
    {
      Serial.printf("delay#%d\n", leds_delay);
      return;
    }

    int delay_value = value_text.toInt();
    if (delay_value < 1 || delay_value > 50)
    {
      Serial.println("ERROR delay must be 1-50");
      return;
    }

    leds_delay = delay_value;
    Serial.printf("OK delay#%d\n", leds_delay);
  }
  else if (command.startsWith("bassf"))
  {
    String value_text = command.substring(String("bassf").length());
    value_text.trim();

    if (value_text.startsWith("=") || value_text.startsWith("#"))
      value_text.remove(0, 1);
    value_text.trim();

    if (value_text.length() == 0)
    {
      Serial.printf("bassf#%d\n", bassf);
      return;
    }

    int bass_value = value_text.toInt();
    if (bass_value < 0 || bass_value > 10)
    {
      Serial.println("ERROR bassf must be 0-10");
      return;
    }

    bassf = bass_value;
    bass_filter_strength = bass_value;
    Serial.printf("OK bassf#%d\n", bassf);
  }
  else if (command.startsWith("bassl"))
  {
    String value_text = command.substring(String("bassl").length());
    value_text.trim();

    if (value_text.startsWith("=") || value_text.startsWith("#"))
      value_text.remove(0, 1);
    value_text.trim();

    if (value_text.length() == 0)
    {
      Serial.printf("bassl#%d\n", bassL);
      return;
    }

    int bass_value = value_text.toInt();
    if (bass_value < 0 || bass_value > 10)
    {
      Serial.println("ERROR bassl must be 0-10");
      return;
    }

    bassL = bass_value;
    bass_filter_strength = bass_value;
    Serial.printf("OK bassl#%d\n", bassL);
  }
  else if (command.startsWith("pattern"))
  {
    String value_text = command.substring(String("pattern").length());
    value_text.trim();

    if (value_text.startsWith("=") || value_text.startsWith("#"))
      value_text.remove(0, 1);
    value_text.trim();

    if (value_text.length() == 0)
    {
      Serial.printf("pattern#%d\n", visualizer_pattern);
      return;
    }

    int pattern = value_text.toInt();
    if (pattern < 0 || pattern > 3)
    {
      Serial.println("ERROR pattern must be 0-3");
      return;
    }

    visualizer_pattern = pattern;
    FastLED.clear(true);
    Serial.printf("OK pattern#%d\n", visualizer_pattern);
  }
  else
  {
    Serial.println("ERROR unknown command");
    Serial.println("Available commands:");
    Serial.println("  brightness#<value> (0-100) default 70");
    Serial.println("Current configuration:");
    Serial.printf("brightness#%d\n", leds_brightness);

    Serial.println("  delay#<value> (1-50) 10 = fast, 25 = normal, 30 = slow, 40 = very slow  ");
    Serial.println("Current configuration:");
    Serial.printf("delay#%d\n", leds_delay);

    Serial.println("  bassf#<value> (0-8) 7 = is default");
    Serial.println("Current configuration:");
    Serial.printf("bassf#%d\n", bassf);

    Serial.println("  bassl#<value> (0-8) 4 = is default");
    Serial.println("Current configuration:");
    Serial.printf("bassl#%d\n", bassL);

    Serial.println("  pattern#<value> 0=classic, 1=pulse, 2=comet, 3=wave");
    Serial.println("Current configuration:");
    Serial.printf("pattern#%d\n", visualizer_pattern);

    Serial.println("  save  save current visualizer configuration");
    Serial.println("   print to print all configuration");
  }
}

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

#include "BluetoothSerial.h"
BluetoothSerial SerialBt;

void btcon()
{
  while (SerialBt.available())
  {
    String command = SerialBt.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command.length() == 0)
      continue;

    if (command == "print")
    {
      Serial.println("Current configuration:");
      Serial.printf("brightness#%d\n", leds_brightness);
      Serial.printf("delay#%d\n", leds_delay);
      Serial.printf("bassf#%d\n", bassf);
      Serial.printf("bassl#%d\n", bassL);
      SerialBt.printf("leds#%d\n", NUM_LEDS);
      //
      SerialBt.println("Current configuration:");
      SerialBt.printf("brightness#%d\n", leds_brightness);
      SerialBt.printf("delay#%d\n", leds_delay);
      SerialBt.printf("bassf#%d\n", bassf);
      SerialBt.printf("bassl#%d\n", bassL);
      SerialBt.printf("leds#%d\n", NUM_LEDS);
      SerialBt.printf("pattern#%d\n", visualizer_pattern);
    }
    else if (command == "save")
    {
      saveVisualizerConfiguration();
      Serial.println("OK configuration saved");
      SerialBt.println("OK configuration saved");
    }
    else if (command == "next")
    {
      btAudio.next();
      Serial.println("OK next");
      SerialBt.println("OK next");
    }
    else if (command == "prev" || command == "previous")
    {
      btAudio.previous();
      Serial.println("OK prev");
      SerialBt.println("OK prev");
    }
    else if (command == "play")
    {
      btAudio.play();
      Serial.println("OK play");
      SerialBt.println("OK play");
    }
    else if (command == "pause")
    {
      btAudio.pause();
      Serial.println("OK pause");
      SerialBt.println("OK pause");
    }
    else if (command == "stop")
    {
      btAudio.stop();
      Serial.println("OK stop");
      SerialBt.println("OK stop");
    }
    else if (command.startsWith("brightness"))
    {
      String value_text = command.substring(String("brightness").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("brightness#%d\n", leds_brightness);
        SerialBt.printf("brightness#%d\n", leds_brightness);
        continue;
      }

      int brightness = value_text.toInt();
      if (brightness < 0 || brightness > 100)
      {
        Serial.println("ERROR brightness must be 0-100");
        SerialBt.println("ERROR brightness must be 0-100");
        continue;
      }

      leds_brightness = brightness;
      setLedBrightness();
      FastLED.show();
      Serial.printf("OK brightness#%d\n", leds_brightness);
      SerialBt.printf("OK brightness#%d\n", leds_brightness);
    }
    else if (command.startsWith("delay"))
    {
      String value_text = command.substring(String("delay").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("delay#%d\n", leds_delay);
        SerialBt.printf("delay#%d\n", leds_delay);
        continue;
      }

      int delay_value = value_text.toInt();
      if (delay_value < 1 || delay_value > 50)
      {
        Serial.println("ERROR delay must be 1-50");
        SerialBt.println("ERROR delay must be 1-50");
        continue;
      }

      leds_delay = delay_value;
      Serial.printf("OK delay#%d\n", leds_delay);
      SerialBt.printf("OK delay#%d\n", leds_delay);
    }
    else if (command.startsWith("bassf"))
    {
      String value_text = command.substring(String("bassf").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("bassf#%d\n", bassf);
        SerialBt.printf("bassf#%d\n", bassf);
        continue;
      }

      int bass_value = value_text.toInt();
      if (bass_value < 0 || bass_value > 10)
      {
        Serial.println("ERROR bassf must be 0-10");
        SerialBt.println("ERROR bassf must be 0-10");
        continue;
      }

      bassf = bass_value;
      bass_filter_strength = bass_value;
      Serial.printf("OK bassf#%d\n", bassf);
      SerialBt.printf("OK bassf#%d\n", bassf);
    }
    else if (command.startsWith("bassl"))
    {
      String value_text = command.substring(String("bassl").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("bassl#%d\n", bassL);
        SerialBt.printf("bassl#%d\n", bassL);
        continue;
      }

      int bass_value = value_text.toInt();
      if (bass_value < 0 || bass_value > 8)
      {
        Serial.println("ERROR bassl must be 0-8");
        SerialBt.println("ERROR bassl must be 0-8");
        continue;
      }

      bassL = bass_value;
      bass_filter_strength = bass_value;
      Serial.printf("OK bassl#%d\n", bassL);
      SerialBt.printf("OK bassl#%d\n", bassL);
    }
    else if (command.startsWith("leds"))
    {
      String value_text = command.substring(String("leds").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("leds#%d\n", NUM_LEDS);
        SerialBt.printf("leds#%d\n", NUM_LEDS);
        continue;
      }

      int led_count = value_text.toInt();
      if (led_count < 1)
      {
        Serial.println("ERROR leds must be greater than 0");
        SerialBt.println("ERROR leds must be greater than 0");
        continue;
      }

      NUM_LEDS = led_count;
      FastLED.clear(true);
      Serial.printf("OK leds#%d\n", NUM_LEDS);
      SerialBt.printf("OK leds#%d\n", NUM_LEDS);
    }
    else if (command.startsWith("pattern"))
    {
      String value_text = command.substring(String("pattern").length());
      value_text.trim();

      if (value_text.startsWith("=") || value_text.startsWith("#"))
        value_text.remove(0, 1);
      value_text.trim();

      if (value_text.length() == 0)
      {
        Serial.printf("pattern#%d\n", visualizer_pattern);
        SerialBt.printf("pattern#%d\n", visualizer_pattern);
        continue;
      }

      int pattern = value_text.toInt();
      if (pattern < 0 || pattern > 3)
      {
        Serial.println("ERROR pattern must be 0-3");
        SerialBt.println("ERROR pattern must be 0-3");
        continue;
      }

      visualizer_pattern = pattern;
      FastLED.clear(true);
      Serial.printf("OK pattern#%d\n", visualizer_pattern);
      SerialBt.printf("OK pattern#%d\n", visualizer_pattern);
    }
    else if (command.startsWith("name"))
    {
      String new_name = command.substring(String("name").length());
      new_name.trim();

      if (new_name.startsWith("#"))
        new_name.remove(0, 1);
      new_name.trim();

      if (new_name.length() == 0)
      {
        SerialBt.println("Name cannot be empty");
      }
      else
      {
        SerialBt.print("Changing name to: ");
        SerialBt.println(new_name);
        bt_name = new_name;
        saveBluetoothName(new_name);
        btAudio.end();
        btAudio = BluetoothA2DPSink(out);
        btAudio.start(bt_name.c_str());
        btAudio.set_volume(current_volume);
        btAudio.set_auto_reconnect(true, 5);
        bt_on = true;
      }
    }
    else
    {
      Serial.println("ERROR unknown command");
      Serial.println("Available commands:");
      Serial.println("  brightness#<value> (0-100) default 70");
      Serial.println("Current configuration:");
      Serial.printf("brightness#%d\n", leds_brightness);
      Serial.println("  delay#<value> (1-50) 10 = fast, 25 = normal, 30 = slow, 40 = very slow  ");
      Serial.println("Current configuration:");
      Serial.printf("delay#%d\n", leds_delay);
      Serial.println("  bassf#<value> (0-8) 7 = is default");
      Serial.println("Current configuration:");
      Serial.printf("bassf#%d\n", bassf);
      Serial.println("  bassl#<value> (0-8) 4 = is default");
      Serial.println("Current configuration:");
      Serial.printf("bassl#%d\n", bassL);
      Serial.println("  leds#<value> number of active LEDs");
      Serial.println("Current configuration:");
      Serial.printf("leds#%d\n", NUM_LEDS);
      Serial.println("  pattern#<value> 0=classic, 1=pulse, 2=comet, 3=wave");
      Serial.println("Current configuration:");
      Serial.printf("pattern#%d\n", visualizer_pattern);
      Serial.println("  save  save current visualizer configuration");
      Serial.println("   print to print all configuration");

      SerialBt.println("ERROR unknown command");
      SerialBt.println("Available commands: brightness, delay, bassf, bassl, leds, pattern, save, print");
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, rxPin, txPin); // serial control port
  SerialBt.begin("");
  Serial.println("bt begin");

  loadVisualizerConfiguration();
  bt_name = loadSavedBluetoothName();
  current_volume = loadSavedVolume();
  Serial.print("Loaded saved volume: ");
  Serial.println(current_volume);
  Serial.print("Loaded saved device name: ");
  Serial.println(bt_name);

  pinMode(led, OUTPUT);
  pinMode(mute_Pin, OUTPUT);
  pinMode(mute_btn, INPUT_PULLUP);

  digitalWrite(mute_Pin, HIGH); // HIGH = unmute, LOW = mute
  mute_state = false;
  Serial.println("Bluetooth Controller Starting");
  auto cfg = out.defaultConfig();
  cfg.pin_bck = 26;
  cfg.pin_ws = 27;
  cfg.pin_data = 25;
  out.begin(cfg);

  FastLED.addLeds<WS2812B, rgb_pin_L, GRB>(ledsL, 64);
  FastLED.addLeds<WS2812B, rgb_pin_R, GRB>(ledsR, 64);
  setLedBrightness();
  FastLED.clear(true);

  // Analyze decoded PCM while retaining the normal I2S audio output.
  btAudio.set_stream_reader(readAudioForVisualizer, true);
  btAudio.set_avrc_rn_playstatus_callback(avrc_rn_playstatus_callback);
  btAudio.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE |
                                           ESP_AVRC_MD_ATTR_ARTIST |
                                           ESP_AVRC_MD_ATTR_ALBUM);
  btAudio.set_avrc_metadata_callback(avrcMetadataCallback);

  btAudio.set_spp_active(true);

  btAudio.start(bt_name.c_str());
  btAudio.set_volume(current_volume);
  btAudio.set_auto_reconnect(true, 5);
  Serial.println("Bluetooth Controller Initialized");
  Serial.print("Device name: ");
  Serial.println(bt_name);
  digitalWrite(led, HIGH);
}

void loop()
{
  processSerialCommands();
  serialVizualierCTRL();
  btcon();

  if (digitalRead(mute_btn) == LOW)
  {
    delay(50);
    if (digitalRead(mute_btn) == LOW)
    {
      if (mute_state)
      {
        digitalWrite(mute_Pin, HIGH);
        mute_state = false;
        Serial.println("Mute button: unmuted");
      }
      else
      {
        digitalWrite(mute_Pin, LOW);
        mute_state = true;
        Serial.println("Mute button: muted");
      }

      while (digitalRead(mute_btn) == LOW)
      {
        delay(10);
      }
    }
  }

  // soundPlay();
  loop_blink();
  updateVisualizer();
}
