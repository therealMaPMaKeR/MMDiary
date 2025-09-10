# MMDiary Project

**MMDiary** is a secure personal information management application written in C++ using the Qt framework.

## ✨ Features

- 🔐 **Encrypted Diary Entries** - Keep your thoughts private with AES-256-GCM encryption
- ✅ **Task Management** - Organize your tasks with customizable lists and notifications  
- 🔑 **Password Manager** - Securely store and manage your passwords
- 📁 **Data Encryptor** - Encrypt any file with secure deletion (3-pass overwrite)
- 🎬 **Video Player** - Built-in media player (currently supports TV shows, movies coming soon)
- 🥽 **VR Support** - Special VR video player for encrypted content

## 🏗️ Architecture

MMDiary is built with the following technologies:

- **Qt 6.5.3** - UI framework and core functionality
- **C++17** - Modern C++ features for robust implementation
- **OpenSSL 3.5.0** - Cryptography library for secure encryption
- **SQLite** - Local database for user data storage

## 📋 Build Requirements

- **Qt 6.5.3** or newer
- **MSVC 2019** 64-bit compiler  
- **Windows OS** (Windows-only application)
- **CMake 3.16+** (optional, if not using Qt Creator)
- **OpenSSL 3.5.0** (static libraries required)
- **libVLC 3.0+** (for video playback)
- **OpenVR 1.16+** (for VR support, optional)

## 🛠️ Project Setup for Developers

## Prerequisites - Third-Party Dependencies

### 1. OpenSSL 3.5.0 (Required for AES-256-GCM Encryption)

**Static Linking Required**: This project requires OpenSSL to be statically linked for release builds.

#### Setup:
1. Download OpenSSL 3.5.0 static libraries for Windows x64 MSVC
2. Place the files in the following structure:
```
MMDiary/
└── 3rdparty/
    └── openssl/
        ├── include/       # OpenSSL headers
        │   └── openssl/
        │       ├── aes.h
        │       ├── evp.h
        │       └── ... (other headers)
        └── lib/
            └── VC/
                └── x64/
                    └── MD/
                        ├── libssl_static.lib
                        └── libcrypto_static.lib
```

**Note**: For debug builds, the project uses system OpenSSL at `C:/Projects/OpenSSL-Win64`

### 2. libVLC (Required for Video Playback)

**Dynamic Linking**: libVLC is dynamically linked and requires runtime DLLs.

#### Setup:
1. Download VLC SDK 3.0+ for Windows x64
2. Place the files in the following structure:
```
MMDiary/
└── 3rdparty/
    └── libvlc/
        ├── include/
        │   └── vlc/
        │       ├── vlc.h
        │       ├── libvlc.h
        │       └── ... (other headers)
        ├── lib/
        │   ├── libvlc.lib
        │   └── libvlccore.lib
        └── bin/
            ├── libvlc.dll
            ├── libvlccore.dll
            └── plugins/    # VLC plugins directory (required)
                ├── access/
                ├── audio_output/
                ├── codec/
                ├── video_output/
                └── ... (other plugin folders)
```

**Important**: The plugins directory is essential for video playback functionality.

### 3. OpenVR (Optional for VR Support)

**Dynamic Linking**: OpenVR support for SteamVR integration.

