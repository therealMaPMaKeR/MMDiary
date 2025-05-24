#include "operations_encrypteddata.h"
#include "../CustomWidgets/CombinedDelegate.h"
#include "../Operations-Global/CryptoUtils.h"
#include "ui_mainwindow.h"
#include "../constants.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>

Operations_TextsManager::Operations_TextsManager(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
}

