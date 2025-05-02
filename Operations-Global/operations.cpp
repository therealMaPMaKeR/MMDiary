#include "operations.h"
#include "../CustomWidgets/custom_QListWidget.h"
#include "qdatetime.h"

namespace Operations
{

QString GetOrdinalSuffix(int number)
{
    int mod10 = number % 10;
    int mod100 = number % 100;

    if (mod10 == 1 && mod100 != 11) {
        return "st";
    } else if (mod10 == 2 && mod100 != 12) {
        return "nd";
    } else if (mod10 == 3 && mod100 != 13) {
        return "rd";
    } else {
        return "th";
    }
}

QString GetDayOfWeek(QDate date) {
    return date.toString("dddd"); // "dddd" format specifier gives the full day name
}



QString ConvertMonthtoText(QString month) //does as the name implies, converts integer month value to text.
{
    if(month == "01")
    {
        return "January";
    }
    else if(month == "02")
    {
        return "February";
    }
    else if(month == "03")
    {
        return "Mars";
    }
    else if(month == "04")
    {
        return "April";
    }
    else if(month == "05")
    {
        return "May";
    }
    else if(month == "06")
    {
        return "June";
    }
    else if(month == "07")
    {
        return "July";
    }
    else if(month == "08")
    {
        return "August";
    }
    else if(month == "09")
    {
        return "September";
    }
    else if(month == "10")
    {
        return "October";
    }
    else if(month == "11")
    {
        return "November";
    }
    else if(month == "12")
    {
        return "December";
    }
    else
    {
        return "ERROR";
    }
}

QString ConvertMonthtoInt(QString month) //does as the name implies, converts text month value to integer as a string
{
    if(month == "January")
    {
        return "01";
    }
    else if(month == "February")
    {
        return "02";
    }
    else if(month == "Mars")
    {
        return "03";
    }
    else if(month == "April")
    {
        return "04";
    }
    else if(month == "May")
    {
        return "05";
    }
    else if(month == "June")
    {
        return "06";
    }
    else if(month == "July")
    {
        return "07";
    }
    else if(month == "August")
    {
        return "08";
    }
    else if(month == "September")
    {
        return "09";
    }
    else if(month == "October")
    {
        return "10";
    }
    else if(month == "November")
    {
        return "11";
    }
    else if(month == "December")
    {
        return "12";
    }
    else
    {
        return "ERROR";
    }
}



int GetColumnIndexByName(QTableWidget* table, const QString& name) {
    for (int i = 0; i < table->columnCount(); ++i) {
        if (table->horizontalHeaderItem(i)->text() == name)
            return i;
    }
    return -1; // Not found
}


QString GetUniqueItemName(const QString& baseName, const QStringList& existingNames)
{
    // If the name doesn't exist in the list, return it as is
    if (!existingNames.contains(baseName)) {
        return baseName;
    }

    // Find a unique name by appending a number
    int counter = 1;
    QString candidateName;
    do {
        candidateName = QString("%1 (%2)").arg(baseName).arg(counter);
        counter++;
    } while (existingNames.contains(candidateName));

    return candidateName;
}

QList<QListWidgetItem*> GetListItems(QListWidget* list)
{
    QList<QListWidgetItem*> items;
    int count = list->count();
    for(int i=0;i<count;i++)
    {
        items.append(list->item(i));
    }
    return items;
}

QListWidgetItem* GetLastListItem(QListWidget* list)
{
    // Return nullptr if the list is null or empty
    if (!list || list->count() == 0) {
        return nullptr;
    }

    // Return the last item
    return list->item(list->count() - 1);
}

int GetIndexFromText(const QString& text, QComboBox* comboBox)
{
    // Return -1 if the comboBox is null
    if (!comboBox) {
        return -1;
    }

    // Find the index of the item with the given text
    return comboBox->findText(text);
}

int GetIndexFromText(const QString& text, QTabWidget* tabWidget)
{
    // Return -1 if the tabWidget is null
    if (!tabWidget) {
        return -1;
    }

    // Loop through all tabs to find the one with matching text
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabText(i) == text) {
            return i;
        }
    }

    // Text not found
    return -1;
}

int GetIndexFromText(const QString& text, QListWidget* listWidget)
{
    // Return -1 if the listWidget is null
    if (!listWidget) {
        return -1;
    }

    // Find items with the given text
    QList<QListWidgetItem*> items = listWidget->findItems(text, Qt::MatchExactly);

    // If any items were found, return the row of the first one
    if (!items.isEmpty()) {
        return listWidget->row(items.first());
    }

    // Text not found
    return -1;
}

}