#### Setup:
1. Download OpenVR SDK 1.16+ from [GitHub](https://github.com/ValveSoftware/openvr)
2. Place the files in the following structure:
```
MMDiary/
└── 3rdparty/
    └── openvr/
        ├── include/
        │   ├── openvr.h
        │   └── openvr_driver.h
        ├── lib/
        │   └── win64/
        │       └── openvr_api.lib
        └── bin/
            └── win64/
                └── openvr_api.dll
```

**Requirements for VR**: 
- SteamVR must be installed
- A compatible VR headset (HTC Vive, Valve Index, Oculus Rift, etc.)

## Clone the Repository
```bash
git clone https://github.com/therealMaPMaKeR/MMDiary.git
cd MMDiary
```

## Directory Structure
```
MMDiary/
├── 3rdparty/            # Third-party dependencies (see setup above)
│   ├── openssl/         # OpenSSL headers and static libraries
│   ├── libvlc/          # VLC headers, libraries, and plugins
│   └── openvr/          # OpenVR headers and libraries
├── CustomWidgets/       # Custom UI components
├── Operations-Features/ # Application feature implementations
├── Operations-Global/   # Core functionality and utilities
├── QT_AESGCM256/        # Encryption implementation
├── resources.qrc        # Application resources
└── MMDiary.pro          # Qt project file
```
## Building with Qt Creator

1. Ensure all dependencies are properly placed in `3rdparty/` directory
2. Open MMDiary.pro in Qt Creator
3. Configure the project with a kit that includes Qt 6.5.3 and MSVC 2019 64-bit
4. Build the project
5. The build process will automatically copy required DLLs to the output directory

## Building from Command Line
```bash
mkdir build
cd build
qmake ../MMDiary.pro
nmake  # or jom if available
```

**Post-Build**: The project automatically copies:
- VLC DLLs and plugins to the output directory
- OpenVR DLL to the output directory
- OpenSSL is statically linked (no DLLs needed for release)


## TMDB Integration for Developers

This project supports TMDB (The Movie Database) for automatic TV show metadata.

### Setup for Developers:
1. Get a free API key from [TMDB](https://www.themoviedb.org/settings/api)
2. Rename `tmdb_api_key_TEMPLATE.h` to `tmdb_api_key.h`
3. Replace the placeholder with your Read Access Token (Bearer token)
4. Recompile the application - the key will be embedded in your binary

Note: The `tmdb_api_key.h` file is gitignored. Never commit your actual API key.

Each developer needs their own TMDB API key.


## 🔒 Security Architecture

MMDiary employs several security measures to protect user data:

- **AES-256-GCM Encryption** - All sensitive data is encrypted using industry-standard encryption
- **PBKDF2 Key Derivation** - Password-based keys are derived using 1,000,000 iterations for enhanced security
- **In-Memory Protection** - Encryption keys are securely wiped from memory when the application exits
- **Input Validation** - Comprehensive validation prevents injection attacks and other security issues
- **Path Traversal Protection** - Strict file path validation prevents directory traversal attacks
- **Secure Temp Files** - Temporary files are stored in the app's folder, ensuring no decrypted data remains on disk
- **Secure Deletion** - Uses 3-pass overwrite when deleting temporarily decrypted files


## 📂 Data Storage

User data is stored in the following locations:

### Database Files
- `./Data/MMDiary.db` - Contains user accounts
- `./Data/[username]/settings.db` - Contains user settings
- `./Data/[username]/persistent.db` - Contains persistent settings (window location, size, etc)

### User Data
- **Diary Entries:** `./Data/[username]/Diaries/[year]/[month]/[day]/*.txt` - Encrypted diary files
- **Task Lists:** `./Data/[username]/TaskLists/*.txt` - Encrypted task list files
- **Passwords:** `./Data/[username]/Passwords/passwords.txt` - Encrypted password storage
- **Encrypted Data:** `./Data/[username]/EncryptedData/(Archives/Document/Image/Video)` - User-encrypted files
- **Video Player:** `./Data/[username]/Videoplayer/Shows` - Imported TV shows (encrypted)

### Temporary Files
- **Temp Files:** `./Data/[username]/Temp` and `./Data/[username]/Temp/tempdecrypt`
- **Temp Locks:** `./Data/[username]/temp_metadata_locks` - TV show player metadata locks

> ⚠️ **Note:** All files are encrypted using AES-256-GCM with user-specific keys.


## 🤝 Contributing

> ℹ️ **Note:** I am not accepting contributions right now since the project is personal and I want to retain full control over it.

I will revisit this once the software is more complete.


## Third-Party Licenses and Legal Information

### MMDiary License
This project is licensed under the **GNU General Public License v3.0 (GPLv3)** - see the LICENSE file for details.

### Third-Party Library Licenses

#### OpenSSL 3.5.0
- **License**: Apache License 2.0
- **Usage**: Static linking permitted
- **Compatibility**: Apache 2.0 is compatible with GPLv3
- **Note**: Since MMDiary is GPLv3, you can freely use OpenSSL. The Apache 2.0 license requires attribution, which is provided in this documentation.

#### libVLC (VideoLAN)
- **License**: GNU Lesser General Public License v2.1 (LGPL v2.1)
- **Usage**: Dynamic linking (DLLs distributed separately)
- **Compatibility**: LGPL v2.1 is compatible with GPLv3
- **Distribution Requirements**: 
  - Must provide libVLC source code or link to it (available at [VideoLAN.org](https://www.videolan.org))
  - VLC plugins must be included for functionality
  - libVLC DLLs remain under LGPL and are not affected by MMDiary's GPLv3 license

#### OpenVR (Valve Software)
- **License**: BSD 3-Clause License
- **Usage**: Dynamic linking
- **Compatibility**: BSD license is fully compatible with GPLv3
- **Note**: Very permissive license, no special requirements. OpenVR is property of Valve Corporation.

### Important Legal Notes

1. **Static vs Dynamic Linking**:
   - OpenSSL is statically linked in release builds (allowed under Apache 2.0 + GPLv3)
   - libVLC and OpenVR are dynamically linked (DLLs distributed separately)

2. **Distribution**:
   - When distributing MMDiary, include all required VLC plugins
   - Ensure OpenVR DLL is included if VR features are enabled
   - Provide access to source code as required by GPLv3

3. **Patents**: Be aware that some video codecs used by VLC may be subject to patents in certain jurisdictions.

## Acknowledgments

- **Qt Framework** for their excellent cross-platform framework
- **OpenSSL Project** for robust cryptographic functionality
- **VideoLAN Team** for libVLC and the amazing VLC media player
- **Valve Software** for OpenVR and SteamVR support
- **TMDB** for their API used to retrieve TV show and movie metadata
- My brother **H1nj0** for his interest in this personal app of mine


For end users interested in simply using the application, please refer to the User Guide.
