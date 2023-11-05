﻿#ifndef CREATEBACKUPFILEWIDGET_H
#define CREATEBACKUPFILEWIDGET_H

#include <QFrame>

class QListView;
class QLineEdit;
class QLabel;
class QStorageInfo;
class CreateBackupFileWidget : public QFrame
{
    Q_OBJECT
public:
    CreateBackupFileWidget(QWidget *parent = nullptr);
    ~CreateBackupFileWidget();

    void sendOptions();

private:
    void initUI();
    void initDiskListView();

public slots:
    void nextPage();
    void backPage();
    void updateuserSelectFileSize(const QString &sizeStr);
    void updaeBackupFileSize();
    void getUpdateDeviceSingla();
    void updateDevice(const QStorageInfo &device, const bool &isAdd);
private:
    quint64 userSelectFileSize{ 0 };

    QString backupFileName{ "" };

    QLabel *backupFileSizeLabel{ nullptr };
    QListView *diskListView{ nullptr };
    QLineEdit *fileNameInput{ nullptr };
    QList<QStorageInfo> deviceList;
};

#endif // CREATEBACKUPFILEWIDGET_H
