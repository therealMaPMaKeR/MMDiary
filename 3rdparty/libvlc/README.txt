LibVLC Integration Setup for MMDiary
=====================================

This directory structure is set up to integrate LibVLC into the MMDiary project.

REQUIRED FILES FROM VLC SDK:
============================

1. HEADERS (copy to include/vlc/):
   - From the VLC SDK, copy ALL .h files from sdk/include/vlc/ to:
     3rdparty/libvlc/include/vlc/
   
   Essential header files include:
   - vlc.h
   - libvlc.h
   - libvlc_media.h
   - libvlc_media_player.h
   - libvlc_events.h
   - libvlc_structures.h
   - libvlc_media_list.h
   - libvlc_media_list_player.h
   - libvlc_media_library.h
   - libvlc_media_discoverer.h
   - libvlc_renderer_discoverer.h
   - libvlc_picture.h
   - libvlc_dialog.h
   - libvlc_version.h
   - deprecated.h

2. LIBRARIES (copy to lib/):
   - libvlc.lib
   - libvlccore.lib

3. RUNTIME DLLs (copy to bin/):
   - libvlc.dll
   - libvlccore.dll
   - npvlc.dll (optional, for browser plugin support)
   - All DLLs from the VLC installation (if needed for specific codecs)

4. PLUGINS (copy to bin/plugins/):
   - Copy the entire plugins folder from VLC installation to:
     3rdparty/libvlc/bin/plugins/
   
   Essential plugin folders include:
   - access/
   - audio_filter/
   - audio_mixer/
   - audio_output/
   - codec/
   - control/
   - demux/
   - gui/
   - misc/
   - packetizer/
   - services_discovery/
   - stream_filter/
   - stream_out/
   - text_renderer/
   - video_chroma/
   - video_filter/
   - video_output/
   - video_splitter/

DIRECTORY STRUCTURE:
====================
3rdparty/libvlc/
├── include/
│   └── vlc/
│       ├── vlc.h
│       ├── libvlc.h
│       └── ... (other header files)
├── lib/
│   ├── libvlc.lib
│   └── libvlccore.lib
├── bin/
│   ├── libvlc.dll
│   ├── libvlccore.dll
│   └── plugins/
│       ├── access/
│       ├── audio_filter/
│       ├── codec/
│       └── ... (other plugin directories)
└── README.txt (this file)

DEPLOYMENT NOTES:
=================
When deploying your application:
1. Copy libvlc.dll and libvlccore.dll to your application's executable directory
2. Copy the plugins folder to your application's executable directory
3. The plugins folder must maintain its structure relative to libvlc.dll

VERSION COMPATIBILITY:
======================
- This setup is configured for VLC 3.0.x SDK
- Ensure you're using the 64-bit version of VLC SDK for x64 builds
- The VLC SDK version should match the VLC runtime version for best compatibility

TESTING:
========
After copying all required files, rebuild your project in Qt Creator.
The .pro file has been configured to:
- Include the libvlc headers
- Link against libvlc and libvlccore libraries
- Define USE_LIBVLC for conditional compilation

If you see warnings about missing libraries during compilation, ensure:
1. libvlc.lib and libvlccore.lib are in 3rdparty/libvlc/lib/
2. The files are the correct architecture (x64 for 64-bit builds)
3. The files are from a compatible VLC SDK version
