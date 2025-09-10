#ifndef OPERATIONS_H
#define OPERATIONS_H
#include <QObject>
#include <QDate>
#include <QListWidget>
#include <QComboBox>
#include <QTabWidget>
#include "qlist_DiaryTextDisplay.h"
#include <QTableWidget>

namespace Operations
{
QString GetOrdinalSuffix(int number);
QString GetDayOfWeek(QDate date);
QString ConvertMonthtoText(QString month);
QString ConvertMonthtoInt(QString month);
int GetColumnIndexByName(QTableWidget* table, const QString& name);
QString GetUniqueItemName(const QString& baseName, const QStringList& existingNames);
QList<QListWidgetItem*> GetListItems(QListWidget *list);
QListWidgetItem* GetLastListItem(QListWidget* list);
// New overloaded functions to get index from text
int GetIndexFromText(const QString& text, QComboBox* comboBox);
int GetIndexFromText(const QString& text, QTabWidget* tabWidget);
int GetIndexFromText(const QString& text, QListWidget* listWidget);
int GetTabIndexByObjectName(const QString& objectName, QTabWidget* tabWidget);
};

#endif // OPERATIONS_H
