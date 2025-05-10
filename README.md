# MMDiary
MMDiary Project


MMDiary is a secure personal information management application written in C++ using the Qt framework. It provides encrypted diary entries, task management, and password storage capabilities with a focus on privacy and security.



Features

Encrypted diary entries: Keep your thoughts private with AES-256-GCM encryption
Task management: Organize your tasks with customizable lists and notifications
Password manager: Securely store and manage your passwords
System tray integration: Minimize to tray for convenient access
Dark theme: Easy on the eyes for extended use

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


Security Architecture
MMDiary employs several security measures to protect user data:

AES-256-GCM encryption: All sensitive data is encrypted using industry-standard encryption
PBKDF2 key derivation: Password-based keys are derived using 1,000,000 iterations for enhanced security
In-memory protection: Encryption keys are securely wiped from memory when the application exits
Input validation: Comprehensive validation prevents injection attacks and other security issues
Path traversal protection: Strict file path validation prevents directory traversal attacks

Data Storage
User data is stored in the following locations:

Database: ./Data/MMDiary.db - Contains user accounts and settings
Diary entries: ./Data/[username]/Diaries/[year]/[month]/[day]/*.txt - Encrypted diary files
Task lists: ./Data/[username]/TaskLists/*.txt - Encrypted task list files
Passwords: ./Data/[username]/passwords.txt - Encrypted password storage

All files are encrypted using AES-256-GCM with user-specific keys.
Key Classes

MainWindow: Main application window and controller
Operations_Diary: Diary entry management
Operations_TaskLists: Task management
Operations_PasswordManager: Password storage and retrieval
CryptoUtils: Encryption and cryptographic operations
DatabaseManager: SQLite database interface
InputValidation: User input validation and security

Contributing
I am not accepting contributions right now since the project is personal and I want to retain full control over it.
I will revisit this once this software is more complete.

License
This project is licensed under the GPLv3 License - see the LICENSE file for details.

Acknowledgments

Qt for their excellent framework
OpenSSL for cryptographic functionality
My brother H1nj0 for his interest in this personal app of mine.


For end users interested in simply using the application, please refer to the User Guide.
