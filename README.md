# MMDiary
MMDiary Project


MMDiary is a secure personal information management application written in C++ using the Qt framework. It provides encrypted diary entries, task management, and password storage capabilities with a focus on privacy and security.



Features

Encrypted diary entries: Keep your thoughts private with AES-256-GCM encryption
Task management: Organize your tasks with customizable lists and notifications
Password manager: Securely store and manage your passwords
Data Encryptor: Allows you to encrypt any file you want and features a secure delete option with 3 passes.
Encrypted Video Player: Currently only supports tv shows, will implement movies and more later on. 

Architecture
MMDiary is built with the following technologies:

Qt 6.5.3: UI framework and core functionality
C++17: Modern C++ features for robust implementation
OpenSSL 3.5.0: Cryptography library for secure encryption
SQLite: Local database for user data storage

Build Requirements

Qt 6.5.3 or newer
MSVC 2019 64-bit compiler
Windows operating system
CMake 3.16+ (optional, if not using Qt Creator)

Project Setup for Developers
Clone the Repository
bashgit clone https://github.com/therealMaPMaKeR/MMDiary.git
cd MMDiary
Directory Structure
MMDiary/
├── 3rdparty/            # Third-party dependencies
│   └── openssl/         # OpenSSL headers and libraries
├── CustomWidgets/       # Custom UI components
├── Operations-Features/ # Application feature implementations
├── Operations-Global/   # Core functionality and utilities
├── QT_AESGCM256/        # Encryption implementation
├── resources.qrc        # Application resources
└── MMDiary.pro          # Qt project file
Building with Qt Creator

Open MMDiary.pro in Qt Creator
Configure the project with a kit that includes Qt 6.5.3 and MSVC 2019 64-bit
Build the project

Building from Command Line
bashmkdir build
cd build
qmake ../MMDiary.pro
nmake (or jom if available)


## TMDB Integration for Developers

This project supports TMDB (The Movie Database) for automatic TV show metadata.

### Setup for Developers:
1. Get a free API key from [TMDB](https://www.themoviedb.org/settings/api)
2. Rename `tmdb_api_key_TEMPLATE.h` to `tmdb_api_key.h`
3. Replace the placeholder with your Read Access Token (Bearer token)
4. Recompile the application - the key will be embedded in your binary

Note: The `tmdb_api_key.h` file is gitignored. Never commit your actual API key.
Each developer needs their own TMDB API key.


Security Architecture
MMDiary employs several security measures to protect user data:

AES-256-GCM encryption: All sensitive data is encrypted using industry-standard encryption
PBKDF2 key derivation: Password-based keys are derived using 1,000,000 iterations for enhanced security
In-memory protection: Encryption keys are securely wiped from memory when the application exits
Input validation: Comprehensive validation prevents injection attacks and other security issues
Path traversal protection: Strict file path validation prevents directory traversal attacks
Temp files are stored in the app's folder, making it easy to have peace of mind that no decrypted file remains on disk.
Uses secure delete when deleting temporarily decrypted files.

Data Storage
User data is stored in the following locations:

Database: ./Data/MMDiary.db - Contains user accounts.
Database: ./Data/[username]/settings.db - Contains user settings.
Database: ./Data/[username]/persistent.db - Contains persistent settings (window location, size, etc)
Diary entries: ./Data/[username]/Diaries/[year]/[month]/[day]/*.txt - Encrypted diary files
Task lists: ./Data/[username]/TaskLists/*.txt - Encrypted task list files
Passwords: ./Data/[username]/Passwords/passwords.txt - Encrypted password storage
Encrypted Data: ./Data/[username]/EncryptedData/(Archives/Document/Image/Video) - Stores data encrypted by the user.
Videoplayer: ./Data/[username]/Videoplayer/Shows - Stores imported tv shows. (encrypted)
Temp files: ./Data/[username]/Temp and ./Data/[username]/Temp/tempdecrypt - stores temp files.


All files are encrypted using AES-256-GCM with user-specific keys.


Contributing
I am not accepting contributions right now since the project is personal and I want to retain full control over it.
I will revisit this once this software is more complete.

License
This project is licensed under the GPLv3 License - see the LICENSE file for details.

Acknowledgments

Qt for their excellent framework
OpenSSL for cryptographic functionality
TMDB for their api that I use to retrieve information about tv shows and movies, such as posters and descriptions.
My brother H1nj0 for his interest in this personal app of mine.


For end users interested in simply using the application, please refer to the User Guide.
