# iFFT Orgel Source Code Structure

## Sound Engines
### iFFT Palette Additive Synthesis (palette_synth.cpp/.h)
- Description: Generates high-quality sound using iFFT and palette additive synthesis.
- Functions:
  - `ps_init()`: Initialization
  - `ps_note_on()` / `ps_note_off()`: Note on / off processing
  - `ps_all_off()`: All notes off
  - `ps_paint_palette()`: Palette rendering / painting
  - `ps_render_block()`: Audio block rendering
- Constants:
  - `CFG_MAX_POLYPHONY`: Maximum polyphony count
- Global Variables:
  - `ps_fill_seq` / `ps_cons_seq`: Produce / consume sequence numbers
  - `ps_active`: Active voice count

### Drum Engine (drum_engine.cpp/.h)
- Description: Plays 4-bit ADPCM drum samples.
- Functions:
  - `drum_engine_init()`: Initialization
  - `drum_engine_note_on()`: Drum note-on processing
  - `drum_engine_mix()`: Drum audio mixing
  - `drum_engine_active_count()`: Retrieves the number of active drum voices

## MIDI Playback
### MIDI Player (midi_player.cpp/.h)
- Description: Plays Standard MIDI Files (SMF).
- Class: `MidiPlayer`
  - `load_path()`: Loads a MIDI file
  - `start()` / `stop()`: Starts / stops playback
  - `pause()` / `resume()`: Pauses / resumes playback
  - `tick()`: Advances playback timing
  - `is_playing()` / `is_finished()`: Retrieves playback status
- Structure: `MidiEvent`
  - Represents a MIDI event
  - `timestamp`, `status`, `data1`, `data2`, `sysex_data`

### Song List Management (song_list.cpp/.h)
- Description: Manages the list of playable songs.
- Class: `SongList`
  - `load()`: Loads the song list
  - `count()`: Retrieves the total number of songs
  - `get()`: Retrieves song information
- Structure: `SongInfo`
  - Represents song information
  - `midi_path`, `title`, `author`, `arranger`, `has_title`, `has_arranger`

## USB Device Classes
### USB Mass Storage (usb_storage.cpp/.h)
- Description: Exposes internal flash memory as a USB Mass Storage device.
- Functions:
  - `usb_storage_begin()`: Initializes FatFS and USB storage exposure
  - `usb_pc_connected()`: Checks if connected to a PC (host mount status)
  - `usb_set_fs_busy()` / `usb_take_wants_mount()` / `usb_take_unplugged()`: Mutual exclusion control with PC host

### USB MIDI (usb_midi.cpp/.h)
- Description: Handles USB MIDI input.
- Functions:
  - `usb_midi_begin()`: Initializes the USB MIDI interface
  - `usb_midi_is_active()`: Checks MIDI reception status
  - `usb_midi_poll()`: Polls and processes MIDI input
  - `usb_midi_all_off()`: All notes off

## Text Rendering (text_render.cpp/.h)
- Description: Renders UTF-8 text (supports Japanese characters).
- Functions:
  - `text_init()`: Initialization
  - `text_set_color()`: Sets text color
  - `text_draw()` / `text_width()`: Draws text / calculates text width
  - `text_draw_clip()` / `text_draw_wrap()`: Text drawing with clipping / word-wrap

## Configuration (config.h)
- Defines various configuration values:
  - `CFG_SAMPLE_RATE`: Sampling rate
  - `CFG_MAX_POLYPHONY`: Maximum polyphony count
  - `CFG_MAX_SONGS`: Maximum number of songs
  - `DISPLAY_LANG`: Display language
  - `JPN_FONT`: Japanese font selection
  - `USE_USB_MSC` / `USE_USB_MIDI`: Enables/disables USB device classes

## Main (iFFT_MIDIl.ino)
- `setup()`: Initialization routines
- `loop()`: Main loop
  - USB-related processing
  - Advancing MIDI playback
  - Handling button inputs
  - Updating screen rendering
