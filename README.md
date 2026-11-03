# SE_FNF_PSP - Psych Engine PSP Port

![Platform](https://img.shields.io/badge/Platform-PlayStation%20Portable-blue.svg)
![Language](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B-lightgrey.svg)
![Engine](https://img.shields.io/badge/Engine-Psych%200.6.3-red.svg)

A high-performance, custom C-based port of **Psych Engine (Friday Night Funkin')** built specifically for the **Sony PlayStation Portable (PSP)**. 

This project was born out of the desire to play FNF on native PSP hardware without the heavy overhead of the original HaxeFlixel/OpenFL engine. The engine has been completely rewritten in C to utilize the PSP's GU (Graphics Unit) and audio hardware directly, ensuring smooth gameplay even on the original PSP Fat (1000) models.

---

## 📖 Table of Contents
1. [Engine Features](#-engine-features)
2. [Technical Limitations & Asset Formats](#-technical-limitations--asset-formats)
3. [Tools & Asset Preparation](#-tools--asset-preparation)
4. [Folder Structure & Installation](#-folder-structure--installation)
5. [Modding Support & Load Order](#-modding-support--load-order)
6. [Controls](#-controls)
7. [Building from Source](#-building-from-source)
8. [Troubleshooting & FAQ](#-troubleshooting--faq)
9. [Credits & Legal](#-credits--legal)

---

## 🚀 Engine Features

* **Native C Implementation:** Built directly on the PSPSDK (`pspgu`, `pspaudio`, `pspctrl`). No Lua, no Haxe, no Virtual Machines. 
* **Multi-Mod Support:** Load multiple mods simultaneously with a stacking file system.
* **Dynamic Freeplay Menu:** Fully functional Freeplay menu with support for custom song colors, custom difficulties per week, and instrumental previews.
* **Auto-Baked Characters:** Automatically parses Adobe Animate `Animation.json` files and caches them into a custom `.chr` binary format for instant loading on the next boot.
* **Robust Audio Sync:** Custom audio thread utilizing raw PCM streaming to maintain perfect sync between the instrumental, vocals, and the Conductor.
* **Camera & Stage System:** Supports Psych Engine's stage JSON files, including camera zooming, character positioning, and stage events (like "Add Camera Zoom").

---

## ⚠️ Technical Limitations & Asset Formats

The PSP is a 2004 handheld with strict hardware limitations (32MB or 64MB of RAM). **Standard `.ogg` and `.png` files from PC are NOT supported.** You cannot simply drag and drop your PC `mods` folder into the PSP. 

Here is exactly how you must format your assets:

### 🎵 Audio (`.pcm`)
Decoding `.ogg` or `.mp3` files on the fly consumes too much CPU on the PSP. This engine streams **Raw PCM** (Pulse Code Modulation) audio.
* **Format:** `s16le` (Signed 16-bit Little Endian).
* **Channels:** **Mono (1 channel)**. Do not use Stereo.
* **Sample Rate:** **22050 Hz** (Recommended for balance of quality and size) or **11025 Hz** (If you are running out of space).
* **File Naming:** Must be exactly `Inst.pcm` and `Voices.pcm`.
* **Storage Warning:** PCM is uncompressed. A 3-minute song will take roughly 8 MB. A full week with 3 songs (Inst + Voices) will take ~50 MB.

### 🖼️ Textures (`.tex` / `.img`)
The PSP GU (Graphics Unit) requires textures to be **Power of Two (POT)**. The maximum texture resolution the PSP can handle is **512x512**.
* **Format:** `.tex` (requires a 4-byte or 8-byte header containing the width and height) or `.img` (raw data).
* **Resolution:** The game renders at `480x272`. Backgrounds or sprites larger than `512x512` must be scaled down or split.
* **POT Rules:** Valid sizes are `128x128`, `256x256`, `512x512`. A `480x272` image will be padded to `512x512` in memory, which wastes VRAM. It is highly recommended to export your UI elements as POT.

---

## 🛠️ Tools & Asset Preparation

Inside the `tools/` directory of this repository, you will find Python scripts designed to automate the conversion process. You will need Python 3 installed on your PC and `ffmpeg` (the script can auto-install it via `pip install imageio-ffmpeg`).

### 1. Audio Conversion (`tools/convert_audio.py`)
This script recursively scans your folders and converts `.ogg` / `.mp3` to `.pcm`.

```bash
# Example: Convert a whole folder to Mono 22050Hz
python tools/convert_audio.py "path/to/assets/songs" --channels 1 --rate 22050

# Example: Force re-conversion if files already exist
python tools/convert_audio.py "path/to/assets/songs" --channels 1 --force
